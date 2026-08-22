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

/**
 * @brief Stream interface for binary payload serialization in OuroKore.
 * Provides clean key-value WriteProperty and ReadProperty templates for all types.
 */
class OuroStream
{
protected:
  std::unordered_set<std::string> m_written_keys;

public:
  virtual ~OuroStream() = default;

  // --- Raw Byte & String I/O (for internal engine use) ---
  virtual int32_t WriteBytes(const uint8_t *buffer, size_t size) = 0;
  virtual int32_t ReadBytes(uint8_t *buffer, size_t size) = 0;

  virtual int32_t WriteStringRaw(const std::string &value) = 0;
  virtual std::string ReadStringRaw() = 0;

  virtual void CheckAndRegisterKey(std::string_view key) = 0;
  virtual void VerifyKey(std::string_view expected_key) = 0;

  // --- Single Universal WriteProperty Template for ALL Types ---
  template <typename KeyT, typename ValueT>
  int32_t WriteProperty(const KeyT &key, const ValueT &value)
  {
    std::string_view k = detail::ToKey(key);
    CheckAndRegisterKey(k);
    WriteStringRaw(std::string(k));

    using DecayT = std::decay_t<ValueT>;
    if constexpr (std::is_same_v<DecayT, std::string>)
    {
      return WriteStringRaw(value);
    }
    else if constexpr (std::is_same_v<DecayT, const char *> || std::is_same_v<DecayT, char *>)
    {
      return WriteStringRaw(std::string(value));
    }
    else
    {
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: WriteProperty value must be trivially copyable (POD/primitive/enum) or std::string"
      );
      return WriteBytes(reinterpret_cast<const uint8_t *>(&value), sizeof(value));
    }
  }

  // --- Single Universal ReadProperty Out-Parameter Template (Auto Type Deduction) ---
  template <typename KeyT, typename ValueT>
  int32_t ReadProperty(const KeyT &key, ValueT &out_value)
  {
    std::string_view k = detail::ToKey(key);
    VerifyKey(k);

    using DecayT = std::decay_t<ValueT>;
    if constexpr (std::is_same_v<DecayT, std::string>)
    {
      out_value = ReadStringRaw();
      return 0;
    }
    else
    {
      static_assert(
          std::is_trivially_copyable_v<DecayT>,
          "OuroStream Error: ReadProperty value must be trivially copyable (POD/primitive/enum) or std::string"
      );
      return ReadBytes(reinterpret_cast<uint8_t *>(&out_value), sizeof(out_value));
    }
  }

  void ClearDupGuard() { m_written_keys.clear(); }
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

public:
  BlueprintStream() = default;

  explicit BlueprintStream(std::vector<uint8_t> buffer)
      : m_buffer(std::move(buffer)), m_read_cursor(0), m_write_cursor(m_buffer.size())
  {
  }

  const std::vector<uint8_t> &GetBuffer() const { return m_buffer; }
  size_t GetSize() const { return m_buffer.size(); }
  void ResetCursors()
  {
    m_read_cursor = 0;
    m_write_cursor = 0;
  }

  int32_t WriteBytes(const uint8_t *buffer, size_t size) override
  {
    if (!buffer || size == 0) return 0;
    if (m_write_cursor + size > m_buffer.size())
    {
      m_buffer.resize(m_write_cursor + size);
    }
    std::memcpy(m_buffer.data() + m_write_cursor, buffer, size);
    m_write_cursor += size;
    return 0;
  }

  int32_t ReadBytes(uint8_t *buffer, size_t size) override
  {
    if (!buffer || size == 0) return 0;
    if (m_read_cursor + size > m_buffer.size())
    {
      throw std::out_of_range("BlueprintStream ReadBytes out of range.");
    }
    std::memcpy(buffer, m_buffer.data() + m_read_cursor, size);
    m_read_cursor += size;
    return 0;
  }

  int32_t WriteStringRaw(const std::string &value) override
  {
    uint32_t len = static_cast<uint32_t>(value.size());
    WriteBytes(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
    if (len > 0)
    {
      WriteBytes(reinterpret_cast<const uint8_t *>(value.data()), len);
    }
    return 0;
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
      throw std::runtime_error("OuroKore Fail-Fast: Duplicate property key detected: " + k);
    }
    m_written_keys.insert(k);
  }

  void VerifyKey(std::string_view expected_key) override
  {
    std::string actual_key = ReadStringRaw();
    if (actual_key != expected_key)
    {
      throw std::runtime_error(
          "OuroKore Fail-Fast: Mismatched property key in stream. Expected '" +
          std::string(expected_key) + "', but got '" + actual_key + "'"
      );
    }
  }
};

}  // namespace ork
