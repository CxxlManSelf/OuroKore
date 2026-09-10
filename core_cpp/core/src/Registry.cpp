#include "Registry.h"

#include <algorithm>

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
    m_object_map.erase(it);
    delete cb;
    if (m_object_destroyed_cb)
    {
      m_object_destroyed_cb(id);
    }
    return true;
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

  {
    std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
    cb->m_owners.push_back(owner_id);
  }

  cb->m_strong_count.fetch_add(1);
  return true;
}

bool Registry::UnregisterEdge(HandleID owner_id, HandleID target_id)
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
  {
    std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
    auto it = std::find(cb->m_owners.begin(), cb->m_owners.end(), owner_id);
    if (it != cb->m_owners.end())
    {
      cb->m_owners.erase(it);
    }
  }

  // 2. Decrement strong reference count
  uint32_t prev_strong = cb->m_strong_count.fetch_sub(1);
  if (prev_strong == 1)
  {
    // Strong count transitioned to 0: free payload
    std::unique_lock<std::shared_mutex> payload_lock(cb->m_rw_lock);
    if (cb->m_payload)
    {
      delete cb->m_payload;
      cb->m_payload = nullptr;
    }
  }

  // 3. Clean up the ControlBlock if completely dead (strong == 0 && weak == 0)
  if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
  {
    std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
    // Double-check under exclusive registry lock
    if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
    {
      auto it = m_object_map.find(target_id);
      if (it != m_object_map.end())
      {
        m_object_map.erase(it);
        delete cb;
        if (m_object_destroyed_cb)
        {
          m_object_destroyed_cb(target_id);
        }
      }
    }
  }
  return true;
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

  cb->m_weak_count.fetch_sub(1);

  if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
  {
    std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
    if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
    {
      auto it = m_object_map.find(target_id);
      if (it != m_object_map.end())
      {
        m_object_map.erase(it);
        delete cb;
        if (m_object_destroyed_cb)
        {
          m_object_destroyed_cb(target_id);
        }
      }
    }
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

  bool alive = (cb->m_strong_count > 0);
  if (!alive && perform_pruning)
  {
    cb->m_weak_count.fetch_sub(1);
    if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
    {
      std::unique_lock<std::shared_mutex> lock(m_registry_mutex);
      if (cb->m_strong_count == 0 && cb->m_weak_count == 0)
      {
        auto it = m_object_map.find(target_id);
        if (it != m_object_map.end())
        {
          m_object_map.erase(it);
          delete cb;
          if (m_object_destroyed_cb)
          {
            m_object_destroyed_cb(target_id);
          }
        }
      }
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

  // Atomic Increment If Non-Zero (Lock-free CAS Loop)
  uint32_t count = cb->m_strong_count.load(std::memory_order_relaxed);
  while (count > 0)
  {
    if (cb->m_strong_count.compare_exchange_weak(count, count + 1,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_relaxed))
    {
      std::lock_guard<std::mutex> owners_lock(cb->m_owners_mutex);
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
