#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ourokore/component/OuroStream.hpp"

namespace ork
{

/**
 * @brief 以記憶體為後端的二進位串流實作 (Memory-backed binary stream implementation)
 *
 * 繼承自抽象串流 OuroStream，使用 std::vector<uint8_t> 作為內部緩衝區，
 * 支援循序讀寫游標操作與重複屬性鍵偵測 (Fail-Fast Dup Guard)。
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
    if (static_cast<size_t>(len) > GetRemainingBytes())
    {
      throw OuroCorruptedStreamException(
          "BlueprintStream ReadStringRaw length exceeds remaining stream bytes: corrupted stream or memory bomb detected."
      );
    }
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
