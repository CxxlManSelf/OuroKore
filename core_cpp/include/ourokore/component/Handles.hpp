#pragma once

#include <algorithm>
#include <atomic>
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
/// @brief 執行緒區域（Thread-Local）作用中物件堆疊。
/// 在物件建構期間（由 OuroObject 基類建構子 Push），讓內部成員欄位（如 OwningHandle / OwningContainerHandle）
/// 能自動取得當前正在建構的父物件指標並向其註冊（RegisterHandle），待物件本體建構完成後再行 Pop。
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

/**
 * @brief RAII Guard for Thread-Local Active Object Construction Stack.
 * Ensures the active object stack is safely restored to its initial depth
 * upon scope exit, preventing leaks on either success or constructor exceptions.
 */
class ActiveObjectGuard
{
public:
  ActiveObjectGuard() : m_initial_depth(g_active_object_stack.size()) {}
  ~ActiveObjectGuard() noexcept
  {
    while (g_active_object_stack.size() > m_initial_depth)
    {
      PopActiveObject();
    }
  }

  ActiveObjectGuard(const ActiveObjectGuard &) = delete;
  ActiveObjectGuard &operator=(const ActiveObjectGuard &) = delete;

private:
  size_t m_initial_depth = 0;
};
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
class UnboundHandle;

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

  ~ActiveOwnerGuard() noexcept
  {
    ork_set_active_owner(m_backup_owner);
  }

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

  explicit OuroPtr(HandleID target_id) :
      m_target_id(target_id)
  {
    if (m_target_id != 0)
    {
      if (ork_register_edge(ORK_ROOT_ID, m_target_id) != ORK_STATUS_OK)
      {
        m_target_id = 0;
      }
    }
  }

  struct PreLockedTag {};

  // Construct from pre-locked root edge (e.g. from UnboundHandle::LockAndAcquire atomic promotion)
  explicit OuroPtr(HandleID target_id, PreLockedTag) noexcept :
      m_target_id(target_id)
  {
  }

  ~OuroPtr() noexcept
  {
    Release();
  }

  // Disable copying
  OuroPtr(const OuroPtr &) = delete;
  OuroPtr &operator=(const OuroPtr &) = delete;

  // Enable moving
  OuroPtr(OuroPtr &&other) noexcept :
      m_target_id(other.m_target_id)
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
  OuroPtr(OuroPtr<U> &&other) noexcept :
      m_target_id(other.m_target_id)
  {
    other.m_target_id = 0;
  }

  // Template converting move assignment
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  OuroPtr &operator=(OuroPtr<U> &&other) noexcept
  {
    if (static_cast<const void *>(this) != static_cast<const void *>(&other))
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
      ork_unregister_edge(ORK_ROOT_ID, m_target_id);
      m_target_id = 0;
    }
  }

  T *get() const noexcept
  {
    if (m_target_id == 0) return nullptr;
    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(m_target_id, &raw_obj) == ORK_STATUS_OK)
    {
      return static_cast<T *>(reinterpret_cast<OuroObject *>(raw_obj));
    }
    return nullptr;
  }

  T *operator->() const noexcept
  {
    return get();
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

  HandleID GetTargetID() const
  {
    return m_target_id;
  }

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
  explicit OwningContainerHandle(std::string slot_name) :
      m_slot_name(std::move(slot_name)),
      m_owner_id(GetActiveOwnerHelper())
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj)
    {
      active_obj->RegisterHandle(this);
    }
  }

  virtual ~OwningContainerHandle() noexcept
  {
    ReleaseAll();
  }

  // Copy semantics
  OwningContainerHandle(const OwningContainerHandle &other) :
      m_slot_name(other.m_slot_name),
      m_owner_id(GetActiveOwnerHelper())
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj)
    {
      active_obj->RegisterHandle(this);
    }

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

  // Move semantics (Zero-Cost for same-owner moves, Edge-Transfer for cross-owner moves)
  OwningContainerHandle(OwningContainerHandle &&other) noexcept :
      m_slot_name(std::move(other.m_slot_name)),
      m_owner_id(DetermineMoveOwner(other.m_owner_id))
  {
    OuroObject *active_obj = detail::GetActiveObject();
    if (active_obj && m_owner_id != other.m_owner_id)
    {
      active_obj->RegisterHandle(this);
    }

    if (m_owner_id == other.m_owner_id)
    {
      // Same-host move: Zero-Cost move without Registry edge overhead
      m_target_ids = std::move(other.m_target_ids);
    }
    else
    {
      // Cross-host move: transfer edges from other.m_owner_id to this->m_owner_id
      m_target_ids.reserve(other.m_target_ids.size());
      for (HandleID tid : other.m_target_ids)
      {
        if (tid != 0)
        {
          CheckEnforceRules(tid);
          ork_register_edge(m_owner_id, tid);
          ork_unregister_edge(other.m_owner_id, tid);
          m_target_ids.push_back(tid);
        }
      }
    }
    other.m_target_ids.clear();
  }

  OwningContainerHandle &operator=(OwningContainerHandle &&other) noexcept
  {
    if (this != &other)
    {
      // 1. Release old targets held by this handle
      ReleaseAll();

      if (m_owner_id == other.m_owner_id)
      {
        // 2A. Same-host move: Zero-Cost move without Registry edge overhead
        m_target_ids = std::move(other.m_target_ids);
      }
      else
      {
        // 2B. Cross-host move: transfer edges from other.m_owner_id to this->m_owner_id
        m_target_ids.reserve(other.m_target_ids.size());
        for (HandleID tid : other.m_target_ids)
        {
          if (tid != 0)
          {
            CheckEnforceRules(tid);
            ork_register_edge(m_owner_id, tid);
            ork_unregister_edge(other.m_owner_id, tid);
            m_target_ids.push_back(tid);
          }
        }
      }
      other.m_target_ids.clear();
    }
    return *this;
  }

  const std::string &GetSlotName() const
  {
    return m_slot_name;
  }
  HandleID GetOwnerID() const
  {
    return m_owner_id;
  }
  const std::vector<HandleID> &GetTargetIDs() const
  {
    return m_target_ids;
  }
  size_t GetTargetCount() const
  {
    return m_target_ids.size();
  }

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

  bool AddTarget(HandleID target_id)
  {
    if (target_id == 0) return false;
    CheckEnforceRules(target_id);

    // If owner object is currently in Dehydrated state (e.g. during rehydration unpack),
    // the edge in Registry is already retained from before dehydration.
    uint8_t state_val = 0;
    if (m_owner_id != 0 && ork_get_storage_state(m_owner_id, &state_val) == ORK_STATUS_OK &&
        static_cast<StorageState>(state_val) == StorageState::Dehydrated)
    {
      m_target_ids.push_back(target_id);
      return true;
    }

    if (ork_register_edge(m_owner_id, target_id) == ORK_STATUS_OK)
    {
      m_target_ids.push_back(target_id);
      return true;
    }
    return false;
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
    // If owner object is currently in Dehydrated state (i.e. payload memory being freed for dehydration),
    // retain edges in Registry and simply clear target IDs.
    uint8_t state_val = 0;
    if (m_owner_id != 0 && ork_get_storage_state(m_owner_id, &state_val) == ORK_STATUS_OK &&
        static_cast<StorageState>(state_val) == StorageState::Dehydrated)
    {
      m_target_ids.clear();
      return;
    }

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
          "ORK_ROOT_ID). Use OuroPtr instead."
      );
    }
  }

  static HandleID GetActiveOwnerHelper()
  {
    HandleID owner = 0;
    ork_get_active_owner(&owner);
    return owner;
  }

  static HandleID DetermineMoveOwner(HandleID other_owner)
  {
    HandleID current_owner = GetActiveOwnerHelper();
    if (current_owner != ORK_ROOT_ID && current_owner != 0 && current_owner != other_owner)
    {
      return current_owner;
    }
    return other_owner;
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
 *
 * 【循環參照與雙向互指使用建議】
 * 在 OuroKore 體系中，業務領域物件圖內部的所有父子關係、雙向互指（如 A <-> B）、網狀關聯，
 * 請一律直接且大膽地使用 OwningHandle！
 *
 * 核心具備進程級背景循環垃圾收集器（CycleCollector），當整個互指圖的外部根引用（OuroPtr）歸零時，
 * 收集器會自動在背景偵測閉環孤島並執行非同步外科手術安全回收。
 * 開發者完全不需要、也不應該為了「打破循環參照」而手動改用 UnboundHandle。
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

  explicit OwningHandle(std::string slot_name) :
      OwningContainerHandle(std::move(slot_name))
  {
  }

  virtual ~OwningHandle() noexcept = default;

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(std::string slot_name, const OuroPtr<U> &ptr) :
      OwningContainerHandle(std::move(slot_name))
  {
    SetTarget(ptr.GetTargetID());
  }

  // Copy Constructor
  OwningHandle(const OwningHandle &other) :
      OwningContainerHandle(other.m_slot_name)
  {
    SetTarget(other.GetTargetID());
  }

  // Converting Copy Constructor (inherits other's slot_name)
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(const OwningHandle<U> &other) :
      OwningContainerHandle(other.m_slot_name)
  {
    SetTarget(other.GetTargetID());
  }

  // Converting Copy Constructor with explicit slot_name
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(std::string slot_name, const OwningHandle<U> &other) :
      OwningContainerHandle(std::move(slot_name))
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

  // Move Constructor (Zero-Cost for same-owner, Edge-Transfer for cross-owner)
  OwningHandle(OwningHandle &&other) noexcept :
      OwningContainerHandle(std::move(other))
  {
  }

  // Converting Move Constructor (Zero-Cost for same-owner, Edge-Transfer for cross-owner)
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(OwningHandle<U> &&other) noexcept :
      OwningContainerHandle(std::move(other))
  {
  }

  // Converting Move Constructor with explicit slot_name
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle(std::string slot_name, OwningHandle<U> &&other) noexcept :
      OwningContainerHandle(std::move(slot_name))
  {
    MoveAssignImpl(other);
  }

  // Move Assignment (Strong Exception Guarantee: Step 1 Check -> Step 2 Register -> Step 3 Release)
  OwningHandle &operator=(OwningHandle &&other) noexcept
  {
    MoveAssignImpl(other);
    return *this;
  }

  // Converting Move Assignment from OwningHandle<U>
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  OwningHandle &operator=(OwningHandle<U> &&other) noexcept
  {
    MoveAssignImpl(other);
    return *this;
  }

  void SetTarget(HandleID target_id)
  {
    if (GetTargetID() == target_id) return;
    Release();
    if (target_id != 0)
    {
      AddTarget(target_id);
    }
  }

  void Release()
  {
    ReleaseAll();
  }

  HandleID GetTargetID() const
  {
    return m_target_ids.empty() ? 0 : m_target_ids[0];
  }

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
  template <typename OtherHandleT>
  void MoveAssignImpl(OtherHandleT &other) noexcept
  {
    if (static_cast<const void *>(this) != static_cast<const void *>(&other))
    {
      HandleID target_id = other.GetTargetID();
      HandleID old_target_id = GetTargetID();
      HandleID other_owner_id = other.m_owner_id;

      // Step 1: Rule check
      CheckEnforceRules(target_id);

      if (old_target_id != 0 && old_target_id == target_id)
      {
        // Target is the same object:
        // Whether cross-host or same-host, 'this' already holds target_id under m_owner_id.
        // We only need to unregister one edge from other_owner_id and clear the source handle 'other'.
        ork_unregister_edge(other_owner_id, target_id);
        other.m_target_ids.clear();
      }
      else
      {
        // Target is different (or target_id == 0)
        // Step 2: Edge registration if cross-host
        if (target_id != 0 && m_owner_id != other_owner_id)
        {
          ork_register_edge(m_owner_id, target_id);
          ork_unregister_edge(other_owner_id, target_id);
        }

        // Step 3: Release old target (Release() -> ReleaseAll() already clears m_target_ids)
        Release();

        if (target_id != 0)
        {
          m_target_ids.push_back(target_id);
          other.m_target_ids.clear();
        }
      }
    }
  }

  friend class OuroObject;

  void *operator new(size_t) = delete;
  void *operator new[](size_t) = delete;
};

/**
 * @brief UnboundHandle（無繫結句柄）：解耦弱引用與動態外掛模組非同步熱卸載安全句柄。
 *
 * 【核心語意與架構特性】
 * 1. 無入邊繫結 (In-degree = 0)：不佔用物件圖中的任何拓撲強引用邊緣，目標物件的生命週期與存亡
 *    完全不受 UnboundHandle 束縛，可被外部自由脫水、銷毀、或伴隨動態模組進行非同步卸載。
 * 2. 動態模組熱卸載防釘死：跨 DLL / 外掛插件邊界觀察或引用服務時，絕不釘死宿主或插件實體。
 * 3. 安全原子提升 (LockAndAcquire)：在目標物件存活且未處於拆解/卸載狀態時，可原子提升為持有根邊緣
 *    的短期操作指針 OuroPtr<T>；若對象已死亡或正在非同步卸載中，提升保證安全失敗並傳回空指針，
 *    徹底杜絕野指標 (Dangling Pointers) 與釋放後使用 (UAF)。
 * 4. 高效無鎖惰性修剪 (Lock-Free Lazy Pruning)：在 IsAlive() 與提升失敗時以 CAS 競爭修剪墓碑弱引用。
 * 5. 與傳統 std::weak_ptr 的心智模型差異（切勿用於破環）：
 *    在傳統 C++ (std::shared_ptr) 中，weak_ptr 常用於打破雙向互指造成的記憶體洩漏；
 *    但在 OuroKore 中，業務圖內的雙向互指一律直接使用 OwningHandle（由 CycleCollector 自動回收）。
 *    UnboundHandle 的設計使命「絕非」用於破環，而是專為「跨動態 DLL / 外掛插件邊界之生命週期解耦」、
 *    「外掛非同步熱卸載防釘死」以及「純唯讀旁路快取觀察」而生。
 */
template <typename T>
class UnboundHandle
{
  template <typename U>
  friend class UnboundHandle;

public:
  using RawT = std::remove_const_t<T>;
  static_assert(std::is_base_of_v<OuroObject, RawT>, "T must inherit from OuroObject");

  UnboundHandle() = default;

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  explicit UnboundHandle(const OwningHandle<U> &handle) :
      m_target_id(handle.GetTargetID())
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  explicit UnboundHandle(const OuroPtr<U> &ptr) :
      m_target_id(ptr.GetTargetID())
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
  }

  ~UnboundHandle() noexcept
  {
    Release();
  }

  // Copy semantics
  UnboundHandle(const UnboundHandle &other) :
      m_target_id(other.m_target_id.load(std::memory_order_relaxed))
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle(const UnboundHandle<U> &other) :
      m_target_id(other.GetTargetID())
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
  }

  UnboundHandle &operator=(const UnboundHandle &other)
  {
    if (this != &other)
    {
      Release();
      HandleID tid = other.m_target_id.load(std::memory_order_relaxed);
      m_target_id.store(tid, std::memory_order_relaxed);
      if (tid != 0)
      {
        ork_register_weak(tid);
      }
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle &operator=(const UnboundHandle<U> &other)
  {
    HandleID other_tid = other.GetTargetID();
    if (this->m_target_id.load(std::memory_order_relaxed) != other_tid)
    {
      Release();
      m_target_id.store(other_tid, std::memory_order_relaxed);
      if (other_tid != 0)
      {
        ork_register_weak(other_tid);
      }
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle &operator=(const OwningHandle<U> &handle)
  {
    Release();
    HandleID tid = handle.GetTargetID();
    m_target_id.store(tid, std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle &operator=(const OuroPtr<U> &ptr)
  {
    Release();
    HandleID tid = ptr.GetTargetID();
    m_target_id.store(tid, std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_register_weak(tid);
    }
    return *this;
  }

  // Move semantics
  UnboundHandle(UnboundHandle &&other) noexcept :
      m_target_id(other.m_target_id.exchange(0, std::memory_order_relaxed))
  {
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle(UnboundHandle<U> &&other) noexcept :
      m_target_id(other.m_target_id.exchange(0, std::memory_order_relaxed))
  {
  }

  UnboundHandle &operator=(UnboundHandle &&other) noexcept
  {
    if (this != &other)
    {
      Release();
      m_target_id.store(other.m_target_id.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
  }

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<std::remove_const_t<U> *, RawT *>>>
  UnboundHandle &operator=(UnboundHandle<U> &&other) noexcept
  {
    HandleID other_tid = other.m_target_id.exchange(0, std::memory_order_relaxed);
    if (this->m_target_id.load(std::memory_order_relaxed) != other_tid)
    {
      Release();
      m_target_id.store(other_tid, std::memory_order_relaxed);
    }
    return *this;
  }

  void Release()
  {
    HandleID tid = m_target_id.exchange(0, std::memory_order_relaxed);
    if (tid != 0)
    {
      ork_unregister_weak(tid);
    }
  }

  bool IsAlive() const
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid == 0) return false;
    int32_t alive = 0;
    // Step 1: Probe WITHOUT pruning (perform_pruning = 0)
    if (ork_check_alive(tid, &alive, 0) == ORK_STATUS_OK && alive)
    {
      return true;  // Fast path: object is alive, 0 atomic mutations!
    }

    // Step 2: Object is dead or not found.
    // Atomically claim the right to prune this UnboundHandle instance (CAS tid -> 0).
    if (m_target_id.compare_exchange_strong(tid, 0, std::memory_order_relaxed))
    {
      // We won the race! Call Registry to perform pruning for this UnboundHandle ONCE.
      ork_check_alive(tid, &alive, 1);
    }
    return false;
  }

  template <typename TargetT = T>
  OuroPtr<TargetT> LockAndAcquire() const
  {
    HandleID tid = m_target_id.load(std::memory_order_relaxed);
    if (tid == 0)
    {
      return OuroPtr<TargetT>();
    }

    if (ork_try_lock_weak(tid) == ORK_STATUS_OK)
    {
      return OuroPtr<TargetT>(tid, typename OuroPtr<TargetT>::PreLockedTag{});
    }

    // Object is dead, not found, or destructing/unloading:
    // Atomically claim the right to prune this UnboundHandle instance (CAS tid -> 0).
    if (m_target_id.compare_exchange_strong(tid, 0, std::memory_order_relaxed))
    {
      int32_t alive = 0;
      ork_check_alive(tid, &alive, 1);
    }
    return OuroPtr<TargetT>();
  }

  HandleID GetTargetID() const
  {
    return m_target_id.load(std::memory_order_relaxed);
  }

private:
  mutable std::atomic<HandleID> m_target_id{0};
};

}  // namespace ork

