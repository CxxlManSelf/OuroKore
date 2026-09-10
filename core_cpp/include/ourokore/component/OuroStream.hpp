#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace ork
{

namespace detail
{
template <typename KeyT>
inline std::string_view ToKey(const KeyT &key)
{
  if constexpr (std::is_convertible_v<KeyT, std::string_view>)
  {
    return std::string_view(key);
  }
  else
  {
    return std::string_view(reinterpret_cast<const char *>(key));
  }
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
    WriteStringRaw(std::string(k));

    using DecayT = std::decay_t<ValueT>;
    if constexpr (std::is_same_v<DecayT, std::string>)
    {
      WriteStringRaw(value);
    }
    else if constexpr (std::is_same_v<DecayT, const char *> || std::is_same_v<DecayT, char *>)
    {
      WriteStringRaw(std::string(value));
    }
    else
    {
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: WriteProperty value must be trivially copyable (POD/primitive/enum) or std::string"
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
    else
    {
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: ReadProperty value must be trivially copyable (POD/primitive/enum) or std::string"
      );
      ReadBytes(reinterpret_cast<uint8_t *>(&out_value), sizeof(out_value));
    }
  }
};

/**
 * @brief Memory-backed binary stream implementation.
 */
class BlueprintStream : public OuroStream
{
private:
  std::vector<uint8_t> m_buffer;
  size_t m_read_cursor = 0;
  size_t m_write_cursor = 0;
  std::unordered_set<std::string> m_written_keys;

public:
  BlueprintStream() = default;

  explicit BlueprintStream(std::vector<uint8_t> buffer)
      : m_buffer(std::move(buffer)), m_read_cursor(0), m_write_cursor(m_buffer.size())
  {
  }

  const std::vector<uint8_t> &GetBuffer() const { return m_buffer; }
  size_t GetSize() const { return m_buffer.size(); }

  void ResetCursors() override
  {
    m_read_cursor = 0;
    m_write_cursor = 0;
  }

  void ClearDupGuard() override
  {
    m_written_keys.clear();
  }

  bool HasRemainingBytes() const override
  {
    return m_read_cursor < m_buffer.size();
  }

  size_t GetRemainingBytes() const override
  {
    return (m_read_cursor < m_buffer.size()) ? (m_buffer.size() - m_read_cursor) : 0;
  }

  void WriteBytes(const uint8_t *buffer, size_t size) override
  {
    if (!buffer || size == 0) return;
    if (m_write_cursor + size > m_buffer.size())
    {
      m_buffer.resize(m_write_cursor + size);
    }
    std::memcpy(m_buffer.data() + m_write_cursor, buffer, size);
    m_write_cursor += size;
  }

  void ReadBytes(uint8_t *buffer, size_t size) override
  {
    if (!buffer || size == 0) return;
    if (m_read_cursor + size > m_buffer.size())
    {
      throw OuroCorruptedStreamException("BlueprintStream ReadBytes out of range: corrupted or truncated stream.");
    }
    std::memcpy(buffer, m_buffer.data() + m_read_cursor, size);
    m_read_cursor += size;
  }

  void WriteStringRaw(const std::string &value) override
  {
    uint32_t len = static_cast<uint32_t>(value.size());
    WriteBytes(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
    if (len > 0)
    {
      WriteBytes(reinterpret_cast<const uint8_t *>(value.data()), len);
    }
  }

  std::string ReadStringRaw() override
  {
    uint32_t len = 0;
    ReadBytes(reinterpret_cast<uint8_t *>(&len), sizeof(len));
    if (len == 0) return "";
    std::string str(len, '\0');
    ReadBytes(reinterpret_cast<uint8_t *>(str.data()), len);
    return str;
  }

  void CheckAndRegisterKey(std::string_view key) override
  {
    std::string k(key);
    if (m_written_keys.find(k) != m_written_keys.end())
    {
      throw OuroDuplicateKeyException("OuroKore Fail-Fast: Duplicate property key detected: " + k);
    }
    m_written_keys.insert(k);
  }

  void VerifyKey(std::string_view expected_key) override
  {
    std::string actual_key = ReadStringRaw();
    if (actual_key != expected_key)
    {
      throw OuroKeyMismatchException(
          "OuroKore Fail-Fast: Mismatched property key in stream. Expected '" +
          std::string(expected_key) + "', but got '" + actual_key + "'"
      );
    }
  }
};

}  // namespace ork
