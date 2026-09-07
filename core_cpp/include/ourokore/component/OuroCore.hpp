#pragma once

#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "AsyncResult.hpp"
#include "Handles.hpp"
#include "IAutoDehydrator.hpp"
#include "IStorageDriver.hpp"
#include "NoOpAutoDehydrator.hpp"
#include "OuroObject.hpp"
#include "OuroStream.hpp"
#include "ourokore/base/ThreadPool.hpp"
#include "ourokore/c_api/component_api.h"
#include "ourokore/c_api/core.h"

namespace ork
{

namespace detail
{
inline std::shared_ptr<IStorageDriver> &GetStorageDriverRef()
{
  static std::shared_ptr<IStorageDriver> s_driver = nullptr;
  return s_driver;
}

inline std::shared_ptr<IAutoDehydrator> &GetAutoDehydratorRef()
{
  static std::shared_ptr<IAutoDehydrator> s_dehydrator = nullptr;
  return s_dehydrator;
}

inline std::shared_ptr<ork::base::FixedThreadPool> &GetCoreThreadPoolRef()
{
  static std::shared_ptr<ork::base::FixedThreadPool> s_pool = nullptr;
  return s_pool;
}

inline void OnObjectDestroyed(HandleID id)
{
  auto driver = GetStorageDriverRef();
  if (driver)
  {
    auto pool = GetCoreThreadPoolRef();
    if (pool && pool->is_running())
    {
      pool->submit_detached([driver, id]() { driver->DeleteBlueprint(id); });
    }
    else
    {
      driver->DeleteBlueprint(id);
    }
  }
  auto &dehydrator = GetAutoDehydratorRef();
  if (dehydrator)
  {
    dehydrator->Unregister(id);
  }
}
}  // namespace detail

/**
 * @brief 取得當前註冊的自動脫水外掛模組（若未指定則自動回傳 NoOpAutoDehydrator，保證永不為 null）
 */
inline std::shared_ptr<IAutoDehydrator> GetAutoDehydrator()
{
  auto &ref = detail::GetAutoDehydratorRef();
  if (!ref)
  {
    ref = std::make_shared<NoOpAutoDehydrator>();
  }
  return ref;
}

/**
 * @brief 取得核心執行緒池（若未配置則可能為 null）
 */
inline std::shared_ptr<ork::base::FixedThreadPool> GetCoreThreadPool()
{
  return detail::GetCoreThreadPoolRef();
}

/**
 * @brief 等待所有排隊中的背景儲存與銷毀任務完全落盤排空 (Flush)
 */
inline void FlushStorage()
{
  auto pool = detail::GetCoreThreadPoolRef();
  if (pool && pool->is_running())
  {
    pool->wait_idle();
  }
}

/**
 * @brief 優雅終止核心執行緒池與背景任務，確保退出時無死鎖與資料遺失
 */
inline void Shutdown()
{
  FlushStorage();
  auto pool = detail::GetCoreThreadPoolRef();
  if (pool)
  {
    pool->stop();
    detail::GetCoreThreadPoolRef() = nullptr;
  }
}

/**
 * @brief Initialize OuroKore Core with a persistent storage driver, optional auto-dehydrator plugin and optional thread pool.
 * @note One-Way Immutable: Only the host application's first call takes effect.
 * Subsequent calls from plugins or other modules are safely ignored (no-op).
 * @return true if successfully initialized by host, false if core has already been initialized.
 */
inline bool Init(std::shared_ptr<IStorageDriver> driver,
                 std::shared_ptr<IAutoDehydrator> auto_dehydrator = nullptr,
                 std::shared_ptr<ork::base::FixedThreadPool> thread_pool = nullptr)
{
  if (ork_try_initialize_core() != ORK_STATUS_OK)
  {
    return false;  // 已由主程式初始化，外掛重複呼叫安全略過
  }

  detail::GetStorageDriverRef() = std::move(driver);
  if (auto_dehydrator)
  {
    detail::GetAutoDehydratorRef() = std::move(auto_dehydrator);
  }
  else
  {
    detail::GetAutoDehydratorRef() = std::make_shared<NoOpAutoDehydrator>();
  }

  if (thread_pool)
  {
    detail::GetCoreThreadPoolRef() = std::move(thread_pool);
  }
  else
  {
    detail::GetCoreThreadPoolRef() = std::make_shared<ork::base::FixedThreadPool>();
  }

  // 註冊全域物件銷毀勾點：當物件 strong_count 與 weak_count 皆歸零死亡時，自動清除 storage 與通知外掛
  ork_set_object_destroyed_callback(&detail::OnObjectDestroyed);
  return true;
}

/**
 * @brief Get currently registered storage driver.
 */
inline std::shared_ptr<IStorageDriver> GetStorageDriver()
{
  return detail::GetStorageDriverRef();
}

/**
 * @brief Backward compatibility alias for GetStorageDriver().
 */
inline std::shared_ptr<IStorageDriver> GetStorageBackend()
{
  return GetStorageDriver();
}

/**
 * @brief Pack an OuroObject's Payload and Edge Roster into any OuroStream.
 * @note Core only packs Payload + Edge Roster (Slot Name -> Target HandleID).
 * Core does NOT dictate physical disk layout or magic numbers.
 */
inline void PackBlueprint(const OuroObject &obj, OuroStream &stream)
{
  // 1. Serialize Pure Payload
  obj.SerializePayload(stream);

  // 2. Automatically traverse m_registered_handles roster to pack Edge Roster
  const auto &handles = obj.GetRegisteredHandles();
  uint32_t edge_count = static_cast<uint32_t>(handles.size());

  stream.WriteBytes(reinterpret_cast<const uint8_t *>(&edge_count), sizeof(edge_count));

  for (const auto &[slot_name, handle_ptr] : handles)
  {
    stream.WriteStringRaw(slot_name);
    const auto &target_ids = handle_ptr->GetTargetIDs();
    uint32_t target_count = static_cast<uint32_t>(target_ids.size());
    stream.WriteBytes(reinterpret_cast<const uint8_t *>(&target_count), sizeof(target_count));
    for (HandleID tid : target_ids)
    {
      stream.WriteBytes(reinterpret_cast<const uint8_t *>(&tid), sizeof(tid));
    }
  }
}

/**
 * @brief Unpack an OuroObject's Payload and Edge Roster from any OuroStream.
 */
inline void UnpackBlueprint(OuroObject &obj, OuroStream &stream)
{
  // 1. Deserialize Pure Payload
  obj.DeserializePayload(stream);

  // 2. Deserialize Edge Roster if stream still has remaining bytes
  if (stream.HasRemainingBytes())
  {
    uint32_t edge_count = 0;
    stream.ReadBytes(reinterpret_cast<uint8_t *>(&edge_count), sizeof(edge_count));
    const auto &handles = obj.GetRegisteredHandles();

    for (uint32_t i = 0; i < edge_count; ++i)
    {
      std::string slot_name = stream.ReadStringRaw();
      uint32_t target_count = 0;
      stream.ReadBytes(reinterpret_cast<uint8_t *>(&target_count), sizeof(target_count));

      std::vector<HandleID> tids(target_count);
      for (uint32_t j = 0; j < target_count; ++j)
      {
        stream.ReadBytes(reinterpret_cast<uint8_t *>(&tids[j]), sizeof(tids[j]));
      }

      auto it = handles.find(slot_name);
      if (it != handles.end())
      {
        it->second->ReleaseAll();
        for (HandleID tid : tids)
        {
          if (tid != 0)
          {
            it->second->AddTarget(tid);
          }
        }
      }
    }
  }
}

/**
 * @brief Save object state to persistent storage driver via pure streaming.
 * If object is Clean or Dehydrated, skips Save (O(1)).
 */
template <typename T>
bool Save(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Save Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  T *obj = ptr.operator->();
  if (!obj)
  {
    throw std::runtime_error("OuroKore Save Error: Cannot access payload.");
  }

  StorageState state = obj->GetStorageState();
  if (state == StorageState::Clean || state == StorageState::Dehydrated)
  {
    return true;  // Fast skip!
  }

  auto driver = GetStorageDriver();
  if (!driver)
  {
    throw std::runtime_error(
        "OuroKore Save Error: Storage driver not initialized. Call ork::Init(driver) first."
    );
  }

  auto stream = driver->CreateWriteStream(id);
  if (!stream)
  {
    throw std::runtime_error("OuroKore Save Error: Failed to create write stream from storage driver.");
  }

  {
    OuroReadLock lock(*obj);
    PackBlueprint(*obj, *stream);
  }

  obj->SetStorageState(StorageState::Clean);
  return true;
}

/**
 * @brief Load (revert/refresh) object state from persistent storage driver into an existing living object via pure streaming.
 */
template <typename T>
bool Load(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Load Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  T *obj = ptr.operator->();
  if (!obj)
  {
    throw std::runtime_error("OuroKore Load Error: Cannot access payload.");
  }

  auto driver = GetStorageDriver();
  if (!driver)
  {
    throw std::runtime_error(
        "OuroKore Load Error: Storage driver not initialized. Call ork::Init(driver) first."
    );
  }

  auto stream = driver->OpenReadStream(id);
  if (!stream)
  {
    return false;
  }

  {
    OuroWriteLock lock(*obj);
    UnpackBlueprint(*obj, *stream);
  }

  obj->SetStorageState(StorageState::Clean);
  return true;
}

/**
 * @brief 透過 HandleID 直接脫水物件（核心唯一標準脫水實作）
 *
 * 100% In-Flight 記憶體安全保證：
 * 核心永遠只檢查單一條件：root_count 必須為 0！
 * 若 root_count > 0（代表有任何執行緒或作用域正持有 OuroPtr 活躍執行中），核心一律安全略過並傳回 false。
 *
 * @param id 目標物件全域唯一 HandleID
 * @return 成功脫水傳回 true；若物件正忙 (In-Flight)、已脫水或不存在則安全略過並傳回 false。
 */
inline bool Dehydrate(HandleID id)
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
  }

  // 4. 標記為 Dehydrated 並釋放 Payload 肉體記憶體
  ork_set_storage_state(id, static_cast<uint8_t>(StorageState::Dehydrated));
  delete obj;
  ork_bind_object_payload(id, nullptr);

  // 5. 通知自動脫水模組物件已脫水
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->OnObjectDehydrated(id);
  }
  return true;
}

/**
 * @brief 透過右值移動（Move）對物件發起脫水（所有權消耗語意）
 *
 * 【重要使用規範】
 * 本函式採用右值消耗語意（Rvalue Consume）。呼叫此函式後，傳入的原 OuroPtr 物件
 * 將會被立即釋放並重置（HandleID 歸零），「使用過後原本的物件指標將不可以再被使用」！
 * 若日後需要再次存取該物件，請使用 Rehydrate<T>(id) 重新取得全新的 OuroPtr。
 *
 * @param ptr 要脫水的目標 OuroPtr（傳入後所有權將被轉移並清空）
 * @return 成功脫水傳回 true；若其他執行緒同時持有該物件 (root_count > 1) 則傳回 false。
 */
template <typename T>
inline bool Dehydrate(OuroPtr<T> &&ptr)
{
  if (!ptr)
  {
    return false;
  }

  HandleID id = ptr.GetTargetID();

  // 1. 檢查是否有其他活躍 OuroPtr (root_count > 1)
  uint32_t root_count = 0;
  if (ork_get_root_edge_count(id, &root_count) != ORK_STATUS_OK || root_count > 1)
  {
    return false;  // 有其他並行執行緒正在使用中，安全略過
  }

  // 2. 在 ptr 依然提供強引用保護的情況下，先完成存檔與脫水標記
  {
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

    if (ork_get_root_edge_count(id, &root_count) != ORK_STATUS_OK || root_count > 1)
    {
      return false;
    }

    uint8_t state_val = 0;
    if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
        static_cast<StorageState>(state_val) == StorageState::Dehydrated)
    {
      ptr.Release();
      return true;
    }

    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(id, &raw_obj) != ORK_STATUS_OK || !raw_obj)
    {
      return false;
    }

    OuroObject *obj = reinterpret_cast<OuroObject *>(raw_obj);

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
    }

    ork_set_storage_state(id, static_cast<uint8_t>(StorageState::Dehydrated));
    delete obj;
    ork_bind_object_payload(id, nullptr);

    // 通知自動脫水模組物件已脫水
    if (auto dehydrator = GetAutoDehydrator())
    {
      dehydrator->OnObjectDehydrated(id);
    }
  }

  // 3. 【最後一步】此時狀態已是 Dehydrated 墓碑，才安全釋放呼叫者的 ptr
  ptr.Release();
  return true;
}

/**
 * @brief 向後相容別名
 */
inline bool DehydrateByID(HandleID id)
{
  return Dehydrate(id);
}

namespace detail
{
template <typename T>
inline void RehydratePayload(HandleID id)
{
  // 1. RAII Exclusive Lock on ControlBlock to guarantee thread-safe serialization for concurrent Rehydrate/Dehydrate calls
  struct RehydrateExclusiveLock
  {
    HandleID m_id;
    explicit RehydrateExclusiveLock(HandleID target_id) : m_id(target_id)
    {
      ork_lock_object(m_id);
    }
    ~RehydrateExclusiveLock()
    {
      ork_unlock_object(m_id);
    }
  } lock_guard(id);

  // 2. Double-Checked Locking: check if object payload was restored while waiting for the lock
  uint8_t state_val = 0;
  if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
      static_cast<StorageState>(state_val) != StorageState::Dehydrated)
  {
    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(id, &raw_obj) == ORK_STATUS_OK && raw_obj)
    {
      return;  // Object payload is already loaded
    }
  }

  auto driver = GetStorageDriver();
  if (!driver)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Storage driver not initialized.");
  }

  auto stream = driver->OpenReadStream(id);
  if (!stream)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Blueprint stream not found in storage driver.");
  }

  // 3. Set ActiveOwnerGuard so child handles constructed in T() inherit this object's ID as owner
  ActiveOwnerGuard guard(id);

  // 記憶體配置與 OOM 緊急脫水自救重試機制
  void *mem = nullptr;
  constexpr int MAX_OOM_RETRIES = 2;
  for (int attempt = 0; attempt <= MAX_OOM_RETRIES; ++attempt)
  {
    try
    {
      mem = ::operator new(sizeof(T));
      break;
    }
    catch (const std::bad_alloc &)
    {
      auto dehydrator = GetAutoDehydrator();
      size_t freed_count = dehydrator ? dehydrator->TriggerDehydration() : 0;
      if (freed_count == 0 || attempt == MAX_OOM_RETRIES)
      {
        throw;
      }
    }
  }

  T *empty_shell = nullptr;
  try
  {
    empty_shell = ::new (mem) T();
    detail::PopActiveObject();
  }
  catch (...)
  {
    detail::PopActiveObject();
    ::operator delete(mem);
    throw;
  }

  // RAII guard to prevent memory leak if UnpackBlueprint throws exception
  std::unique_ptr<T> shell_guard(empty_shell);
  shell_guard->SetObjectID(id);

  // 4. Unpack Payload & Edge Roster (Exceptions safely bubble up while shell_guard frees memory)
  UnpackBlueprint(*shell_guard, *stream);

  // 5. Re-bind payload pointer to existing ControlBlock in Registry
  if (ork_bind_object_payload(id, reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(shell_guard.get()))) !=
      ORK_STATUS_OK)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Failed to re-bind payload pointer to ControlBlock.");
  }

  T *released_obj = shell_guard.release();
  released_obj->SetStorageState(StorageState::Clean);

  // Re-register type-specific auto-rehydration callback
  ork_set_rehydrate_fn(id, &RehydrateCallback<T>);

  // 6. 通知自動脫水模組物件已復水
  if (auto dehydrator = GetAutoDehydrator())
  {
    dehydrator->OnObjectRehydrated(id);
  }
}
}  // namespace detail

/**
 * @brief Rehydrate a dehydrated object: allocate new empty shell T(), stream load blueprint, and rebind payload to
 * ControlBlock. Target HandleID & child connection handles remain 100% stable!
 */
template <typename T>
OuroPtr<T> Rehydrate(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Invalid HandleID.");
  }
  detail::RehydratePayload<T>(id);
  return OuroPtr<T>(id);
}

template <typename T>
OuroPtr<T> Rehydrate(const OuroPtr<T> &ptr)
{
  return Rehydrate<T>(ptr.GetTargetID());
}

/**
 * @brief 型別專屬的自動復水回呼函式（Automatic Rehydration Callback）
 *
 * 當受管物件處於脫水狀態（Dehydrated）且底層嘗試存取其指標時（如 ork_acquire_object_pointer），
 * ControlBlock 會透過預先註冊的函式指標觸發此回呼，自動完成物件之記憶體重建、狀態還原（RehydratePayload）
 * 與指標重新綁定，實現對呼叫端透明的延遲復水（Transparent On-Demand Rehydration）。
 *
 * @tparam T 物件型別
 * @param id 物件的 HandleID
 * @return ::OuroObject* 復水後重建的底層物件指標
 */
template <typename T>
inline ::OuroObject *RehydrateCallback(HandleID id)
{
  detail::RehydratePayload<T>(id);
  ::OuroObject *raw_obj = nullptr;
  ork_acquire_object_pointer(id, &raw_obj);
  return raw_obj;
}

// =========================================================================
// --- 核心非同步 I/O 操作 (Async I/O with Context Preservation) ---
// =========================================================================

/**
 * @brief 非同步儲存物件狀態至儲存體
 *
 * 內部透過在任務中建立獨立的 OuroPtr<T> 生命週期守衛，保證在背景寫盤落盤前物件絕不被提前銷毀。
 * @param ptr 目標物件指標
 * @return std::future<AsyncResult<T>> 自帶 HandleID、成功狀態與物件指標的 Future 結果
 */
template <typename T>
inline std::future<AsyncResult<T>> SaveAsync(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore SaveAsync Error: Invalid or null OuroPtr.");
  }

  auto pool = detail::GetCoreThreadPoolRef();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore SaveAsync Error: Core ThreadPool not initialized or stopped.");
  }

  HandleID id = ptr.GetTargetID();
  OuroPtr<T> guard(id);

  return pool->submit([guard = std::move(guard)]() mutable -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = guard.GetTargetID();
    try
    {
      if (Save(guard))
      {
        result.success = true;
        result.ptr = std::move(guard);
      }
      else
      {
        result.success = false;
        result.error = "Storage driver save operation returned false.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during SaveAsync.";
    }
    return result;
  });
}

/**
 * @brief 非同步從儲存體載入 / 刷新物件狀態（以寫鎖反序列化覆蓋舊狀態）
 */
template <typename T>
inline std::future<AsyncResult<T>> LoadAsync(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore LoadAsync Error: Invalid or null OuroPtr.");
  }

  auto pool = detail::GetCoreThreadPoolRef();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore LoadAsync Error: Core ThreadPool not initialized or stopped.");
  }

  HandleID id = ptr.GetTargetID();
  OuroPtr<T> guard(id);

  return pool->submit([guard = std::move(guard)]() mutable -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = guard.GetTargetID();
    try
    {
      if (Load(guard))
      {
        result.success = true;
        result.ptr = std::move(guard);
      }
      else
      {
        result.success = false;
        result.error = "Storage driver load operation returned false.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during LoadAsync.";
    }
    return result;
  });
}

/**
 * @brief 非同步復水：背景配置空殼、讀檔反序列化並重新綁定 Payload
 * @return std::future<AsyncResult<T>> 包含 HandleID、成功狀態與復水後之全新 OuroPtr<T>
 */
template <typename T>
inline std::future<AsyncResult<T>> RehydrateAsync(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore RehydrateAsync Error: Invalid HandleID.");
  }

  auto pool = detail::GetCoreThreadPoolRef();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore RehydrateAsync Error: Core ThreadPool not initialized or stopped.");
  }

  return pool->submit([id]() -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = id;
    try
    {
      result.ptr = Rehydrate<T>(id);
      result.success = (result.ptr.GetTargetID() != 0);
      if (!result.success)
      {
        result.error = "Rehydrate failed to produce a valid OuroPtr.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during RehydrateAsync.";
    }
    return result;
  });
}

/**
 * @brief 依 HandleID 發起非同步脫水
 */
inline std::future<AsyncResult<void>> DehydrateAsync(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Invalid HandleID.");
  }

  auto pool = detail::GetCoreThreadPoolRef();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Core ThreadPool not initialized or stopped.");
  }

  return pool->submit([id]() -> AsyncResult<void> {
    AsyncResult<void> result;
    result.id = id;
    try
    {
      result.success = Dehydrate(id);
      if (!result.success)
      {
        result.error = "Dehydrate returned false (object might be in-flight, missing, or already dehydrated).";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during DehydrateAsync.";
    }
    return result;
  });
}

/**
 * @brief 透過右值移動 OuroPtr 發起非同步脫水（右值所有權轉移）
 */
template <typename T>
inline std::future<AsyncResult<void>> DehydrateAsync(OuroPtr<T> &&ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Invalid or null OuroPtr.");
  }

  auto pool = detail::GetCoreThreadPoolRef();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Core ThreadPool not initialized or stopped.");
  }

  HandleID id = ptr.GetTargetID();
  return pool->submit([target_ptr = std::move(ptr), id]() mutable -> AsyncResult<void> {
    AsyncResult<void> result;
    result.id = id;
    try
    {
      result.success = Dehydrate(std::move(target_ptr));
      if (!result.success)
      {
        result.error = "Dehydrate with move-ptr failed (other in-flight references may exist).";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during DehydrateAsync.";
    }
    return result;
  });
}

// =========================================================================
// --- 多核心批次並行操作 (Parallel Batch APIs) ---
// =========================================================================

/**
 * @brief 批次多核心平行儲存
 * @param batch 要儲存的物件集合
 * @return 依序回傳各物件之 AsyncResult<T> 結果向量
 */
template <typename T>
inline std::vector<AsyncResult<T>> SaveBatch(const std::vector<OuroPtr<T>> &batch)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(batch.size());
  for (const auto &item : batch)
  {
    futures.push_back(SaveAsync(item));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行載入
 */
template <typename T>
inline std::vector<AsyncResult<T>> LoadBatch(const std::vector<OuroPtr<T>> &batch)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(batch.size());
  for (const auto &item : batch)
  {
    futures.push_back(LoadAsync(item));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行復水
 */
template <typename T>
inline std::vector<AsyncResult<T>> RehydrateBatch(const std::vector<HandleID> &ids)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(ids.size());
  for (HandleID id : ids)
  {
    futures.push_back(RehydrateAsync<T>(id));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行脫水
 */
inline std::vector<AsyncResult<void>> DehydrateBatch(const std::vector<HandleID> &ids)
{
  std::vector<std::future<AsyncResult<void>>> futures;
  futures.reserve(ids.size());
  for (HandleID id : ids)
  {
    futures.push_back(DehydrateAsync(id));
  }

  std::vector<AsyncResult<void>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

}  // namespace ork
