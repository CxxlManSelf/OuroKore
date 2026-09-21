#include "Registry.h"

#include <algorithm>

#include "ControlBlock.h"
#include "CycleCollector.h"
#include "DeferredDeleteQueue.h"
#include "RuntimeContext.h"
#include "ourokore/c_api/core.h"
#include "ourokore/component/OuroObject.hpp"

namespace ork
{

static thread_local HandleID g_active_owner_id = ORK_ROOT_ID;

Registry::Registry()
{
  std::random_device rd;
  m_rng.seed(rd());
}

Registry::~Registry()
{
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  for (auto &pair : m_object_map)
  {
    delete pair.second;
  }
  m_object_map.clear();
}

Registry &Registry::GetInstance()
{
  static Registry instance;
  return instance;
}

HandleID Registry::GenerateUniqueID()
{
  std::lock_guard<std::mutex> lock(m_rng_mutex);
  HandleID id = 0;
  while (id == 0)
  {
    id = m_rng();
  }
  return id;
}

HandleID Registry::RegisterObject(OuroObject *obj, DestroyFn destroy_fn)
{
  if (!obj) return 0;

  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  HandleID id = GenerateUniqueID();
  while (m_object_map.find(id) != m_object_map.end())
  {
    id = GenerateUniqueID();
  }

  obj->SetObjectID(id);
  ControlBlock *cb = new ControlBlock(obj, destroy_fn);
  m_object_map[id] = cb;
  return id;
}

HandleID Registry::ReserveID()
{
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  HandleID id = GenerateUniqueID();
  while (m_object_map.find(id) != m_object_map.end())
  {
    id = GenerateUniqueID();
  }

  ControlBlock *cb = new ControlBlock(nullptr);
  m_object_map[id] = cb;
  return id;
}

bool Registry::BindPayload(HandleID id, OuroObject *obj, DestroyFn destroy_fn)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(id);
  if (it != m_object_map.end())
  {
    ControlBlock *cb = it->second;
    if (obj)
    {
      obj->SetObjectID(id);
    }
    cb->m_payload = obj;
    if (!obj)
    {
      cb->m_destroy_fn = nullptr;
    }
    else if (destroy_fn)
    {
      cb->m_destroy_fn = destroy_fn;
    }
    return true;
  }
  return false;
}

bool Registry::UnregisterObject(HandleID id)
{
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(id);
  if (it != m_object_map.end())
  {
    ControlBlock *cb = it->second;
    // 取消預留 (Rollback Reservation)：若已有 payload 實體先行釋放
    cb->DeletePayload();
    bool expected = false;
    bool should_notify = cb->m_destruction_notified.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    bool destroyed = TryDestroyControlBlockLocked(id, cb);
    lock.unlock();

    if (should_notify)
    {
      NotifyObjectDestroyed(id);
    }
    return destroyed;
  }
  return false;
}

bool Registry::RegisterEdge(HandleID owner_id, HandleID target_id)
{
  // 全程持有 shared_lock 保證 target_id 對應之 ControlBlock 絕對不會在加邊期間被並發銷毀
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;

  // 孤島拆解中防線：若目標物件處於拆解態，拒絕加邊
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }

  {
    std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
    // 再次在名冊鎖保護下確認拆解狀態
    if (cb->m_is_destructing.load(std::memory_order_acquire))
    {
      return false;
    }
    cb->m_owners.push_back(owner_id);
  }

  cb->m_strong_count.fetch_add(1);
  return true;
}

bool Registry::UnregisterEdge(HandleID owner_id, HandleID target_id, bool silent)
{
  ControlBlock *cb = nullptr;
  uint32_t prev_strong = 0;
  bool should_suspect = false;
  {
    // 在 shared_lock 保護下完成邊緣移除、強計數原子扣減與嫌疑犯標記
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return false;
    }
    cb = it->second;

    // 1. Remove the edge from owner list
    bool edge_found = false;
    {
      std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
      auto edge_it = std::find(cb->m_owners.begin(), cb->m_owners.end(), owner_id);
      if (edge_it != cb->m_owners.end())
      {
        cb->m_owners.erase(edge_it);
        edge_found = true;
      }
    }

    if (!edge_found)
    {
      return false;
    }

    // 2. Decrement strong reference count
    prev_strong = cb->m_strong_count.fetch_sub(1);
    if (prev_strong > 1 && !silent)
    {
      // 扣減後 StrongCount 仍大於 0：可能構成自娛自樂的閉環孤島
      // 透過原子 CAS 去重旗標，成功搶入者標記入隊
      bool expected = false;
      if (cb->m_in_suspect_queue.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      {
        should_suspect = true;
      }
    }
  }

  if (prev_strong == 1)
  {
    // Strong count transitioned to 0: 移交 DeferredDeleteQueue 背景多執行緒平行銷毀
    // 徹底消滅遞迴解構造成的呼叫堆疊溢位 (Stack Overflow)
    DeferredDeleteQueue::GetInstance().Push(target_id);
  }
  else if (should_suspect)
  {
    CycleCollector::GetInstance().PushSuspect(target_id);
  }

  return true;
}

ControlBlock *Registry::GetControlBlock(HandleID target_id) const
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    return it->second;
  }
  return nullptr;
}

bool Registry::TryDestroyControlBlockLocked(HandleID id, ControlBlock *cb)
{
  if (!cb)
  {
    return false;
  }

  // 核心銷毀天條：
  // 1. 強引用歸零 (m_strong_count == 0)
  // 2. 弱引用歸零 (m_weak_count == 0)
  // 3. 肉體實體已被物理銷毀置空 (m_payload == nullptr)
  // 此三者同時滿足，方可安全將 ControlBlock 抹除並 delete，防止與 DeferredDeleteQueue 搶跑引發 UAF
  if (cb->m_strong_count.load(std::memory_order_acquire) == 0 &&
      cb->m_weak_count.load(std::memory_order_acquire) == 0 &&
      cb->m_payload == nullptr)
  {
    auto it = m_object_map.find(id);
    if (it != m_object_map.end() && it->second == cb)
    {
      m_object_map.erase(it);
      delete cb;
      return true;
    }
  }
  return false;
}

bool Registry::DestroyControlBlockIfDead(HandleID target_id)
{
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  return TryDestroyControlBlockLocked(target_id, it->second);
}

bool Registry::RegisterWeak(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;

  // 在讀鎖保護下檢查拆解態並遞增弱引用，防止 cb 逃逸被並發刪除
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }

  cb->m_weak_count.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool Registry::UnregisterWeak(HandleID target_id)
{
  // 持有獨佔寫鎖進行扣減與銷毀收割，保證 cb 存取期間絕對無任何線程並發 delete
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;

  cb->m_weak_count.fetch_sub(1, std::memory_order_acq_rel);
  TryDestroyControlBlockLocked(target_id, cb);
  return true;
}

bool Registry::CheckAlive(HandleID target_id, bool perform_pruning)
{
  if (!perform_pruning)
  {
    // 唯讀查詢路徑：持有 shared_lock 保證 cb 絕對不被銷毀，並發零阻塞
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return false;
    }
    ControlBlock *cb = it->second;
    bool is_destructing = cb->m_is_destructing.load(std::memory_order_acquire);
    return (cb->m_strong_count.load(std::memory_order_acquire) > 0) && !is_destructing;
  }

  // 剪枝修剪路徑：持有 unique_lock，安全扣減並收割 ControlBlock，徹底杜絕鎖外逃逸與 UAF
  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;

  bool is_destructing = cb->m_is_destructing.load(std::memory_order_acquire);
  bool alive = (cb->m_strong_count.load(std::memory_order_acquire) > 0) && !is_destructing;
  if (!alive)
  {
    cb->m_weak_count.fetch_sub(1, std::memory_order_acq_rel);
    TryDestroyControlBlockLocked(target_id, cb);
  }
  return alive;
}

bool Registry::TryLockWeak(HandleID target_id)
{
  // 持有 shared_lock 全程守護 cb 生命週期，嚴禁 cb 在 CAS 期間被背後銷毀
  std::shared_lock<std::shared_mutex> registry_lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;

  // 1. 若物件已被標記為正在拆解中，嚴禁弱引用晉升（防止死者甦醒與 UAF）
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }

  // 2. Atomic Increment If Non-Zero (Lock-free CAS Loop)
  uint32_t count = cb->m_strong_count.load(std::memory_order_relaxed);
  while (count > 0)
  {
    // CAS 過程中若偵測到拆解旗標，立即退出
    if (cb->m_is_destructing.load(std::memory_order_acquire))
    {
      return false;
    }

    if (cb->m_strong_count.compare_exchange_weak(count, count + 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed))
    {
      std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
      // 在名冊鎖保護下進行二次確認
      if (cb->m_is_destructing.load(std::memory_order_acquire))
      {
        // 遭遇併發拆解搶跑，回滾強計數
        uint32_t prev = cb->m_strong_count.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1)
        {
          // 關鍵防禦：若併發的 UnregisterEdge 因我們先前的 CAS +1 而誤判未歸零且未入隊，
          // 此處回滾後 StrongCount 剛好歸零，必須由本執行緒補交 DeferredDeleteQueue 銷毀，徹底杜絕記憶體洩漏！
          DeferredDeleteQueue::GetInstance().Push(target_id);
        }
        return false;
      }
      cb->m_owners.push_back(ORK_ROOT_ID);
      return true;
    }
  }

  return false;
}

bool Registry::LockObject(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }
  cb->m_rw_lock.lock();
  return true;
}

bool Registry::UnlockObject(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  it->second->m_rw_lock.unlock();
  return true;
}

bool Registry::LockObjectShared(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  ControlBlock *cb = it->second;
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }
  cb->m_rw_lock.lock_shared();
  return true;
}

bool Registry::UnlockObjectShared(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return false;
  }
  it->second->m_rw_lock.unlock_shared();
  return true;
}

OuroObject *Registry::AcquireObjectPointer(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return nullptr;
  }
  ControlBlock *cb = it->second;

  // 1. 在 shared_lock 保護下確認物件存活態與非拆解態
  if (cb->m_strong_count.load(std::memory_order_acquire) == 0 ||
      cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return nullptr;
  }

  // 2. 熱路徑（常態記憶體常駐物件）：全程在 shared_lock 保護下直接回傳，杜絕鎖外逃逸與 UAF
  if (cb->m_payload != nullptr &&
      cb->m_storage_state.load(std::memory_order_acquire) != static_cast<uint8_t>(StorageState::Dehydrated))
  {
    return cb->m_payload;
  }

  // 3. 冷路徑（自動復水）：在讀鎖內安全拷貝復水回呼指標，解鎖後執行以徹底防止遞迴重入死鎖
  RehydrateFn rehydrate_fn = cb->m_rehydrate_fn;
  lock.unlock();

  if (rehydrate_fn != nullptr)
  {
    rehydrate_fn(target_id);
  }

  // 4. 復水完成後，重新在 shared_lock 守護下獲取剛重綁的 payload 指標並安全回傳
  std::shared_lock<std::shared_mutex> recheck_lock(m_registry_mutex);
  it = m_object_map.find(target_id);
  if (it != m_object_map.end() && it->second->m_strong_count.load(std::memory_order_acquire) > 0)
  {
    return it->second->m_payload;
  }
  return nullptr;
}

uint8_t Registry::GetStorageState(HandleID target_id) const
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    return it->second->m_storage_state.load(std::memory_order_relaxed);
  }
  return 0;  // Default UnsavedNew
}

bool Registry::SetStorageState(HandleID target_id, uint8_t state)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    it->second->m_storage_state.store(state, std::memory_order_relaxed);
    return true;
  }
  return false;
}

bool Registry::MarkDirty(HandleID target_id)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    uint8_t expected = 1;  // Clean (1)
    it->second->m_storage_state.compare_exchange_strong(expected, 2);  // Dirty (2)
    return true;
  }
  return false;
}

bool Registry::SetRehydrateFn(HandleID target_id, RehydrateFn fn)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    it->second->m_rehydrate_fn = fn;
    return true;
  }
  return false;
}

bool Registry::SetDestroyFn(HandleID target_id, DestroyFn fn)
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it != m_object_map.end())
  {
    it->second->m_destroy_fn = fn;
    return true;
  }
  return false;
}

bool Registry::DestroyPayload(HandleID target_id)
{
  ControlBlock *cb = nullptr;
  {
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return false;
    }
    cb = it->second;
  }
  if (cb)
  {
    cb->DeletePayload();
    return true;
  }
  return false;
}

uint32_t Registry::GetRootEdgeCount(HandleID target_id) const
{
  std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
  auto it = m_object_map.find(target_id);
  if (it == m_object_map.end())
  {
    return 0;
  }
  ControlBlock *cb = it->second;

  std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
  uint32_t count = 0;
  for (HandleID owner : cb->m_owners)
  {
    if (owner == ORK_ROOT_ID)
    {
      ++count;
    }
  }
  return count;
}

void Registry::SetActiveOwner(HandleID owner_id)
{
  g_active_owner_id = owner_id;
}

HandleID Registry::GetActiveOwner() const
{
  return g_active_owner_id;
}

void Registry::NotifyObjectDestroyed(HandleID id)
{
  // 1. 底層原生連鎖反應：通知 RuntimeContext 清理藍圖與註銷脫水名冊
  RuntimeContext::GetInstance().OnObjectDestroyed(id);

  // 2. 外部自定義回呼（原子快照讀取，杜絕 TOCTOU 空指標呼叫與 Data Race）
  auto cb = m_object_destroyed_cb.load(std::memory_order_acquire);
  if (cb)
  {
    cb(id);
  }
}

void Registry::Clear()
{
  std::unordered_map<HandleID, ControlBlock *> to_cleanup;
  {
    std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
    // 1. 在獨佔鎖內極速換出整張名冊，並清空銷毀回呼
    to_cleanup.swap(m_object_map);
    m_object_destroyed_cb.store(nullptr, std::memory_order_release);
  }

  // 2. 第一階段：在無鎖狀態下，先釋放所有殘留的 payload 實體
  // 即使 payload 解構引發子物件呼叫 UnregisterEdge，因 m_registry_mutex 已解鎖，絕不死鎖
  for (auto &pair : to_cleanup)
  {
    ControlBlock *cb = pair.second;
    if (cb)
    {
      cb->DeletePayload();
    }
  }

  // 3. 第二階段：在無鎖狀態下，徹底釋放所有 ControlBlock 墓碑
  for (auto &pair : to_cleanup)
  {
    delete pair.second;
  }

  // 4. 清理持久化映射名冊
  {
    std::unique_lock<std::shared_mutex> p_lock(m_persistent_mutex);
    m_persistent_map.clear();
  }
}

}  // namespace ork
