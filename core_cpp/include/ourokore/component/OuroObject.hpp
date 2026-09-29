#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

#include "OuroStream.hpp"
#include "Types.hpp"
#include "ourokore/base/Hash.hpp"
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

class OuroObject;

namespace detail
{
template <typename T>
class Rehydrator;

template <typename T>
constexpr std::string_view ExtractRawSignature() noexcept
{
#if defined(_MSC_VER)
  return __FUNCSIG__;
#elif defined(__clang__) || defined(__GNUC__)
  return __PRETTY_FUNCTION__;
#else
  return "UnknownType";
#endif
}

template <typename T>
constexpr std::string_view ExtractCleanTypeName() noexcept
{
  std::string_view sig = ExtractRawSignature<T>();
#if defined(_MSC_VER)
  // Format: ...ExtractRawSignature<class Foo>(void) or ...<struct Foo>(void)
  std::string_view pattern = "ExtractRawSignature<";
  size_t start = sig.find(pattern);
  if (start == std::string_view::npos) return sig;
  start += pattern.size();
  size_t end = sig.rfind('>');
  if (end == std::string_view::npos || end <= start) return sig;
  std::string_view inner = sig.substr(start, end - start);
  if (inner.starts_with("class ")) inner.remove_prefix(6);
  else if (inner.starts_with("struct ")) inner.remove_prefix(7);
  size_t last_colons = inner.rfind("::");
  if (last_colons != std::string_view::npos)
  {
    inner.remove_prefix(last_colons + 2);
  }
  return inner;
#elif defined(__clang__)
  // Format: ... [T = Foo]
  std::string_view pattern = "[T = ";
  size_t start = sig.find(pattern);
  if (start == std::string_view::npos) return sig;
  start += pattern.size();
  size_t end = sig.find(']', start);
  if (end == std::string_view::npos || end <= start) return sig;
  std::string_view inner = sig.substr(start, end - start);
  if (inner.starts_with("class ")) inner.remove_prefix(6);
  else if (inner.starts_with("struct ")) inner.remove_prefix(7);
  size_t last_colons = inner.rfind("::");
  if (last_colons != std::string_view::npos)
  {
    inner.remove_prefix(last_colons + 2);
  }
  return inner;
#elif defined(__GNUC__)
  // Format: ... [with T = Foo; ...]
  std::string_view pattern = "[with T = ";
  size_t start = sig.find(pattern);
  if (start == std::string_view::npos) return sig;
  start += pattern.size();
  size_t end = sig.find_first_of(";]", start);
  if (end == std::string_view::npos || end <= start) return sig;
  std::string_view inner = sig.substr(start, end - start);
  if (inner.starts_with("class ")) inner.remove_prefix(6);
  else if (inner.starts_with("struct ")) inner.remove_prefix(7);
  size_t last_colons = inner.rfind("::");
  if (last_colons != std::string_view::npos)
  {
    inner.remove_prefix(last_colons + 2);
  }
  return inner;
#else
  return "UnknownType";
#endif
}

template <typename T>
struct TypeNameStorage
{
  static constexpr auto MakeStorage()
  {
    constexpr std::string_view sv = ExtractCleanTypeName<T>();
    std::array<char, sv.size() + 1> arr{};
    for (size_t i = 0; i < sv.size(); ++i)
    {
      arr[i] = sv[i];
    }
    arr[sv.size()] = '\0';
    return arr;
  }
  static constexpr auto s_storage = MakeStorage();
  static constexpr const char *value = s_storage.data();
};

inline TypeID RegisterTypeHelper(TypeID type_id, const char *name_utf8, TypeID parent_type_id)
{
  ork_register_type(type_id, name_utf8, parent_type_id);
  return type_id;
}

template <typename T, typename = void>
struct TypeTraits
{
  static TypeID GetTypeID()
  {
    if constexpr (requires { T::StaticTypeID(); })
    {
      return T::StaticTypeID();
    }
    else
    {
      static const TypeID s_id = RegisterTypeHelper(
          ::ork::base::Fnv1a64(typeid(T).name()), typeid(T).name(), ::ork::base::Fnv1a64("OuroObject"));
      return s_id;
    }
  }

  static const char *GetTypeName()
  {
    if constexpr (requires { T::StaticTypeName(); })
    {
      return T::StaticTypeName();
    }
    else
    {
      return typeid(T).name();
    }
  }
};
}  // namespace detail

/**
 * @brief Base class for all managed objects in OuroKore.
 * All concrete components must inherit from this class.
 */
class OuroObject
{
public:
  using ThisClass = OuroObject;
  using SuperClass = void;
  static constexpr const char *StaticTypeName() noexcept { return "OuroObject"; }
  static TypeID StaticTypeID() noexcept
  {
    static const TypeID s_type_id = ::ork::base::Fnv1a64("OuroObject");
    return s_type_id;
  }

  /**
   * @brief Gets runtime TypeID of this object.
   */
  virtual TypeID GetTypeID() const
  {
    if (m_object_id != 0)
    {
      ork_type_id_t tid = 0;
      if (ork_get_object_type(m_object_id, &tid) == ORK_STATUS_OK && tid != 0)
      {
        return tid;
      }
    }
    return StaticTypeID();
  }

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

/**
 * @brief CRTP 受管衍生類別基底（方案 A：免巨集自動型別系統）
 * 
 * 透過 CRTP 自動繼承 Base 類別，在編譯期自動萃取型別名稱並向核心型別登錄系統註冊繼承關係。
 * 使用者類別體內完全無需撰寫任何巨集即可具備完整 RTTI、多型轉型（Is<T> / As<T>）以及安全序列化支援。
 * 
 * 支援多層繼承（如孫類別、曾孫類別），並支援帶參數建構子之完美轉發。
 * 
 * 範例：
 *   class Creature : public ork::Subclass<Creature, ork::OuroObject> { ... };
 *   class Monster : public ork::Subclass<Monster, Creature> { ... };
 *   class BossMonster : public ork::Subclass<BossMonster, Monster> { ... };
 */
template <typename Derived, typename Base = OuroObject>
class Subclass : public Base
{
public:
  using ThisClass = Derived;
  using SuperClass = Base;

  static_assert(std::is_base_of_v<OuroObject, Base>, "Base must inherit from ork::OuroObject");

  Subclass() = default;

  template <typename... Args>
    requires(sizeof...(Args) > 0 && std::is_constructible_v<Base, Args...>)
  explicit Subclass(Args &&...args)
      : Base(std::forward<Args>(args)...)
  {
  }

  static constexpr const char *StaticTypeName() noexcept
  {
    if constexpr (requires { Derived::CustomTypeName(); })
    {
      return Derived::CustomTypeName();
    }
    else
    {
      return detail::TypeNameStorage<Derived>::value;
    }
  }

  static TypeID StaticTypeID()
  {
    static_assert(std::is_base_of_v<Subclass<Derived, Base>, Derived>,
                  "Derived class must inherit from Subclass<Derived, Base>");
    static const TypeID s_type_id = detail::RegisterTypeHelper(
        base::Fnv1a64(StaticTypeName()), StaticTypeName(), detail::TypeTraits<Base>::GetTypeID());
    return s_type_id;
  }

  TypeID GetTypeID() const override
  {
    return StaticTypeID();
  }
};

}  // namespace ork


