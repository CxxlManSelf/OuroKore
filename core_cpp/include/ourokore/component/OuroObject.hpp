#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "OuroStream.hpp"
#include "Types.hpp"
#include "ourokore/c_api/component_api.h"

namespace ork
{

// Forward declarations
class OwningContainerHandle;
template <typename T>
class OwningHandle;
template <typename T>
class OuroPtr;
class Registry;
class ControlBlock;
class DeferredDeleteQueue;

namespace detail
{
template <typename T>
class Rehydrator;
}

/**
 * @brief Base class for all managed objects in OuroKore.
 * All concrete components must inherit from this class.
 */
class OuroObject
{
public:
  virtual ~OuroObject() = default;

  // 託管實體具備唯一生命週期識別碼，嚴格禁止值語意之拷貝與搬移
  OuroObject(const OuroObject &) = delete;
  OuroObject &operator=(const OuroObject &) = delete;
  OuroObject(OuroObject &&) = delete;
  OuroObject &operator=(OuroObject &&) = delete;

  // 禁止外部直接透過 new 或 new[] 產生物件，必須透過 ork::CreateObject 進行託管建立
  void *operator new(size_t) = delete;
  void *operator new[](size_t) = delete;

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

protected:
  OuroObject();

  /**
   * @brief Fallback in-place deleter for runtime when no custom deleter hook is registered.
   * Protected to prevent plugins from bypassing deferred delete queues.
   */
  virtual void DestroySelf() { delete this; }

private:
  friend class Registry;
  friend class ControlBlock;
  friend class DeferredDeleteQueue;

  template <typename T>
  friend class detail::Rehydrator;

  /**
   * @brief Sets the runtime instance identifier of this object.
   * Internal-only: managed strictly by Registry and Rehydration routines.
   */
  void SetObjectID(HandleID id) { m_object_id = id; }

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
      int32_t status = ork_lock_object_shared(m_target_id);
      if (status != ORK_STATUS_OK)
      {
        m_target_id = 0;
        throw std::runtime_error("OuroKore Error: Failed to acquire shared read lock on OuroObject.");
      }
    }
  }

  ~OuroReadLock() noexcept
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
      int32_t status = ork_lock_object(m_target_id);
      if (status != ORK_STATUS_OK)
      {
        m_target_id = 0;
        throw std::runtime_error("OuroKore Error: Failed to acquire exclusive write lock on OuroObject.");
      }
    }
  }

  ~OuroWriteLock() noexcept
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
