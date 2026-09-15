#include "Registry.h"

#include <algorithm>

#include "ControlBlock.h"
#include "CycleCollector.h"
#include "DeferredDeleteQueue.h"
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

HandleID Registry::RegisterObject(OuroObject *obj)
{
  if (!obj) return 0;

  std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
  HandleID id = GenerateUniqueID();
  while (m_object_map.find(id) != m_object_map.end())
  {
    id = GenerateUniqueID();
  }

  obj->SetObjectID(id);
  ControlBlock *cb = new ControlBlock(obj);
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

bool Registry::BindPayload(HandleID id, OuroObject *obj)
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
    if (cb->m_payload)
    {
      delete cb->m_payload;
      cb->m_payload = nullptr;
    }
    return TryDestroyControlBlockLocked(id, cb);
  }
  return false;
}

bool Registry::RegisterEdge(HandleID owner_id, HandleID target_id)
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
  {
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return false;
    }
    cb = it->second;
  }

  // 1. Remove the edge from owner list
  bool edge_found = false;
  {
    std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
    auto it = std::find(cb->m_owners.begin(), cb->m_owners.end(), owner_id);
    if (it != cb->m_owners.end())
    {
      cb->m_owners.erase(it);
      edge_found = true;
    }
  }

  if (!edge_found)
  {
    return false;
  }

  // 2. Decrement strong reference count
  uint32_t prev_strong = cb->m_strong_count.fetch_sub(1);
  if (prev_strong == 1)
  {
    // Strong count transitioned to 0: 移交 DeferredDeleteQueue 背景多執行緒平行銷毀
    // 徹底消滅遞迴解構造成的呼叫堆疊溢位 (Stack Overflow)
    DeferredDeleteQueue::GetInstance().Push(target_id);
  }
  else if (prev_strong > 1 && !silent)
  {
    // 扣減後 StrongCount 仍大於 0：可能構成自娛自樂的閉環孤島
    // 透過原子 CAS 去重旗標，成功搶入者推入 CycleCollector 嫌疑犯佇列
    bool expected = false;
    if (cb->m_in_suspect_queue.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
      CycleCollector::GetInstance().PushSuspect(target_id);
    }
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
      if (m_object_destroyed_cb)
      {
        m_object_destroyed_cb(id);
      }
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

  // 若目標物件已被判定為循環孤島或正處於拆解態，禁止新增弱引用
  if (cb->m_is_destructing.load(std::memory_order_acquire))
  {
    return false;
  }

  cb->m_weak_count.fetch_add(1);
  return true;
}

bool Registry::UnregisterWeak(HandleID target_id)
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

  uint32_t prev_weak = cb->m_weak_count.fetch_sub(1, std::memory_order_acq_rel);

  // 僅當弱計數剛好歸零，且強計數亦為零時，嘗試獲取寫鎖收割 ControlBlock
  if (prev_weak == 1 && cb->m_strong_count.load(std::memory_order_acquire) == 0)
  {
    std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
    TryDestroyControlBlockLocked(target_id, cb);
  }
  return true;
}

bool Registry::CheckAlive(HandleID target_id, bool perform_pruning)
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

  bool is_destructing = cb->m_is_destructing.load(std::memory_order_acquire);
  bool alive = (cb->m_strong_count.load(std::memory_order_acquire) > 0) && !is_destructing;
  if (!alive && perform_pruning)
  {
    uint32_t prev_weak = cb->m_weak_count.fetch_sub(1, std::memory_order_acq_rel);
    if (prev_weak == 1 && cb->m_strong_count.load(std::memory_order_acquire) == 0)
    {
      std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
      TryDestroyControlBlockLocked(target_id, cb);
    }
  }
  return alive;
}

bool Registry::TryLockWeak(HandleID target_id)
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
  cb->m_rw_lock.lock();
  return true;
}

bool Registry::UnlockObject(HandleID target_id)
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
  cb->m_rw_lock.unlock();
  return true;
}

bool Registry::LockObjectShared(HandleID target_id)
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
  cb->m_rw_lock.lock_shared();
  return true;
}

bool Registry::UnlockObjectShared(HandleID target_id)
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
  cb->m_rw_lock.unlock_shared();
  return true;
}

OuroObject *Registry::AcquireObjectPointer(HandleID target_id)
{
  ControlBlock *cb = nullptr;
  {
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return nullptr;
    }
    cb = it->second;
  }
  // Return null if object is dead (strong count == 0)
  if (cb->m_strong_count == 0)
  {
    return nullptr;
  }

  // Automatic Rehydration Check:
  // If payload is null or state is Dehydrated, invoke registered rehydrate callback
  if (cb->m_payload == nullptr ||
      cb->m_storage_state.load(std::memory_order_acquire) == static_cast<uint8_t>(StorageState::Dehydrated))
  {
    if (cb->m_rehydrate_fn != nullptr)
    {
      cb->m_rehydrate_fn(target_id);
    }
  }

  return cb->m_payload;
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

uint32_t Registry::GetRootEdgeCount(HandleID target_id) const
{
  ControlBlock *cb = nullptr;
  {
    std::shared_lock<std::shared_mutex> lock(m_registry_mutex);
    auto it = m_object_map.find(target_id);
    if (it == m_object_map.end())
    {
      return 0;
    }
    cb = it->second;
  }
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

}  // namespace ork
