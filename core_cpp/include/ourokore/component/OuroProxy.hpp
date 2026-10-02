#pragma once

#include <type_traits>
#include <utility>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"

namespace ork
{

/**
 * @brief OuroProxyBase 安全受管代理基底類別樣板 (Zero Raw Pointer Invariant)
 *
 * 核心設計準則：
 * 1. 內部僅持有 OuroPtr<T>& 引用，絕不持有、快取或外洩任何裸指標 T*。
 * 2. 嚴格不提供 operator-> 或 get()，杜絕脫水懸垂指標 (UAF) 逃逸。
 * 3. 方法調用全數透過 OuroPtr::operator() 成員指標安全轉發，享有 O(1) 極速原生調用與自動透明復水支援。
 * 4. 嚴格禁止從右值臨時 OuroPtr 建構 Proxy，杜絕生命週期脫節導致之懸垂引用。
 */
template <typename T>
class OuroProxyBase
{
public:
  using TargetType = T;
  static_assert(std::is_base_of_v<OuroObject, T>, "Target type T must inherit from ork::OuroObject");

  /// @brief 從合法的左值 OuroPtr<T> 引用構造安全代理
  explicit OuroProxyBase(OuroPtr<T> &ptr) noexcept : m_ptr(ptr) {}

  /// @brief 核心安全防線：嚴格禁止從右值臨時 OuroPtr 建立代理，防範懸垂引用引發 UAF
  explicit OuroProxyBase(OuroPtr<T> &&) = delete;

  virtual ~OuroProxyBase() = default;

  // 複製與移動建構（引用傳遞）
  OuroProxyBase(const OuroProxyBase &) noexcept = default;
  OuroProxyBase(OuroProxyBase &&) noexcept = default;

  // 引用成員不可重新賦值
  OuroProxyBase &operator=(const OuroProxyBase &) = delete;
  OuroProxyBase &operator=(OuroProxyBase &&) = delete;

  /// @brief 取得受託管物件之 64 位元全域唯一 HandleID
  [[nodiscard]] HandleID GetTargetID() const noexcept
  {
    return m_ptr.GetTargetID();
  }

  /// @brief 純記憶體 ControlBlock 存活判定（零 I/O 查詢，脫水狀態下絕不觸發復水）
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return static_cast<bool>(m_ptr);
  }

  /// @brief 純記憶體 ControlBlock 活躍判定（零 I/O 查詢）
  [[nodiscard]] bool IsAlive() const noexcept
  {
    return static_cast<bool>(m_ptr);
  }

  /// @brief 取得受託管物件之全域唯一 TypeID（零 I/O 查詢）
  [[nodiscard]] TypeID GetTypeID() const
  {
    return m_ptr.GetTypeID();
  }

  /// @brief 多型型別檢查（純 ControlBlock 查詢，零 I/O 消耗）
  template <typename TargetT>
  [[nodiscard]] bool Is() const
  {
    return m_ptr.template Is<TargetT>();
  }

  /// @brief 取得底層 OuroPtr 的引用（絕不外洩裸指標 T*）
  [[nodiscard]] OuroPtr<T> &GetPtr() const noexcept
  {
    return m_ptr;
  }

  /// @brief 成員指標安全轉發調用（首次呼叫延遲快取指標，後續為 O(1) 極速原生調用）
  template <typename Fn, typename... Args>
    requires std::is_member_pointer_v<std::decay_t<Fn>>
  decltype(auto) Invoke(Fn &&fn, Args &&...args) const
  {
    return m_ptr(std::forward<Fn>(fn), std::forward<Args>(args)...);
  }

  /// @brief 受信任進階閉包通道（呼叫端需自行確保不得逃逸物件裸指標）
  template <typename Fn, typename... Args>
  decltype(auto) WithObject(Fn &&fn, Args &&...args) const
  {
    return m_ptr.WithObject(std::forward<Fn>(fn), std::forward<Args>(args)...);
  }

protected:
  OuroPtr<T> &m_ptr;
};

/**
 * @brief 預設通用 AsProxy 轉換輔助樣板（適用於未自訂專屬 Proxy 之類型）
 */
template <typename T>
inline OuroProxyBase<T> AsProxy(OuroPtr<T> &ptr)
{
  return OuroProxyBase<T>(ptr);
}

/// @brief 核心安全防線：嚴格禁止對臨時右值呼叫 AsProxy
template <typename T>
void AsProxy(OuroPtr<T> &&) = delete;

}  // namespace ork

// ===================================================================
// X-Macro 單一真實來源 (Single Source of Truth) 代碼生成巨集
// ===================================================================

/**
 * @brief 領域物件實體成員與線程安全 Getter/Setter 生成巨集
 * 包含：
 * - private 屬性欄位宣告與預設值
 * - 自動加 OuroReadLock 共享讀鎖之 Get##name() 方法
 * - 自動加 OuroWriteLock 獨佔寫鎖並於解構時自動原子標記 Dirty 之 Set##name() 方法
 */
#define OURO_GEN_ENTITY_PROPERTY(type, name, default_val) \
private: \
  type m_##name = default_val; \
public: \
  type Get##name() const \
  { \
    ::ork::OuroReadLock lock(*this); \
    return m_##name; \
  } \
  void Set##name(type val) \
  { \
    ::ork::OuroWriteLock lock(*this); \
    m_##name = std::move(val); \
  }

/**
 * @brief 一鍵展開領域物件內的所有屬性定義
 */
#define OURO_GEN_ENTITY_PROPERTIES(PROPERTIES_LIST) \
  PROPERTIES_LIST(OURO_GEN_ENTITY_PROPERTY)

/**
 * @brief 單一屬性序列化生成巨集
 */
#define OURO_GEN_ENTITY_SERIALIZE_PROPERTY(type, name, default_val) \
  stream.WriteProperty(#name, m_##name);

/**
 * @brief 單一屬性反序列化生成巨集
 */
#define OURO_GEN_ENTITY_DESERIALIZE_PROPERTY(type, name, default_val) \
  stream.ReadProperty(#name, m_##name);

/**
 * @brief 一鍵展開領域物件的 SerializePayload 與 DeserializePayload 方法
 * 自動讀寫所有宣告之屬性。（底層序列化與反序列化由核心脫水與復水管道提供線程安全保護）
 */
#define OURO_GEN_ENTITY_SERIALIZATION(PROPERTIES_LIST) \
  void SerializePayload(::ork::OuroStream &stream) const override \
  { \
    PROPERTIES_LIST(OURO_GEN_ENTITY_SERIALIZE_PROPERTY) \
  } \
  void DeserializePayload(::ork::OuroStream &stream) override \
  { \
    PROPERTIES_LIST(OURO_GEN_ENTITY_DESERIALIZE_PROPERTY) \
  }

/**
 * @brief Proxy 類別內的安全轉發方法生成巨集（預設需在類別內宣告 using TargetType = TargetClass;）
 * 呼叫透過 this->GetPtr()(&TargetType::...) 安全轉發，零裸指標暴露。
 */
#define OURO_GEN_PROXY_PROPERTY(type, name, default_val) \
  type Get##name() const \
  { \
    return this->GetPtr()(&TargetType::Get##name); \
  } \
  void Set##name(type val) const \
  { \
    this->GetPtr()(&TargetType::Set##name, std::move(val)); \
  }

/**
 * @brief 顯式指定目標類別之 Proxy 轉發方法生成巨集
 */
#define OURO_GEN_PROXY_PROPERTY_EX(TargetClass, type, name, default_val) \
  type Get##name() const \
  { \
    return this->GetPtr()(&TargetClass::Get##name); \
  } \
  void Set##name(type val) const \
  { \
    this->GetPtr()(&TargetClass::Set##name, std::move(val)); \
  }

/**
 * @brief 一鍵宣告專屬安全 Proxy 類別並註冊 AsProxy 輔助函式
 *
 * 範例：
 *   OURO_DEFINE_PROXY(MonsterProxy, Monster, MONSTER_PROPERTIES)
 */
#define OURO_DEFINE_PROXY(ProxyClassName, TargetClass, PROPERTIES_LIST) \
  class ProxyClassName : public ::ork::OuroProxyBase<TargetClass> \
  { \
  public: \
    using TargetType = TargetClass; \
    using ::ork::OuroProxyBase<TargetClass>::OuroProxyBase; \
    PROPERTIES_LIST(OURO_GEN_PROXY_PROPERTY) \
  }; \
  inline ProxyClassName AsProxy(::ork::OuroPtr<TargetClass> &ptr) \
  { \
    return ProxyClassName(ptr); \
  }

/**
 * @brief 為手動定義擴充的 Proxy 類別註冊專屬 AsProxy 重載函式
 */
#define OURO_REGISTER_PROXY(ProxyClassName, TargetClass) \
  inline ProxyClassName AsProxy(::ork::OuroPtr<TargetClass> &ptr) \
  { \
    return ProxyClassName(ptr); \
  }

/**
 * @brief 在 Proxy 類別內自動生成成員函數安全轉發方法
 *
 * 透過完美轉發 (Perfect Forwarding)，自動支援任意參數型別、參數個數與傳回值型別。
 * 底層透過 this->GetPtr()(&TargetType::MethodName, ...) 成員指標轉發，
 * 享有 O(1) 極速原生調用與透明自動復水，且 100% 零裸指標暴露。
 *
 * 範例：
 *   class BossProxy : public ork::OuroProxyBase<Boss> {
 *   public:
 *     using TargetType = Boss;
 *     using ork::OuroProxyBase<Boss>::OuroProxyBase;
 *
 *     OURO_PROXY_METHOD(Enrage)
 *     OURO_PROXY_METHOD(CalcTotalPower)
 *   };
 */
#define OURO_PROXY_METHOD(MethodName) \
  template <typename... Args> \
  decltype(auto) MethodName(Args &&...args) const \
  { \
    return this->GetPtr()(&TargetType::MethodName, std::forward<Args>(args)...); \
  }

/**
 * @brief 顯式指定目標類別之成員函數安全轉發方法生成巨集
 */
#define OURO_PROXY_METHOD_EX(TargetClass, MethodName) \
  template <typename... Args> \
  decltype(auto) MethodName(Args &&...args) const \
  { \
    return this->GetPtr()(&TargetClass::MethodName, std::forward<Args>(args)...); \
  }
