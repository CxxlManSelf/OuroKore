#include "RuntimeContext.h"

#include <utility>

#include "ourokore/c_api/host_api.h"
#include "internal_api.h"
#include "ourokore/component/BlueprintPackaging.hpp"
#include "ourokore/component/builtin/NoOpAutoDehydrator.hpp"

namespace ork
{

RuntimeContext &RuntimeContext::GetInstance()
{
  static RuntimeContext s_instance;
  return s_instance;
}

bool RuntimeContext::Initialize(std::shared_ptr<IStorageDriver> driver,
                                std::shared_ptr<IAutoDehydrator> auto_dehydrator,
                                std::shared_ptr<ork::base::FixedThreadPool> thread_pool)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_initialized.load(std::memory_order_acquire))
  {
    return false;  // 單向不可變防線已鎖定，拒絕後續任何外掛重複初始化！
  }

  // 確保底層 C API 循環回收與延遲隊列啟動
  ork_try_initialize_core();

  m_storage_driver = std::move(driver);

  if (auto_dehydrator)
  {
    m_auto_dehydrator = std::move(auto_dehydrator);
  }
  else
  {
    m_auto_dehydrator = std::make_shared<NoOpAutoDehydrator>();
  }

  if (thread_pool)
  {
    m_thread_pool = std::move(thread_pool);
    m_is_core_owned_pool = false;
  }
  else
  {
    m_thread_pool = std::make_shared<ork::base::FixedThreadPool>();
    m_is_core_owned_pool = true;
  }

  m_is_initialized.store(true, std::memory_order_release);
  return true;
}

void RuntimeContext::FlushStorage()
{
  std::shared_ptr<ork::base::FixedThreadPool> pool;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    pool = m_thread_pool;
  }

  while (true)
  {
    // 1. 同步排空延遲物理銷毀隊列
    ork_flush_deferred_deletions();

    // 2. 若有儲存執行緒池，等待所有藍圖寫入與刪除任務完成
    if (pool && pool->is_running())
    {
      pool->wait_idle();
    }

    // 3. 檢查在 ThreadPool 執行過程中，是否又有 OuroPtr 解構產生了新的延遲銷毀任務
    if (ork_get_deferred_delete_pending_count() == 0)
    {
      break;
    }
  }
}

void RuntimeContext::Shutdown()
{
  bool expected = false;
  if (!m_is_shutdown_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
  {
    return;
  }

  // 1. 先停用循環參照收集器背景巡檢，並執行一次最後的同步外科手術收集，解開所有殘留孤島
  ork_stop_cycle_collector();
  ork_collect_cycles();

  // 2. 雙管線收斂排空：確保延遲銷毀與執行緒池任務全部完成落盤
  FlushStorage();

  // 3. 立即解除全域銷毀通知回呼：關閉水龍頭，防止後續任何解構引發新任務被分發至執行緒池
  ork_set_object_destroyed_callback(nullptr);

  // 4. 停止延遲銷毀執行緒池並等待其工作執行緒安全退出
  ork_stop_deferred_deletions();

  // 5. 處理儲存執行緒池：依所有權決定是否強制 stop
  std::shared_ptr<ork::base::FixedThreadPool> pool;
  bool is_owned = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    pool = m_thread_pool;
    is_owned = m_is_core_owned_pool;
  }

  if (pool)
  {
    if (is_owned)
    {
      pool->stop();
    }
    else
    {
      pool->wait_idle();
    }
  }

  // 6. 確認所有執行緒徹底終止後，才安全重置靜態指標與初始化狀態
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_thread_pool = nullptr;
    m_storage_driver = nullptr;
    m_auto_dehydrator = nullptr;
    m_is_core_owned_pool = false;
    m_is_initialized.store(false, std::memory_order_release);
  }

  m_is_shutdown_running.store(false, std::memory_order_release);
}

bool RuntimeContext::Dehydrate(HandleID id)
{
  if (id == 0)
  {
    return false;
  }

  // 1. In-Flight 安全檢查：嚴格要求 root_count 必須為 0！
  uint32_t root_count = 0;
  if (ork_get_root_edge_count(id, &root_count) != ORK_STATUS_OK || root_count > 0)
  {
    return false;  // 物件正處於 In-Flight 活躍執行中，安全跳過
  }

  // 2. RAII 獨佔寫鎖
  struct DehydrateExclusiveLock
  {
    HandleID m_id;
    explicit DehydrateExclusiveLock(HandleID target_id) : m_id(target_id)
    {
      ork_lock_object(m_id);
    }
    ~DehydrateExclusiveLock()
    {
      ork_unlock_object(m_id);
    }
  } lock_guard(id);

  // 在鎖定下雙重檢查 root_count
  if (ork_get_root_edge_count(id, &root_count) != ORK_STATUS_OK || root_count > 0)
  {
    return false;
  }

  uint8_t state_val = 0;
  if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
      static_cast<StorageState>(state_val) == StorageState::Dehydrated)
  {
    return true;  // 已經是脫水狀態
  }

  ::OuroObject *raw_obj = nullptr;
  if (ork_acquire_object_pointer(id, &raw_obj) != ORK_STATUS_OK || !raw_obj)
  {
    return false;
  }

  OuroObject *obj = reinterpret_cast<OuroObject *>(raw_obj);

  // 3. 若為 UnsavedNew 或 Dirty，自動串流寫入儲存體
  StorageState current_state = obj->GetStorageState();
  if (current_state == StorageState::UnsavedNew || current_state == StorageState::Dirty)
  {
    auto driver = GetStorageDriver();
    if (!driver)
    {
      return false;
    }
    auto stream = driver->CreateWriteStream(id);
    if (!stream)
    {
      return false;
    }
    PackBlueprint(*obj, *stream);
    stream->Commit();
  }

  // 4. 標記為 Dehydrated 並安全釋放 Payload 肉體記憶體（必定回到原建立模組之 CRT 堆疊釋放）
  ork_set_storage_state(id, static_cast<uint8_t>(StorageState::Dehydrated));
  ork_destroy_payload(id);

  // 5. 通知自動脫水模組物件已脫水
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->OnObjectDehydrated(id);
  }
  return true;
}

void RuntimeContext::OnObjectDestroyed(HandleID id)
{
  auto driver = GetStorageDriver();
  if (driver)
  {
    auto pool = GetThreadPool();
    if (pool && pool->is_running())
    {
      pool->submit_detached([driver, id]() { driver->DeleteBlueprint(id); });
    }
    else
    {
      driver->DeleteBlueprint(id);
    }
  }

  auto dehydrator = GetAutoDehydrator();
  if (dehydrator)
  {
    dehydrator->Unregister(id);
  }
}

std::shared_ptr<IStorageDriver> RuntimeContext::GetStorageDriver() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_storage_driver;
}

std::shared_ptr<IAutoDehydrator> RuntimeContext::GetAutoDehydrator() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_auto_dehydrator)
  {
    const_cast<RuntimeContext *>(this)->m_auto_dehydrator = std::make_shared<NoOpAutoDehydrator>();
  }
  return m_auto_dehydrator;
}

std::shared_ptr<ork::base::FixedThreadPool> RuntimeContext::GetThreadPool() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_thread_pool;
}

bool RuntimeContext::IsInitialized() const
{
  return m_is_initialized.load(std::memory_order_acquire);
}

bool RuntimeContext::IsShutdownRunning() const
{
  return m_is_shutdown_running.load(std::memory_order_acquire);
}

void RuntimeContext::SetAutoDehydrator(std::shared_ptr<IAutoDehydrator> dehydrator)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (dehydrator)
  {
    m_auto_dehydrator = std::move(dehydrator);
  }
  else
  {
    m_auto_dehydrator = std::make_shared<NoOpAutoDehydrator>();
  }
}

void RuntimeContext::SetStorageDriver(std::shared_ptr<IStorageDriver> driver)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_storage_driver = std::move(driver);
}

void RuntimeContext::NotifyObjectRegistered(HandleID id, size_t size_bytes)
{
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->Register(id, size_bytes);
  }
}

void RuntimeContext::NotifyObjectDehydrated(HandleID id)
{
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->OnObjectDehydrated(id);
  }
}

void RuntimeContext::NotifyObjectRehydrated(HandleID id)
{
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->OnObjectRehydrated(id);
  }
}

DehydrationReport RuntimeContext::TriggerDehydrationRescue(size_t bytes_needed)
{
  if (auto dehydrator = GetAutoDehydrator())
  {
    return dehydrator->TriggerDehydration(bytes_needed);
  }
  return DehydrationReport{};
}

void RuntimeContext::Reset()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_storage_driver = nullptr;
  m_auto_dehydrator = nullptr;
  m_thread_pool = nullptr;
  m_is_core_owned_pool = false;
  m_is_shutdown_running.store(false, std::memory_order_relaxed);
  m_is_initialized.store(false, std::memory_order_release);
}

}  // namespace ork
