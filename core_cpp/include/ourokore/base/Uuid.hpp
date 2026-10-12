#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "ourokore/base/Result.hpp"
#include "ourokore/base/export.h"
#include "ourokore/base/uuid_api.h"

namespace ork::base
{

/**
 * @brief OuroKore 現代高效能 128 位元 UUID/GUID 工具類別
 *
 * 具備特性：
 * 1. 嚴格符合 RFC 4122 與 RFC 9562 規範。
 * 2. 支援 UUIDv4（密碼學安全隨機數）與 UUIDv7（時間排序型，單調毫秒時序，資料庫與分散式索引友善）。
 * 3. 完整支援 C++23 constexpr 常數評估、編譯期常數字面量 `_uuid` 與三路比較運算子 `<=>`。
 * 4. 提供零配置 (Zero-Allocation) 字串轉換與無例外值語意錯誤處理 `Result<Uuid>`。
 * 5. 底層純 C ABI (`ork_uuid_t`) 互通，跨語言 FFI 友善。
 */
class ORK_BASE_API Uuid
{
public:
  using ByteSpan = std::span<const uint8_t, 16>;

  // =========================================================================
  // 1. 建構子與工廠方法
  // =========================================================================

  /**
   * @brief 預設建構子，產生全 0 的 Nil UUID
   */
  constexpr Uuid() noexcept = default;

  /**
   * @brief 從 16 位元組陣列構造
   */
  constexpr explicit Uuid(const std::array<uint8_t, 16> &bytes) noexcept :
      m_bytes(bytes)
  {
  }

  /**
   * @brief 從 16 位元組固定 span 構造
   */
  constexpr explicit Uuid(std::span<const uint8_t, 16> bytes) noexcept
  {
    for (size_t i = 0; i < 16; ++i)
    {
      m_bytes[i] = bytes[i];
    }
  }

  /**
   * @brief 從原生唯讀位元組指標構造 (長度必須 >= 16)
   */
  explicit Uuid(const uint8_t *bytes) noexcept
  {
    if (bytes)
    {
      for (size_t i = 0; i < 16; ++i)
      {
        m_bytes[i] = bytes[i];
      }
    }
  }

  /**
   * @brief 從高 64 位元與低 64 位元整數構造 (大端序 Big-Endian 佈局)
   */
  constexpr Uuid(uint64_t high_bits, uint64_t low_bits) noexcept
  {
    for (int i = 7; i >= 0; --i)
    {
      m_bytes[i] = static_cast<uint8_t>(high_bits & 0xFF);
      high_bits >>= 8;
    }
    for (int i = 15; i >= 8; --i)
    {
      m_bytes[i] = static_cast<uint8_t>(low_bits & 0xFF);
      low_bits >>= 8;
    }
  }

  /**
   * @brief 從純 C ABI 結構體互轉
   */
  constexpr explicit Uuid(const ork_uuid_t &c_uuid) noexcept
  {
    for (size_t i = 0; i < 16; ++i)
    {
      m_bytes[i] = c_uuid.bytes[i];
    }
  }

  /**
   * @brief 產生全 0 的 Nil UUID (00000000-0000-0000-0000-000000000000)
   */
  static constexpr Uuid Nil() noexcept
  {
    return Uuid{};
  }

  /**
   * @brief 產生全 0xFF 的 Max UUID (ffffffff-ffff-ffff-ffff-ffffffffffff)
   */
  static constexpr Uuid Max() noexcept
  {
    Uuid id;
    for (auto &b : id.m_bytes)
    {
      b = 0xFF;
    }
    return id;
  }

  /**
   * @brief 產生隨機型 UUIDv4 (RFC 4122 / RFC 9562，密碼學安全隨機數)
   */
  static Uuid GenerateV4() noexcept;

  /**
   * @brief 產生時間排序型 UUIDv7 (RFC 9562，Unix 毫秒時間戳 + 嚴格單調計數器 + 安全隨機熵)
   */
  static Uuid GenerateV7() noexcept;

  /**
   * @brief 指定 Unix 紀元毫秒時間戳記產生 UUIDv7 (便於歷史時間序列測試與回溯)
   */
  static Uuid FromUnixTimeMsV7(uint64_t timestamp_ms) noexcept;

  // =========================================================================
  // 2. 狀態與屬性查詢
  // =========================================================================

  /**
   * @brief 判定是否為 Nil UUID (全 0)
   */
  [[nodiscard]] constexpr bool IsNil() const noexcept
  {
    for (uint8_t b : m_bytes)
    {
      if (b != 0) return false;
    }
    return true;
  }

  /**
   * @brief 判定是否為 Max UUID (全 0xFF)
   */
  [[nodiscard]] constexpr bool IsMax() const noexcept
  {
    for (uint8_t b : m_bytes)
    {
      if (b != 0xFF) return false;
    }
    return true;
  }

  /**
   * @brief 布林求值：有效 (非 Nil) 傳回 true
   */
  constexpr explicit operator bool() const noexcept
  {
    return !IsNil();
  }

  /**
   * @brief 取得版本號 (Version: 如 4 或 7)
   */
  [[nodiscard]] constexpr uint8_t Version() const noexcept
  {
    return static_cast<uint8_t>((m_bytes[6] >> 4) & 0x0F);
  }

  /**
   * @brief 取得變體碼 (Variant: RFC 4122/9562 為 1，即高二位為 0b10)
   */
  [[nodiscard]] constexpr uint8_t Variant() const noexcept
  {
    uint8_t b = m_bytes[8];
    if ((b & 0x80) == 0x00) return 0;       // NCS
    if ((b & 0xC0) == 0x80) return 1;       // RFC 4122 / 9562
    if ((b & 0xE0) == 0xC0) return 2;       // Microsoft GUID
    return 3;                               // Reserved
  }

  /**
   * @brief 提取 UUIDv7 之 Unix 紀元毫秒時間戳記 (前 48 位元)
   */
  [[nodiscard]] constexpr uint64_t TimestampMs() const noexcept
  {
    return (static_cast<uint64_t>(m_bytes[0]) << 40) |
           (static_cast<uint64_t>(m_bytes[1]) << 32) |
           (static_cast<uint64_t>(m_bytes[2]) << 24) |
           (static_cast<uint64_t>(m_bytes[3]) << 16) |
           (static_cast<uint64_t>(m_bytes[4]) << 8)  |
           (static_cast<uint64_t>(m_bytes[5]));
  }

  /**
   * @brief 取得前 64 位元整數 (Big-Endian)
   */
  [[nodiscard]] constexpr uint64_t HighBits() const noexcept
  {
    uint64_t val = 0;
    for (size_t i = 0; i < 8; ++i)
    {
      val = (val << 8) | m_bytes[i];
    }
    return val;
  }

  /**
   * @brief 取得後 64 位元整數 (Big-Endian)
   */
  [[nodiscard]] constexpr uint64_t LowBits() const noexcept
  {
    uint64_t val = 0;
    for (size_t i = 8; i < 16; ++i)
    {
      val = (val << 8) | m_bytes[i];
    }
    return val;
  }

  // =========================================================================
  // 3. 記憶體位元組存取與 C ABI 互轉
  // =========================================================================

  [[nodiscard]] constexpr const std::array<uint8_t, 16> &Bytes() const noexcept
  {
    return m_bytes;
  }

  [[nodiscard]] constexpr const uint8_t *Data() const noexcept
  {
    return m_bytes.data();
  }

  [[nodiscard]] constexpr uint8_t *Data() noexcept
  {
    return m_bytes.data();
  }

  [[nodiscard]] static constexpr size_t Size() noexcept
  {
    return 16;
  }

  [[nodiscard]] constexpr ByteSpan AsSpan() const noexcept
  {
    return m_bytes;
  }

  [[nodiscard]] constexpr uint8_t operator[](size_t index) const noexcept
  {
    return m_bytes[index];
  }

  [[nodiscard]] constexpr uint8_t &operator[](size_t index) noexcept
  {
    return m_bytes[index];
  }

  [[nodiscard]] constexpr ork_uuid_t ToCApi() const noexcept
  {
    ork_uuid_t c_uuid{};
    for (size_t i = 0; i < 16; ++i)
    {
      c_uuid.bytes[i] = m_bytes[i];
    }
    return c_uuid;
  }

  // =========================================================================
  // 4. 字串格式化 (Serialization)
  // =========================================================================

  /**
   * @brief 輸出為標準 36 字元橫線格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
   */
  [[nodiscard]] std::string ToString(bool uppercase = false) const
  {
    std::string s(36, '\0');
    ToChars(s.data(), uppercase);
    return s;
  }

  /**
   * @brief 輸出為 32 字元緊湊格式 (無橫線)：xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
   */
  [[nodiscard]] std::string ToCompactString(bool uppercase = false) const
  {
    std::string s(32, '\0');
    ToCompactChars(s.data(), uppercase);
    return s;
  }

  /**
   * @brief 輸出為大括號包裹格式：{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
   */
  [[nodiscard]] std::string ToBracedString(bool uppercase = false) const
  {
    std::string s(38, '\0');
    s[0] = '{';
    ToChars(s.data() + 1, uppercase);
    s[37] = '}';
    return s;
  }

  /**
   * @brief 零配置寫入 36 字元至外部緩衝區 (不含結尾 null)
   */
  constexpr void ToChars(char *out_36, bool uppercase = false) const noexcept
  {
    const char *hex_table = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t out_idx = 0;
    for (size_t i = 0; i < 16; ++i)
    {
      if (i == 4 || i == 6 || i == 8 || i == 10)
      {
        out_36[out_idx++] = '-';
      }
      uint8_t byte = m_bytes[i];
      out_36[out_idx++] = hex_table[(byte >> 4) & 0x0F];
      out_36[out_idx++] = hex_table[byte & 0x0F];
    }
  }

  /**
   * @brief 零配置寫入 32 字元無橫線至外部緩衝區 (不含結尾 null)
   */
  constexpr void ToCompactChars(char *out_32, bool uppercase = false) const noexcept
  {
    const char *hex_table = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t out_idx = 0;
    for (size_t i = 0; i < 16; ++i)
    {
      uint8_t byte = m_bytes[i];
      out_32[out_idx++] = hex_table[(byte >> 4) & 0x0F];
      out_32[out_idx++] = hex_table[byte & 0x0F];
    }
  }

  /**
   * @brief 零配置回傳 36 字元靜態陣列 (constexpr)
   */
  [[nodiscard]] constexpr std::array<char, 36> ToCharArray(bool uppercase = false) const noexcept
  {
    std::array<char, 36> arr{};
    ToChars(arr.data(), uppercase);
    return arr;
  }

  // =========================================================================
  // 5. 字串解析 (Deserialization)
  // =========================================================================

  /**
   * @brief 從字串解析 UUID (支援標準 36 字元、32 字元緊湊、38 字元大括號，大小寫無關)
   * 採用現代值語意 Result<Uuid> 傳回解析結果或錯誤資訊。
   */
  static Result<Uuid> FromString(std::string_view str) noexcept
  {
    auto opt = ParseConstexpr(str);
    if (!opt.has_value())
    {
      return std::unexpected(Status::InvalidArgument("Invalid UUID string format or character"));
    }
    return *opt;
  }

  /**
   * @brief 嘗試解析 UUID，失敗傳回 std::nullopt (noexcept)
   */
  static std::optional<Uuid> TryParse(std::string_view str) noexcept
  {
    return ParseConstexpr(str);
  }

  /**
   * @brief 編譯期常數解析 UUID (constexpr)
   */
  static constexpr std::optional<Uuid> ParseConstexpr(std::string_view str) noexcept
  {
    // 去除大括號 { ... }
    if (str.size() == 38 && str.front() == '{' && str.back() == '}')
    {
      str = str.substr(1, 36);
    }

    auto hex_val = [](char c) constexpr -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };

    Uuid result;

    if (str.size() == 36)
    {
      // 驗證分隔符號位置：8-4-4-4-12
      if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
      {
        return std::nullopt;
      }

      size_t byte_idx = 0;
      for (size_t i = 0; i < 36; ++i)
      {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
          continue;
        }

        int h1 = hex_val(str[i]);
        if (h1 < 0 || i + 1 >= 36) return std::nullopt;
        int h2 = hex_val(str[++i]);
        if (h2 < 0) return std::nullopt;

        result.m_bytes[byte_idx++] = static_cast<uint8_t>((h1 << 4) | h2);
      }
      return result;
    }
    else if (str.size() == 32)
    {
      // 緊湊格式：32 個十六進位字元
      for (size_t i = 0; i < 16; ++i)
      {
        int h1 = hex_val(str[i * 2]);
        int h2 = hex_val(str[i * 2 + 1]);
        if (h1 < 0 || h2 < 0) return std::nullopt;
        result.m_bytes[i] = static_cast<uint8_t>((h1 << 4) | h2);
      }
      return result;
    }

    return std::nullopt;
  }

  // =========================================================================
  // 6. 運算子重載 (三路比較運算子)
  // =========================================================================

  constexpr auto operator<=>(const Uuid &other) const noexcept = default;
  constexpr bool operator==(const Uuid &other) const noexcept = default;

private:
  alignas(8) std::array<uint8_t, 16> m_bytes{};
};

// =========================================================================
// 7. 使用者自訂常數後綴字面量 (User-Defined Literals)
// =========================================================================

namespace literals::uuid_literals
{

namespace detail
{
[[noreturn]] inline void ThrowCompileTimeUuidError()
{
  // 於編譯期引發非 constexpr 呼叫以觸發靜態編譯斷言
  struct CompileTimeUuidError {};
  throw CompileTimeUuidError{};
}
}  // namespace detail

/**
 * @brief 編譯期 UUID 字面量運算子，例如 "018f2f4c-5353-7b3b-8105-df4d3855a02e"_uuid
 */
constexpr Uuid operator""_uuid(const char *str, size_t len)
{
  auto opt = Uuid::ParseConstexpr(std::string_view(str, len));
  if (!opt.has_value())
  {
    detail::ThrowCompileTimeUuidError();
  }
  return *opt;
}

}  // namespace literals::uuid_literals

}  // namespace ork::base

// =========================================================================
// 8. STL 擴充特化：std::hash 與 std::formatter
// =========================================================================

template <>
struct std::hash<ork::base::Uuid>
{
  size_t operator()(const ork::base::Uuid &id) const noexcept
  {
    // 結合高 64 位元與低 64 位元雜湊
    uint64_t h = id.HighBits();
    uint64_t l = id.LowBits();
    if constexpr (sizeof(size_t) >= 8)
    {
      return static_cast<size_t>(h ^ (l + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
    }
    else
    {
      uint32_t h32 = static_cast<uint32_t>(h ^ (h >> 32));
      uint32_t l32 = static_cast<uint32_t>(l ^ (l >> 32));
      return static_cast<size_t>(h32 ^ (l32 + 0x9e3779b9U + (h32 << 6) + (h32 >> 2)));
    }
  }
};

template <>
struct std::formatter<ork::base::Uuid> : std::formatter<std::string_view>
{
  char spec = 's';  // 預設標準小寫

  constexpr auto parse(std::format_parse_context &ctx)
  {
    auto it = ctx.begin();
    if (it != ctx.end() && *it != '}')
    {
      spec = *it++;
    }
    return it;
  }

  auto format(const ork::base::Uuid &id, std::format_context &ctx) const
  {
    std::string s;
    switch (spec)
    {
      case 'S': s = id.ToString(true); break;
      case 'c': s = id.ToCompactString(false); break;
      case 'C': s = id.ToCompactString(true); break;
      case 'b': s = id.ToBracedString(false); break;
      case 'B': s = id.ToBracedString(true); break;
      default:  s = id.ToString(false); break;
    }
    return std::formatter<std::string_view>::format(s, ctx);
  }
};
