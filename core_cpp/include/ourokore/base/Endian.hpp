#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

#include "ourokore/base/Result.hpp"

namespace ork::base::endian
{

/**
 * @brief 檢查當前主機硬體架構是否為小端序 (Little-Endian)
 */
constexpr bool is_little_endian() noexcept
{
  return std::endian::native == std::endian::little;
}

/**
 * @brief 檢查當前主機硬體架構是否為大端序 (Big-Endian)
 */
constexpr bool is_big_endian() noexcept
{
  return std::endian::native == std::endian::big;
}

/**
 * @brief 翻轉整數型別之位元組序 (Byte Swap)
 */
template <typename T>
  requires std::integral<T>
constexpr T byteswap(T value) noexcept
{
  return std::byteswap(value);
}

/**
 * @brief 將原生整數轉為小端序 (Little-Endian)
 */
template <typename T>
  requires std::integral<T>
constexpr T to_little_endian(T value) noexcept
{
  if constexpr (is_little_endian())
  {
    return value;
  }
  else
  {
    return byteswap(value);
  }
}

/**
 * @brief 自小端序 (Little-Endian) 解析為原生整數
 */
template <typename T>
  requires std::integral<T>
constexpr T from_little_endian(T value) noexcept
{
  return to_little_endian(value);
}

/**
 * @brief 將原生整數轉為大端序 (Big-Endian)
 */
template <typename T>
  requires std::integral<T>
constexpr T to_big_endian(T value) noexcept
{
  if constexpr (is_big_endian())
  {
    return value;
  }
  else
  {
    return byteswap(value);
  }
}

/**
 * @brief 自大端序 (Big-Endian) 解析為原生整數
 */
template <typename T>
  requires std::integral<T>
constexpr T from_big_endian(T value) noexcept
{
  return to_big_endian(value);
}

// ============================================================================
// 記憶體緩衝區直接讀寫函式 (Direct Buffer I/O - Little-Endian Invariant)
// 確保藍圖與二進位串流無未對齊 (Unaligned Memory Access) 崩潰風險
// ============================================================================

inline void write_uint16_le(uint8_t *dst, uint16_t val) noexcept
{
  uint16_t le = to_little_endian(val);
  std::memcpy(dst, &le, sizeof(uint16_t));
}

inline void write_uint32_le(uint8_t *dst, uint32_t val) noexcept
{
  uint32_t le = to_little_endian(val);
  std::memcpy(dst, &le, sizeof(uint32_t));
}

inline void write_uint64_le(uint8_t *dst, uint64_t val) noexcept
{
  uint64_t le = to_little_endian(val);
  std::memcpy(dst, &le, sizeof(uint64_t));
}

inline void write_int16_le(uint8_t *dst, int16_t val) noexcept
{
  write_uint16_le(dst, static_cast<uint16_t>(val));
}

inline void write_int32_le(uint8_t *dst, int32_t val) noexcept
{
  write_uint32_le(dst, static_cast<uint32_t>(val));
}

inline void write_int64_le(uint8_t *dst, int64_t val) noexcept
{
  write_uint64_le(dst, static_cast<uint64_t>(val));
}

inline void write_float32_le(uint8_t *dst, float val) noexcept
{
  uint32_t raw = 0;
  std::memcpy(&raw, &val, sizeof(float));
  write_uint32_le(dst, raw);
}

inline void write_float64_le(uint8_t *dst, double val) noexcept
{
  uint64_t raw = 0;
  std::memcpy(&raw, &val, sizeof(double));
  write_uint64_le(dst, raw);
}

inline uint16_t read_uint16_le(const uint8_t *src) noexcept
{
  uint16_t raw = 0;
  std::memcpy(&raw, src, sizeof(uint16_t));
  return from_little_endian(raw);
}

inline uint32_t read_uint32_le(const uint8_t *src) noexcept
{
  uint32_t raw = 0;
  std::memcpy(&raw, src, sizeof(uint32_t));
  return from_little_endian(raw);
}

inline uint64_t read_uint64_le(const uint8_t *src) noexcept
{
  uint64_t raw = 0;
  std::memcpy(&raw, src, sizeof(uint64_t));
  return from_little_endian(raw);
}

inline int16_t read_int16_le(const uint8_t *src) noexcept
{
  return static_cast<int16_t>(read_uint16_le(src));
}

inline int32_t read_int32_le(const uint8_t *src) noexcept
{
  return static_cast<int32_t>(read_uint32_le(src));
}

inline int64_t read_int64_le(const uint8_t *src) noexcept
{
  return static_cast<int64_t>(read_uint64_le(src));
}

inline float read_float32_le(const uint8_t *src) noexcept
{
  uint32_t raw = read_uint32_le(src);
  float val = 0.0f;
  std::memcpy(&val, &raw, sizeof(float));
  return val;
}

inline double read_float64_le(const uint8_t *src) noexcept
{
  uint64_t raw = read_uint64_le(src);
  double val = 0.0;
  std::memcpy(&val, &raw, sizeof(double));
  return val;
}

// ============================================================================
// 邊界安全 Span 讀取 (Bounds-Checked Span Reading)
// ============================================================================

template <typename T>
  requires(std::is_arithmetic_v<T>)
Result<T> read_le_checked(std::span<const uint8_t> buffer, size_t offset = 0) noexcept
{
  if (offset + sizeof(T) > buffer.size())
  {
    return unexpected(Status::OutOfRange("緩衝區越界：讀取需求大小超出實際容量"));
  }

  const uint8_t *ptr = buffer.data() + offset;
  if constexpr (std::is_same_v<T, uint16_t>)
  {
    return read_uint16_le(ptr);
  }
  else if constexpr (std::is_same_v<T, int16_t>)
  {
    return read_int16_le(ptr);
  }
  else if constexpr (std::is_same_v<T, uint32_t>)
  {
    return read_uint32_le(ptr);
  }
  else if constexpr (std::is_same_v<T, int32_t>)
  {
    return read_int32_le(ptr);
  }
  else if constexpr (std::is_same_v<T, uint64_t>)
  {
    return read_uint64_le(ptr);
  }
  else if constexpr (std::is_same_v<T, int64_t>)
  {
    return read_int64_le(ptr);
  }
  else if constexpr (std::is_same_v<T, float>)
  {
    return read_float32_le(ptr);
  }
  else if constexpr (std::is_same_v<T, double>)
  {
    return read_float64_le(ptr);
  }
  else if constexpr (sizeof(T) == 1)
  {
    T val{};
    std::memcpy(&val, ptr, 1);
    return val;
  }
  else
  {
    static_assert(sizeof(T) == 0, "不支援的算術型別位元組序解析");
  }
}

}  // namespace ork::base::endian
