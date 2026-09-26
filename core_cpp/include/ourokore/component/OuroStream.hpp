#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "ourokore/base/utf8.hpp"

namespace ork
{

namespace detail
{
template <typename KeyT>
inline std::string_view ToKey(const KeyT &key)
{
  return ork::utf8::as_view(key);
}
}  // namespace detail

// --- OuroKore Serialization Exception Hierarchy ---
class OuroSerializationException : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class OuroDuplicateKeyException : public OuroSerializationException
{
public:
  using OuroSerializationException::OuroSerializationException;
};

class OuroKeyMismatchException : public OuroSerializationException
{
public:
  using OuroSerializationException::OuroSerializationException;
};

class OuroCorruptedStreamException : public OuroSerializationException
{
public:
  using OuroSerializationException::OuroSerializationException;
};

/**
 * @brief Stream interface for binary payload serialization in OuroKore.
 * Provides clean key-value WriteProperty and ReadProperty templates for all types.
 */
class OuroStream
{
public:
  virtual ~OuroStream() = default;

  // --- Raw Byte & String I/O (for internal engine use) ---
  virtual void WriteBytes(const uint8_t *buffer, size_t size) = 0;
  virtual void ReadBytes(uint8_t *buffer, size_t size) = 0;

  virtual void WriteStringRaw(const std::string &value) = 0;
  virtual void WriteStringRaw(std::string_view value)
  {
    WriteStringRaw(std::string(value));
  }
  virtual void WriteStringRaw(const char *value)
  {
    WriteStringRaw(std::string_view(value ? value : ""));
  }
  virtual std::string ReadStringRaw() = 0;

  // --- Stream State & Cursor Management ---
  virtual bool HasRemainingBytes() const = 0;
  virtual size_t GetRemainingBytes() const = 0;
  virtual void ResetCursors() = 0;

  // --- Transactional Stream Lifecycle ---
  /**
   * @brief 顯式提交寫入串流 (Commit Transaction)
   * 預設空實作。寫入串流實作端應在 Commit 時將緩衝區寫入底層儲存；
   * 若串流在未呼叫 Commit() 的情況下解構，應視為失敗並自動丟棄緩衝區（Rollback）。
   */
  virtual void Commit() {}

  // --- Key Alignment & Duplicate Guard ---
  virtual void CheckAndRegisterKey(std::string_view key) = 0;
  virtual void VerifyKey(std::string_view expected_key) = 0;
  virtual void ClearDupGuard() = 0;

  // --- Single Universal WriteProperty Template for ALL Types ---
  template <typename KeyT, typename ValueT>
  void WriteProperty(const KeyT &key, const ValueT &value)
  {
    std::string_view k = detail::ToKey(key);
    CheckAndRegisterKey(k);
    WriteStringRaw(k);

    using DecayT = std::decay_t<ValueT>;
    if constexpr (ork::utf8::is_string_like_v<DecayT>)
    {
      WriteStringRaw(ork::utf8::as_view(value));
    }
    else
    {
      // 關鍵防線：嚴格禁止任何非字串指標類型（防止指標位址被當成 POD 寫入）
      static_assert(
          !std::is_pointer_v<DecayT>,
          "OuroStream Error: Raw pointers cannot be serialized directly as properties. "
          "Use OuroPtr/OwningHandle for object references, or string/u8string types for text."
      );
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: WriteProperty value must be trivially copyable (POD/primitive/enum) or string/u8string"
      );
      WriteBytes(reinterpret_cast<const uint8_t *>(&value), sizeof(value));
    }
  }

  // --- Single Universal ReadProperty Out-Parameter Template (Auto Type Deduction) ---
  template <typename KeyT, typename ValueT>
  void ReadProperty(const KeyT &key, ValueT &out_value)
  {
    std::string_view k = detail::ToKey(key);
    VerifyKey(k);

    using DecayT = std::decay_t<ValueT>;
    if constexpr (std::is_same_v<DecayT, std::string>)
    {
      out_value = ReadStringRaw();
    }
    else if constexpr (std::is_same_v<DecayT, std::u8string>)
    {
      std::string s = ReadStringRaw();
      out_value = ork::utf8::to_u8string(s);
    }
    else
    {
      static_assert(
          !std::is_pointer_v<DecayT>,
          "OuroStream Error: Cannot deserialize into raw pointers."
      );
      static_assert(
          !std::is_same_v<DecayT, std::string_view> && !std::is_same_v<DecayT, std::u8string_view>,
          "OuroStream Error: Cannot ReadProperty into string_view because string_view does not own memory. "
          "Use std::string or std::u8string instead."
      );
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: ReadProperty value must be trivially copyable (POD/primitive/enum) or string/u8string"
      );
      ReadBytes(reinterpret_cast<uint8_t *>(&out_value), sizeof(out_value));
    }
  }
};

}  // namespace ork

