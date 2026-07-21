#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "OuroObject.hpp"
#include "ourokore/c_api/component_api.h"

namespace ork
{

// Forward declarations
template <typename T>
class OuroPtr;
template <typename T>
class OwningHandle;
template <typename T>
class WeakHandle;

/**
 * @brief RAII Guard for Thread-Local Active Owner context.
 * Automatically backs up and restores the active owner ID on the current thread.
 */
class ActiveOwnerGuard
{
public:
  explicit ActiveOwnerGuard(HandleID new_owner)
  {
    ork_get_active_owner(&m_backup_owner);
    ork_set_active_owner(new_owner);
  }

  ~ActiveOwnerGuard() { ork_set_active_owner(m_backup_owner); }

  ActiveOwnerGuard(const ActiveOwnerGuard &) = delete;
  ActiveOwnerGuard &operator=(const ActiveOwnerGuard &) = delete;

private:
  HandleID m_backup_owner = ORK_ROOT_ID;
};

/**
 * @brief OuroPtr represents an active, locked operational execution pointer (RAII).
 * It can also act as the Root credential (owning a life cycle) when m_is_root is true.
 */
template <typename T>
class OuroPtr
{
public:
  static_assert(std::is_base_of_v<OuroObject, T>, "T must inherit from OuroObject");
  static_assert(std::is_convertible_v<T *, OuroObject *>, "T* must be convertible to OuroObject*");

  OuroPtr() = default;

  OuroPtr(HandleID target_id, bool write, bool is_root) : m_target_id(target_id), m_is_root(is_root), m_is_write(write)
  {
    if (m_is_root && m_target_id != 0)
    {
      ork_register_edge(ORK_ROOT_ID, m_target_id);
    }

    if (m_target_id != 0)
    {
      if (m_is_write)
      {
        ork_lock_object(m_target_id);
      }
      else
      {
        ork_lock_object_shared(m_target_id);
      }
    }
  }

  ~OuroPtr() { Release(); }

  // Disable copying
  OuroPtr(const OuroPtr &) = delete;
  OuroPtr &operator=(const OuroPtr &) = delete;

  // Enable moving
  OuroPtr(OuroPtr &&other) noexcept
      : m_target_id(other.m_target_id), m_is_root(other.m_is_root), m_is_write(other.m_is_write)
  {
    other.m_target_id = 0;
    other.m_is_root = false;
  }

  OuroPtr &operator=(OuroPtr &&other) noexcept
  {
    if (this != &other)
    {
      Release();
      m_target_id = other.m_target_id;
      m_is_root = other.m_is_root;
      m_is_write = other.m_is_write;
      other.m_target_id = 0;
      other.m_is_root = false;
    }
    return *this;
  }

  void Release()
  {
    if (m_target_id != 0)
    {
      if (m_is_write)
      {
        ork_unlock_object(m_target_id);
      }
      else
      {
        ork_unlock_object_shared(m_target_id);
      }
      if (m_is_root)
      {
        ork_unregister_edge(ORK_ROOT_ID, m_target_id);
      }
      m_target_id = 0;
      m_is_root = false;
    }
  }

  T *operator->() const
  {
    if (m_target_id == 0) return nullptr;
    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(m_target_id, &raw_obj) == ORK_STATUS_OK)
    {
      return static_cast<T *>(reinterpret_cast<OuroObject *>(raw_obj));
    }
    return nullptr;
  }

  T &operator*() const
  {
    T *ptr = operator->();
    if (!ptr)
    {
      throw std::runtime_error("Attempted to dereference a null or dead object via OuroPtr");
    }
    return *ptr;
  }

  explicit operator bool() const
  {
    if (m_target_id == 0) return false;
    ::OuroObject *raw_obj = nullptr;
    return ork_acquire_object_pointer(m_target_id, &raw_obj) == ORK_STATUS_OK;
  }

  HandleID GetTargetID() const { return m_target_id; }
  bool IsRoot() const { return m_is_root; }
  bool IsWriteLocked() const { return m_is_write; }

private:
  HandleID m_target_id = 0;
  bool m_is_root = false;
  bool m_is_write = false;
};

/**
 * @brief OwningHandle controls object lifecycles inside Parent OuroObjects.
 * It is strictly forbidden to be instantiated on Stack/Global.
 */
template <typename T>
class OwningHandle
{
public:
  static_assert(std::is_base_of_v<OuroObject, T>, "T must inherit from OuroObject");

  OwningHandle()
  {
    ork_get_active_owner(&m_owner_id);
    // Default constructed state (m_target_id == 0) is allowed.
  }

  OwningHandle(const OuroPtr<T> &ptr)
  {
    ork_get_active_owner(&m_owner_id);
    HandleID target_id = ptr.GetTargetID();
    CheckEnforceRules(target_id);
    m_target_id = target_id;
    if (m_target_id != 0)
    {
      ork_register_edge(m_owner_id, m_target_id);
    }
  }

  ~OwningHandle() { Release(); }

  // Copy constructor (establishes edge from this handle's owner context)
  OwningHandle(const OwningHandle &other)
  {
    ork_get_active_owner(&m_owner_id);
    HandleID target_id = other.m_target_id;
    CheckEnforceRules(target_id);
    m_target_id = target_id;
    if (m_target_id != 0)
    {
      ork_register_edge(m_owner_id, m_target_id);
    }
  }

  // Copy assignment (reuses pre-established owner ID)
  OwningHandle &operator=(const OwningHandle &other)
  {
    if (this != &other)
    {
      HandleID target_id = other.m_target_id;
      CheckEnforceRules(target_id);
      if (target_id == 0)
      {
        Release();
      }
      else
      {
        ork_register_edge(m_owner_id, target_id);
        Release();
        m_target_id = target_id;
      }
    }
    return *this;
  }

  OwningHandle &operator=(const OuroPtr<T> &ptr)
  {
    HandleID target_id = ptr.GetTargetID();
    CheckEnforceRules(target_id);
    if (target_id != 0)
    {
      ork_register_edge(m_owner_id, target_id);
    }
    Release();
    m_target_id = target_id;
    return *this;
  }

  // Move semantics
  OwningHandle(OwningHandle &&other) noexcept
  {
    ork_get_active_owner(&m_owner_id);
    HandleID target_id = other.m_target_id;
    CheckEnforceRules(target_id);
    m_target_id = target_id;
    if (m_target_id != 0 && m_owner_id != other.m_owner_id)
    {
      ork_register_edge(m_owner_id, m_target_id);
      ork_unregister_edge(other.m_owner_id, m_target_id);
    }
    other.m_target_id = 0;
  }

  OwningHandle &operator=(OwningHandle &&other) noexcept
  {
    if (this != &other)
    {
      HandleID target_id = other.m_target_id;
      HandleID other_owner_id = other.m_owner_id;
      CheckEnforceRules(target_id);
      if (target_id != 0 && m_owner_id != other_owner_id)
      {
        ork_register_edge(m_owner_id, target_id);
        ork_unregister_edge(other_owner_id, target_id);
      }
      Release();
      m_target_id = target_id;
      other.m_target_id = 0;
    }
    return *this;
  }

  void Release()
  {
    if (m_target_id != 0)
    {
      ork_unregister_edge(m_owner_id, m_target_id);
      m_target_id = 0;
    }
  }

  OuroPtr<T> LockAndAcquire(bool write = false) const
  {
    if (m_target_id == 0)
    {
      return OuroPtr<T>();
    }
    return OuroPtr<T>(m_target_id, write, false /*is_root*/);
  }

  HandleID GetTargetID() const { return m_target_id; }
  HandleID GetOwnerID() const { return m_owner_id; }

private:
  void CheckEnforceRules(HandleID target_id) const
  {
    if (m_owner_id == ORK_ROOT_ID && target_id != 0)
    {
      throw std::runtime_error(
          "OuroKore Error: OwningHandle with a valid target is strictly forbidden on Stack/Global (owner must not be "
          "ORK_ROOT_ID). Use OuroPtr instead.");
    }
  }

  HandleID m_target_id = 0;
  HandleID m_owner_id = 0;
};

/**
 * @brief WeakHandle is an observer handle that holds WeakCount to prevent ControlBlock deletion.
 * Implements Lazy Pruning (自我閹割) on CheckAlive probing.
 */
template <typename T>
class WeakHandle
{
public:
  static_assert(std::is_base_of_v<OuroObject, T>, "T must inherit from OuroObject");

  WeakHandle() = default;

  explicit WeakHandle(const OwningHandle<T> &handle) : m_target_id(handle.GetTargetID())
  {
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
  }

  explicit WeakHandle(const OuroPtr<T> &ptr) : m_target_id(ptr.GetTargetID())
  {
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
  }

  ~WeakHandle() { Release(); }

  // Copy semantics
  WeakHandle(const WeakHandle &other) : m_target_id(other.m_target_id)
  {
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
  }

  WeakHandle &operator=(const WeakHandle &other)
  {
    if (this != &other)
    {
      Release();
      m_target_id = other.m_target_id;
      if (m_target_id != 0)
      {
        ork_register_weak(m_target_id);
      }
    }
    return *this;
  }

  WeakHandle &operator=(const OwningHandle<T> &handle)
  {
    Release();
    m_target_id = handle.GetTargetID();
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
    return *this;
  }

  WeakHandle &operator=(const OuroPtr<T> &ptr)
  {
    Release();
    m_target_id = ptr.GetTargetID();
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
    return *this;
  }

  // Move semantics
  WeakHandle(WeakHandle &&other) noexcept : m_target_id(other.m_target_id) { other.m_target_id = 0; }

  WeakHandle &operator=(WeakHandle &&other) noexcept
  {
    if (this != &other)
    {
      Release();
      m_target_id = other.m_target_id;
      other.m_target_id = 0;
    }
    return *this;
  }

  void Release()
  {
    if (m_target_id != 0)
    {
      ork_unregister_weak(m_target_id);
      m_target_id = 0;
    }
  }

  bool IsAlive() const
  {
    if (m_target_id == 0) return false;
    int32_t alive = 0;
    // Check alive and perform pruning if dead (argument 3 = 1)
    if (ork_check_alive(m_target_id, &alive, 1) == ORK_STATUS_OK)
    {
      if (!alive)
      {
        m_target_id = 0;  // Lazy pruning!
        return false;
      }
      return true;
    }
    m_target_id = 0;
    return false;
  }

  OuroPtr<T> LockAndAcquire(bool write = false) const
  {
    if (!IsAlive())
    {
      return OuroPtr<T>();
    }
    return OuroPtr<T>(m_target_id, write, false /*is_root*/);
  }

  HandleID GetTargetID() const { return m_target_id; }

private:
  mutable HandleID m_target_id = 0;
};

/**
 * @brief Factory function to construct and register managed OuroObjects.
 * Enforces Diamond Inheritance compile-time assertion.
 */
template <typename T, typename... Args>
OuroPtr<T> CreateObject(Args &&...args)
{
  static_assert(std::is_base_of_v<OuroObject, T>, "T must inherit from OuroObject");
  static_assert(std::is_convertible_v<T *, OuroObject *>,
                "T* must be convertible to OuroObject* (Diamond Inheritance forbidden)");

  HandleID reserved_id = 0;
  if (ork_reserve_object_id(&reserved_id) != ORK_STATUS_OK)
  {
    throw std::runtime_error("OuroKore Error: Failed to reserve HandleID from Registry.");
  }

  // Pre-set active owner ID to nested child handles constructed in T's constructor
  ActiveOwnerGuard guard(reserved_id);

  T *obj = nullptr;
  try
  {
    obj = new T(std::forward<Args>(args)...);
  }
  catch (...)
  {
    // If constructor fails, release the reserved ID to avoid leaks.
    ork_unregister_object(reserved_id);
    throw;
  }

  if (ork_bind_object_payload(reserved_id, reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(obj))) !=
      ORK_STATUS_OK)
  {
    delete obj;
    ork_unregister_object(reserved_id);
    throw std::runtime_error("OuroKore Error: Failed to bind payload to reserved HandleID.");
  }

  // Return the OuroPtr marked as Root (is_root = true)
  return OuroPtr<T>(reserved_id, true /*write*/, true /*is_root*/);
}

}  // namespace ork
