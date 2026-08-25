#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Handles.hpp"
#include "IStorageDriver.hpp"
#include "OuroObject.hpp"
#include "OuroStream.hpp"
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
}  // namespace detail

/**
 * @brief Initialize OuroKore Core with a persistent storage driver instance (Dependency Injection).
 */
inline void Init(std::shared_ptr<IStorageDriver> driver)
{
  detail::GetStorageDriverRef() = std::move(driver);
}

/**
 * @brief Shutdown OuroKore Core and release storage driver reference.
 */
inline void Shutdown()
{
  detail::GetStorageDriverRef().reset();
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
    if (handle_ptr)
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
      if (it != handles.end() && it->second)
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
 * @brief Dehydrate an object: stream Payload to storage driver and free memory payload.
 * ControlBlock tombstone & HandleID remain intact in memory!
 * Fully thread-safe: acquires Exclusive Lock on ControlBlock and guards against in-flight concurrent execution.
 *
 * @param ptr Active OuroPtr holding the target object.
 * @param force If true, bypasses in-flight root edge count check (when root count > 1).
 */
template <typename T>
void Dehydrate(const OuroPtr<T> &ptr, bool force = false)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Dehydrate Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();

  // 1. Guard against In-Flight execution:
  // The passed `ptr` itself contributes 1 to root edge count.
  // If root_count > 1, other active OuroPtr instances exist in stack/threads.
  uint32_t root_count = 0;
  if (ork_get_root_edge_count(id, &root_count) == ORK_STATUS_OK && root_count > 1 && !force)
  {
    throw std::runtime_error(
        "OuroKore Dehydrate Error: Cannot dehydrate object while other active In-Flight OuroPtr instances (Root Edge "
        "Count > 1) are holding it. Pass force=true if intentional."
    );
  }

  // 2. RAII Exclusive Lock on ControlBlock to guarantee zero concurrent member function execution (Prevent UAF)
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

  uint8_t state_val = 0;
  if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
      static_cast<StorageState>(state_val) == StorageState::Dehydrated)
  {
    return;  // Already dehydrated
  }

  ::OuroObject *raw_obj = nullptr;
  if (ork_acquire_object_pointer(id, &raw_obj) != ORK_STATUS_OK || !raw_obj)
  {
    return;  // Already null or invalid
  }

  T *obj = static_cast<T *>(reinterpret_cast<OuroObject *>(raw_obj));

  // 3. Save to storage driver if UnsavedNew or Dirty (direct pack under held exclusive lock)
  StorageState current_state = obj->GetStorageState();
  if (current_state == StorageState::UnsavedNew || current_state == StorageState::Dirty)
  {
    auto driver = GetStorageDriver();
    if (!driver)
    {
      throw std::runtime_error(
          "OuroKore Dehydrate Error: Storage driver not initialized. Call ork::Init(driver) first."
      );
    }
    auto stream = driver->CreateWriteStream(id);
    if (!stream)
    {
      throw std::runtime_error("OuroKore Dehydrate Error: Failed to create write stream from storage driver.");
    }
    PackBlueprint(*obj, *stream);
  }

  // 4. Free payload memory, set payload to null, and mark ControlBlock as Dehydrated!
  delete obj;
  ork_bind_object_payload(id, nullptr);
  ork_set_storage_state(id, static_cast<uint8_t>(StorageState::Dehydrated));
}

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

  // Check if object payload already exists
  uint8_t state_val = 0;
  if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
      static_cast<StorageState>(state_val) != StorageState::Dehydrated)
  {
    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(id, &raw_obj) == ORK_STATUS_OK && raw_obj)
    {
      return OuroPtr<T>(id);  // Object payload is already loaded
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

  // 1. Set ActiveOwnerGuard so child handles constructed in T() inherit this object's ID as owner
  ActiveOwnerGuard guard(id);

  void *mem = ::operator new(sizeof(T));
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

  // 2. Unpack Payload & Edge Roster (Exceptions safely bubble up while shell_guard frees memory)
  UnpackBlueprint(*shell_guard, *stream);

  // 3. Re-bind payload pointer to existing ControlBlock in Registry
  if (ork_bind_object_payload(id, reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(shell_guard.get()))) !=
      ORK_STATUS_OK)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Failed to re-bind payload pointer to ControlBlock.");
  }

  T *released_obj = shell_guard.release();
  released_obj->SetStorageState(StorageState::Clean);

  // Re-register type-specific auto-rehydration callback
  ork_set_rehydrate_fn(id, &RehydrateCallback<T>);

  return OuroPtr<T>(id);
}

template <typename T>
OuroPtr<T> Rehydrate(const OuroPtr<T> &ptr)
{
  return Rehydrate<T>(ptr.GetTargetID());
}

template <typename T>
inline ::OuroObject *RehydrateCallback(HandleID id)
{
  auto ptr = Rehydrate<T>(id);
  ::OuroObject *raw_obj = nullptr;
  ork_acquire_object_pointer(id, &raw_obj);
  return raw_obj;
}

}  // namespace ork
