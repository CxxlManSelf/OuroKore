#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "OuroStream.hpp"
#include "ourokore/c_api/component_api.h"

namespace ork
{

using HandleID = uint64_t;

// Forward declarations
class OwningContainerHandle;
template <typename T>
class OwningHandle;
template <typename T>
class OuroPtr;

/**
 * @brief Storage lifecycle states for an OuroObject.
 */
enum class StorageState : uint8_t
{
  UnsavedNew = 0,  ///< Freshly created object, never saved to storage.
  Clean = 1,       ///< Saved in storage and memory payload is unmodified.
  Dirty = 2,       ///< Saved in storage but memory payload has been modified.
  Dehydrated = 3   ///< Saved in storage and payload memory freed (empty shell).
};

/**
 * @brief Base class for all managed objects in OuroKore.
 * All concrete components must inherit from this class.
 */
class OuroObject
{
public:
  virtual ~OuroObject() = default;

  /**
   * @brief Gets the runtime instance identifier of this object.
   */
  HandleID GetObjectID() const { return m_object_id; }

  /**
   * @brief Get current storage lifecycle state from ControlBlock.
   */
  StorageState GetStorageState() const
  {
    uint8_t state_val = 0;
    if (ork_get_storage_state(m_object_id, &state_val) == ORK_STATUS_OK)
    {
      return static_cast<StorageState>(state_val);
    }
    return StorageState::UnsavedNew;
  }

  /**
   * @brief Set storage lifecycle state in ControlBlock.
   */
  void SetStorageState(StorageState state)
  {
    ork_set_storage_state(m_object_id, static_cast<uint8_t>(state));
  }

  /**
   * @brief Mark object as Dirty in ControlBlock if currently Clean.
   */
  void MarkDirty()
  {
    ork_mark_dirty(m_object_id);
  }

  /**
   * @brief Register an OwningContainerHandle into this object's handle roster.
   */
  void RegisterHandle(OwningContainerHandle *handle);

  /**
   * @brief Retrieve registered handles roster.
   */
  const std::unordered_map<std::string, OwningContainerHandle *> &GetRegisteredHandles() const
  {
    return m_registered_handles;
  }

  /**
   * @brief Pure Payload serialization interface for concrete component classes.
   */
  virtual void SerializePayload(OuroStream & /*stream*/) const {}
  virtual void DeserializePayload(OuroStream & /*stream*/) {}

  /**
   * @brief Sets the runtime instance identifier of this object.
   */
  void SetObjectID(HandleID id) { m_object_id = id; }

protected:
  OuroObject();

private:
  friend class Registry;

  HandleID m_object_id = 0;
  std::unordered_map<std::string, OwningContainerHandle *> m_registered_handles;
};

/**
 * @brief RAII Scope Guard for Read Lock (Shared Lock) on an OuroObject.
 */
class OuroReadLock
{
public:
  explicit OuroReadLock(const OuroObject &obj) : m_target_id(obj.GetObjectID())
  {
    if (m_target_id != 0)
    {
      ork_lock_object_shared(m_target_id);
    }
  }

  ~OuroReadLock()
  {
    if (m_target_id != 0)
    {
      ork_unlock_object_shared(m_target_id);
    }
  }

  OuroReadLock(const OuroReadLock &) = delete;
  OuroReadLock &operator=(const OuroReadLock &) = delete;

private:
  HandleID m_target_id = 0;
};

/**
 * @brief RAII Scope Guard for Write Lock (Exclusive Lock) on an OuroObject.
 * Automatically marks the object as Dirty upon scope release if currently Clean.
 */
class OuroWriteLock
{
public:
  explicit OuroWriteLock(OuroObject &obj)
      : m_target_id(obj.GetObjectID())
  {
    if (m_target_id != 0)
    {
      ork_lock_object(m_target_id);
    }
  }

  ~OuroWriteLock()
  {
    if (m_target_id != 0)
    {
      ork_mark_dirty(m_target_id);
      ork_unlock_object(m_target_id);
    }
  }

  OuroWriteLock(const OuroWriteLock &) = delete;
  OuroWriteLock &operator=(const OuroWriteLock &) = delete;

private:
  HandleID m_target_id = 0;
};

}  // namespace ork
