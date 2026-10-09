#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace ork::utf8
{

/**
 * @brief 將各類字串、字串視圖、字面量（包含 C++20 char8_t 與 UTF-8）零拷貝統一轉換為 std::string_view
 * 支援類型：
 * 1. std::string, std::string_view, const char*, char*
 * 2. std::u8string, std::u8string_view, const char8_t*, char8_t*
 */
template <typename T>
constexpr std::string_view as_view(const T &str) noexcept
{
  using DecayT = std::decay_t<T>;
  if constexpr (std::is_convertible_v<T, std::string_view>)
  {
    return std::string_view(str);
  }
  else if constexpr (std::is_same_v<DecayT, const char8_t *> || std::is_same_v<DecayT, char8_t *>)
  {
    return str ? std::string_view(reinterpret_cast<const char *>(str)) : std::string_view();
  }
  else if constexpr (std::is_convertible_v<T, std::u8string_view>)
  {
    std::u8string_view u8v(str);
    return std::string_view(reinterpret_cast<const char *>(u8v.data()), u8v.size());
  }
  else
  {
    return std::string_view(reinterpret_cast<const char *>(str));
  }
}

/**
 * @brief 將各類字串、字面量或 UTF-8 視圖統一轉換為 std::string
 */
template <typename T>
inline std::string to_string(const T &str)
{
  std::string_view v = as_view(str);
  return std::string(v.data(), v.size());
}

/**
 * @brief 將 std::string_view 轉換為 C++20 原生 std::u8string
 */
inline std::u8string to_u8string(std::string_view v)
{
  return std::u8string(reinterpret_cast<const char8_t *>(v.data()), v.size());
}

/**
 * @brief 判定特定型別是否為字串或字串視圖型別（含 UTF-8 / char8_t 系列）
 */
template <typename T>
struct is_string_like : std::false_type {};

template <> struct is_string_like<std::string> : std::true_type {};
template <> struct is_string_like<std::u8string> : std::true_type {};
template <> struct is_string_like<std::string_view> : std::true_type {};
template <> struct is_string_like<std::u8string_view> : std::true_type {};
template <> struct is_string_like<const char *> : std::true_type {};
template <> struct is_string_like<char *> : std::true_type {};
template <> struct is_string_like<const char8_t *> : std::true_type {};
template <> struct is_string_like<char8_t *> : std::true_type {};

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<std::decay_t<T>>::value;

/**
 * @brief 判定字串內容是否為嚴格合規的 UTF-8 位元組序列（依據 RFC 3629）
 *
 * 嚴格排除：
 * 1. 非法前導位元組（0xC0, 0xC1, 0xF5..0xFF）
 * 2. 超長編碼（Overlong encoding）
 * 3. UTF-16 代理字元區段（Surrogate halves: U+D800..U+DFFF, 0xED 0xA0..0xBF）
 * 4. 超出 Unicode 最大碼點（> U+10FFFF, 0xF4 0x90..0xBF）
 * 5. 截斷或不完整之多位元組序列
 */
template <typename T>
constexpr bool is_valid(const T &str) noexcept
{
  std::string_view v = as_view(str);
  const auto *bytes = reinterpret_cast<const uint8_t *>(v.data());
  const size_t len = v.size();
  size_t i = 0;

  while (i < len)
  {
    uint8_t b1 = bytes[i++];
    if (b1 <= 0x7F)
    {
      // 1 位元組 (ASCII: 0x00..0x7F)
      continue;
    }
    else if (b1 >= 0xC2 && b1 <= 0xDF)
    {
      // 2 位元組 (0xC2..0xDF 0x80..0xBF)
      if (i >= len) return false;
      uint8_t b2 = bytes[i++];
      if ((b2 & 0xC0) != 0x80) return false;
    }
    else if (b1 >= 0xE0 && b1 <= 0xEF)
    {
      // 3 位元組
      if (i + 1 >= len) return false;
      uint8_t b2 = bytes[i++];
      uint8_t b3 = bytes[i++];
      if ((b3 & 0xC0) != 0x80) return false;

      if (b1 == 0xE0)
      {
        // 排除超長編碼 (b2 必須 >= 0xA0)
        if (b2 < 0xA0 || b2 > 0xBF) return false;
      }
      else if (b1 == 0xED)
      {
        // 排除 UTF-16 代理字元 U+D800..U+DFFF (b2 必須 <= 0x9F)
        if (b2 < 0x80 || b2 > 0x9F) return false;
      }
      else
      {
        if ((b2 & 0xC0) != 0x80) return false;
      }
    }
    else if (b1 >= 0xF0 && b1 <= 0xF4)
    {
      // 4 位元組
      if (i + 2 >= len) return false;
      uint8_t b2 = bytes[i++];
      uint8_t b3 = bytes[i++];
      uint8_t b4 = bytes[i++];
      if ((b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80) return false;

      if (b1 == 0xF0)
      {
        // 排除超長編碼 (b2 必須 >= 0x90)
        if (b2 < 0x90 || b2 > 0xBF) return false;
      }
      else if (b1 == 0xF4)
      {
        // 排除超出 U+10FFFF (b2 必須 <= 0x8F)
        if (b2 < 0x80 || b2 > 0x8F) return false;
      }
      else
      {
        if ((b2 & 0xC0) != 0x80) return false;
      }
    }
    else
    {
      // 非法前導位元組 (0x80..0xC1 或 0xF5..0xFF)
      return false;
    }
  }

  return true;
}

}  // namespace ork::utf8
