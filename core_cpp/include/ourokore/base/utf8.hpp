#pragma once

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

}  // namespace ork::utf8
