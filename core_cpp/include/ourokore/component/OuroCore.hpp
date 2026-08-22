#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Handles.hpp"
#include "OuroObject.hpp"
#include "OuroStream.hpp"
#include "StorageBackend.hpp"
#include "ourokore/c_api/component_api.h"
#include "ourokore/c_api/core.h"

namespace ork
{

namespace detail
{
inline std::shared_ptr<IStorageBackend> &GetStorageBackendRef()
{
  static std::shared_ptr<IStorageBackend> s_backend = nullptr;
  return s_backend;
}
}  // namespace detail

/**
 * @brief Initialize OuroKore Core with a persistent storage backend instance (Dependency Injection).
 */
inline void Init(std::shared_ptr<IStorageBackend> storage)
{
  detail::GetStorageBackendRef() = std::move(storage);
}

/**
 * @brief Shutdown OuroKore Core and release storage backend reference.
 */
inline void Shutdown()
{
  detail::GetStorageBackendRef().reset();
}

/**
 * @brief Get currently registered storage backend.
 */
inline std::shared_ptr<IStorageBackend> GetStorageBackend()
{
  return detail::GetStorageBackendRef();
}

/**
 * @brief Pack an OuroObject's Payload and Edge Roster into a BlueprintStream binary vector.
 * @note Core only packs Payload + Edge Roster (Slot Name -> Target HandleID).
 * Core does NOT dictate physical disk layout or magic numbers.
 */
inline std::vector<uint8_t> PackBlueprint(const OuroObject &obj)
{
  BlueprintStream stream;

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

  return stream.GetBuffer();
}

/**
 * @brief Unpack an OuroObject's Payload and Edge Roster from a binary buffer.
 */
inline void UnpackBlueprint(OuroObject &obj, const std::vector<uint8_t> &buffer)
{
  BlueprintStream stream(buffer);

  // 1. Deserialize Pure Payload
  obj.DeserializePayload(stream);

  // 2. Deserialize Edge Roster
  if (stream.GetSize() > 0)
  {
    try
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
    catch (...)
    {
      // Ignore if stream ends without edge roster
    }
  }
}

/**
 * @brief Save object state to persistent storage.
 * If object is Clean or Dehydrated, skips Save (O(1)).
 */
template <typename T>
void Save(const OuroPtr<T> &ptr)
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
    return;  // Fast skip!
  }

  auto storage = GetStorageBackend();
  if (!storage)
  {
    throw std::runtime_error(
        "OuroKore Save Error: Storage backend not initialized. Call ork::Init(storage) first."
    );
  }

  std::vector<uint8_t> data = PackBlueprint(*obj);
  storage->SaveBlueprint(id, data);
  obj->SetStorageState(StorageState::Clean);
}

/**
 * @brief Dehydrate an object: save Payload to storage backend and free memory payload.
 * ControlBlock tombstone & HandleID remain intact in memory!
 */
template <typename T>
void Dehydrate(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Dehydrate Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();

  ::OuroObject *raw_obj = nullptr;
  if (ork_acquire_object_pointer(id, &raw_obj) != ORK_STATUS_OK || !raw_obj)
  {
    return;  // Already dehydrated or null
  }

  T *obj = static_cast<T *>(reinterpret_cast<OuroObject *>(raw_obj));
  if (obj->GetStorageState() == StorageState::Dehydrated)
  {
    return;  // Already dehydrated
  }

  // 1. Save if UnsavedNew or Dirty
  Save(ptr);

  // 2. Free payload memory, set payload to null, and mark ControlBlock as Dehydrated!
  delete obj;
  ork_bind_object_payload(id, nullptr);
  ork_set_storage_state(id, static_cast<uint8_t>(StorageState::Dehydrated));
}

/**
 * @brief Rehydrate a dehydrated object: allocate new empty shell T(), load blueprint, and rebind payload to ControlBlock.
 * Target HandleID & child connection handles remain 100% stable!
 */
template <typename T>
OuroPtr<T> Rehydrate(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Invalid HandleID.");
  }

  // Check if object payload already exists
  ::OuroObject *raw_obj = nullptr;
  if (ork_acquire_object_pointer(id, &raw_obj) == ORK_STATUS_OK && raw_obj)
  {
    return OuroPtr<T>(id);  // Object payload is already loaded
  }

  auto storage = GetStorageBackend();
  if (!storage)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Storage backend not initialized.");
  }

  std::vector<uint8_t> data;
  if (!storage->LoadBlueprint(id, data))
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Blueprint data not found in storage backend.");
  }

  // 1. Set ActiveOwnerGuard so child handles constructed in T() inherit this object's ID as owner
  ActiveOwnerGuard guard(id);

  T *empty_shell = nullptr;
  try
  {
    empty_shell = new T();
    detail::PopActiveObject();
  }
  catch (...)
  {
    detail::PopActiveObject();
    throw;
  }
  empty_shell->SetObjectID(id);

  // 2. Unpack Payload & Edge Roster
  UnpackBlueprint(*empty_shell, data);

  // 3. Re-bind payload pointer to existing ControlBlock in Registry
  if (ork_bind_object_payload(
          id, reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(empty_shell))
      ) != ORK_STATUS_OK)
  {
    delete empty_shell;
    throw std::runtime_error("OuroKore Rehydrate Error: Failed to re-bind payload pointer to ControlBlock.");
  }

  empty_shell->SetStorageState(StorageState::Clean);
  return OuroPtr<T>(id);
}

template <typename T>
OuroPtr<T> Rehydrate(const OuroPtr<T> &ptr)
{
  return Rehydrate<T>(ptr.GetTargetID());
}

}  // namespace ork
