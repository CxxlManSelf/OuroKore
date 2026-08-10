#pragma once

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "OuroObject.hpp"
#include "ourokore/c_api/component_api.h"

namespace ork
{

namespace detail
{
inline thread_local std::vector<OuroObject *> g_active_object_stack;

inline void PushActiveObject(OuroObject *obj)
{
  g_active_object_stack.push_back(obj);
}

inline void PopActiveObject()
{
  if (!g_active_object_stack.empty())
  {
    g_active_object_stack.pop_back();
  }
}

inline OuroObject *GetActiveObject()
{
  return g_active_object_stack.empty() ? nullptr : g_active_object_stack.back();
}
}  // namespace detail

// Inline implementation of OuroObject constructor for ActiveObject tracking
inline OuroObject::OuroObject()
{
  detail::PushActiveObject(this);
}

// Forward declarations
template <typename T>
class OuroPtr;
class OwningContainerHandle;
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
 * @brief OuroPtr represents a lifetime guard operational execution pointer (RAII).
 * Every active OuroPtr unconditionally registers a root edge (ORK_ROOT_ID -> m_target_id)
 * to guarantee 100% memory safety and lifetime protection while alive.
 */
template <typename T>
class OuroPtr
{
public:
  using RawT = std::remove_const_t<T>;
  static_assert(std::is_base_of_v<OuroObject, RawT>, "T must inherit from OuroObject");
  static_assert(std::is_convertible_v<RawT *, OuroObject *>, "T* must be convertible to OuroObject*");

  template <typename U>
  friend class OuroPtr;

  OuroPtr() = default;

  explicit OuroPtr(HandleID target_id) : m_target_id(target_id)
  {
    if (m_target_id != 0)
    {
      ork_register_edge(ORK_ROOT_ID, m_target_id);
    }
  }

  ~OuroPtr() { Release(); }

  // Disable copying
  OuroPtr(const OuroPtr &) = delete;
  OuroPtr &operator=(const OuroPtr &) = delete;

  // Enable moving
  OuroPtr(OuroPtr &&other) noexcept : m_target_id(other.m_target_id)
  {
    other.m_target_id = 0;
  }

  OuroPtr &operator=(OuroPtr &&other) noexcept
  {
    if (this != &other)
    {
      Release();
      m_target_id = other.m_target_id;
      other.m_target_id = 0;
    }
    return *this;
  }

  // Template converting move constructor (allows OuroPtr<T> -> OuroPtr<const T>)
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  OuroPtr(OuroPtr<U> &&other) noexcept : m_target_id(other.m_target_id)
  {
    other.m_target_id = 0;
  }

  // Template converting move assignment
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  OuroPtr &operator=(OuroPtr<U> &&other) noexcept
  {
    if (this->m_target_id != other.m_target_id)
    {
      Release();
      m_target_id = other.m_target_id;
    }
    other.m_target_id = 0;
    return *this;
  }

  void Release()
  {
    if (m_target_id != 0)
    {
      ork_unregister_edge(ORK_ROOT_ID, m_target_id);
      m_target_id = 0;
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

private:
  HandleID m_target_id = 0;
};

/**
 * @brief Base class for all Owning Handles (Single and Container variant).
 * Holds explicit region-unique Slot Name, Owner ID, and dynamic Target IDs vector.
 */
class OwningContainerHandle
{
public:
  explicit OwningContainerHandle(std::string slot_name)
      : m_slot_name(std::move(slot_name)), m_owner_id(GetActiveOwnerHelper())
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj)
    {
      active_obj->RegisterHandle(this);
    }
  }

  virtual ~OwningContainerHandle() { ReleaseAll(); }

  // Copy semantics
  OwningContainerHandle(const OwningContainerHandle &other)
      : m_slot_name(other.m_slot_name), m_owner_id(GetActiveOwnerHelper())
  {
    for (HandleID tid : other.m_target_ids)
    {
      AddTarget(tid);
    }
  }

  OwningContainerHandle &operator=(const OwningContainerHandle &other)
  {
    if (this != &other)
    {
      ReleaseAll();
      for (HandleID tid : other.m_target_ids)
      {
        AddTarget(tid);
      }
    }
    return *this;
  }

  // Move semantics (Zero-Cost Reallocation for same-owner moves)
  OwningContainerHandle(OwningContainerHandle &&other) noexcept
      : m_slot_name(std::move(other.m_slot_name)), m_owner_id(other.m_owner_id)
  {
    m_target_ids = std::move(other.m_target_ids);
    other.m_target_ids.clear();
  }

  OwningContainerHandle &operator=(OwningContainerHandle &&other) noexcept
  {
    if (this != &other)
    {
      ReleaseAll();
      m_slot_name = std::move(other.m_slot_name);
      m_target_ids = std::move(other.m_target_ids);
      other.m_target_ids.clear();
    }
    return *this;
  }

  const std::string &GetSlotName() const { return m_slot_name; }
  HandleID GetOwnerID() const { return m_owner_id; }
  const std::vector<HandleID> &GetTargetIDs() const { return m_target_ids; }
  size_t GetTargetCount() const { return m_target_ids.size(); }

  /**
   * @brief Lock and acquire all child objects in this container into an OuroPtr vector.
   */
  template <typename T = OuroObject>
  std::vector<OuroPtr<T>> LockAndAcquireAll() const
  {
    std::vector<OuroPtr<T>> result;
    result.reserve(m_target_ids.size());
    for (HandleID tid : m_target_ids)
    {
      if (tid != 0)
      {
        result.emplace_back(tid);
      }
    }
    return result;
  }

  void AddTarget(HandleID target_id)
  {
    if (target_id == 0) return;
    CheckEnforceRules(target_id);
    ork_register_edge(m_owner_id, target_id);
    m_target_ids.push_back(target_id);
  }

  void RemoveTarget(HandleID target_id)
  {
    auto it = std::find(m_target_ids.begin(), m_target_ids.end(), target_id);
    if (it != m_target_ids.end())
    {
      ork_unregister_edge(m_owner_id, target_id);
      m_target_ids.erase(it);
    }
  }

  void ReleaseAll()
  {
    for (HandleID tid : m_target_ids)
    {
      if (tid != 0)
      {
        ork_unregister_edge(m_owner_id, tid);
      }
    }
    m_target_ids.clear();
  }

protected:
  void CheckEnforceRules(HandleID target_id) const
  {
    if (m_owner_id == ORK_ROOT_ID && target_id != 0)
    {
      throw std::runtime_error(
          "OuroKore Error: OwningHandle with a valid target is strictly forbidden on Stack/Global (owner must not be "
          "ORK_ROOT_ID). Use OuroPtr instead.");
    }
  }

  static HandleID GetActiveOwnerHelper()
  {
    HandleID owner = 0;
    ork_get_active_owner(&owner);
    return owner;
  }

  std::string m_slot_name;
  const HandleID m_owner_id = 0;
  std::vector<HandleID> m_target_ids;
};

// Inline implementation of OuroObject::RegisterHandle after OwningContainerHandle declaration
inline void OuroObject::RegisterHandle(OwningContainerHandle *handle)
{
  if (!handle) return;
  const std::string &name = handle->GetSlotName();
  if (m_registered_handles.find(name) != m_registered_handles.end())
  {
    throw std::runtime_error("OuroKore Error: Duplicate Handle Slot Name detected in OuroObject: " + name);
  }
  m_registered_handles[name] = handle;
}

/**
 * @brief OwningHandle controls object lifecycles inside Parent OuroObjects.
 * Inherits from OwningContainerHandle with strict max Target count of 1.
 */
template <typename T>
class OwningHandle : protected OwningContainerHandle
{
  friend class OuroObject;

  template <typename U>
  friend class OwningHandle;

public:
  using RawT = std::remove_const_t<T>;
  using OwningContainerHandle::GetOwnerID;
  using OwningContainerHandle::GetSlotName;

  static_assert(std::is_base_of_v<OuroObject, RawT>, "T must inherit from OuroObject");

  explicit OwningHandle(std::string slot_name) : OwningContainerHandle(std::move(slot_name)) {}

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(std::string slot_name, const OuroPtr<U> &ptr) : OwningContainerHandle(std::move(slot_name))
  {
    SetTarget(ptr.GetTargetID());
  }

  // Copy Constructor
  OwningHandle(const OwningHandle &other) : OwningContainerHandle(other.m_slot_name) { SetTarget(other.GetTargetID()); }

  // Converting Copy Constructor
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(std::string slot_name, const OwningHandle<U> &other) : OwningContainerHandle(std::move(slot_name))
  {
    SetTarget(other.GetTargetID());
  }

  // Copy Assignment
  OwningHandle &operator=(const OwningHandle &other)
  {
    if (this != &other)
    {
      SetTarget(other.GetTargetID());
    }
    return *this;
  }

  // Converting Copy Assignment from OwningHandle<U>
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle &operator=(const OwningHandle<U> &other)
  {
    SetTarget(other.GetTargetID());
    return *this;
  }

  // Converting Copy Assignment from OuroPtr<U>
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle &operator=(const OuroPtr<U> &ptr)
  {
    SetTarget(ptr.GetTargetID());
    return *this;
  }

  // Move Constructor (inherits same m_owner_id & slot_name)
  OwningHandle(OwningHandle &&other) noexcept : OwningContainerHandle(std::move(other)) {}

  // Move Assignment (Strong Exception Guarantee: Step 1 Check -> Step 2 Register -> Step 3 Release)
  OwningHandle &operator=(OwningHandle &&other) noexcept
  {
    if (this != &other)
    {
      HandleID target_id = other.GetTargetID();
      HandleID other_owner_id = other.m_owner_id;

      // Step 1: Rule check
      CheckEnforceRules(target_id);

      // Step 2: Edge registration if cross-host
      if (target_id != 0 && m_owner_id != other_owner_id)
      {
        ork_register_edge(m_owner_id, target_id);
        ork_unregister_edge(other_owner_id, target_id);
      }

      // Step 3: Release old target
      Release();

      m_target_ids.clear();
      if (target_id != 0)
      {
        m_target_ids.push_back(target_id);
      }
      other.m_target_ids.clear();
    }
    return *this;
  }

  void SetTarget(HandleID target_id)
  {
    CheckEnforceRules(target_id);
    if (target_id != 0)
    {
      ork_register_edge(m_owner_id, target_id);
    }
    Release();
    m_target_ids.clear();
    if (target_id != 0)
    {
      m_target_ids.push_back(target_id);
    }
  }

  void Release() { ReleaseAll(); }

  HandleID GetTargetID() const { return m_target_ids.empty() ? 0 : m_target_ids[0]; }

  template <typename TargetT = T>
  OuroPtr<TargetT> LockAndAcquire() const
  {
    HandleID tid = GetTargetID();
    if (tid == 0)
    {
      return OuroPtr<TargetT>();
    }
    return OuroPtr<TargetT>(tid);
  }

private:
  friend class OuroObject;

  void *operator new(size_t) = delete;
  void *operator new[](size_t) = delete;
};

/**
 * @brief WeakHandle is an observer handle that holds WeakCount to prevent ControlBlock deletion.
 * Implements Lazy Pruning on CheckAlive probing.
 */
template <typename T>
class WeakHandle
{
  template <typename U>
  friend class WeakHandle;

public:
  using RawT = std::remove_const_t<T>;
  static_assert(std::is_base_of_v<OuroObject, RawT>, "T must inherit from OuroObject");

  WeakHandle() = default;

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  explicit WeakHandle(const OwningHandle<U> &handle) : m_target_id(handle.GetTargetID())
  {
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  explicit WeakHandle(const OuroPtr<U> &ptr) : m_target_id(ptr.GetTargetID())
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

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  WeakHandle(const WeakHandle<U> &other) : m_target_id(other.GetTargetID())
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

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  WeakHandle &operator=(const WeakHandle<U> &other)
  {
    if (this->m_target_id != other.GetTargetID())
    {
      Release();
      m_target_id = other.GetTargetID();
      if (m_target_id != 0)
      {
        ork_register_weak(m_target_id);
      }
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  WeakHandle &operator=(const OwningHandle<U> &handle)
  {
    Release();
    m_target_id = handle.GetTargetID();
    if (m_target_id != 0)
    {
      ork_register_weak(m_target_id);
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  WeakHandle &operator=(const OuroPtr<U> &ptr)
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

  template <typename TargetT = T>
  OuroPtr<TargetT> LockAndAcquire() const
  {
    if (!IsAlive())
    {
      return OuroPtr<TargetT>();
    }
    return OuroPtr<TargetT>(m_target_id);
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

  ActiveOwnerGuard guard(reserved_id);

  T *obj = nullptr;
  try
  {
    obj = new T(std::forward<Args>(args)...);
    detail::PopActiveObject();
  }
  catch (...)
  {
    detail::PopActiveObject();
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

  return OuroPtr<T>(reserved_id);
}

}  // namespace ork
