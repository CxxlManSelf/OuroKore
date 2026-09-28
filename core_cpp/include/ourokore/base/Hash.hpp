#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ork::base
{

/**
 * @brief OuroKore 現代高效能雜湊工具庫 (Hash Utilities)
 * 提供編譯期常數計算 (constexpr) 與執行期通用雜湊演算法：
 * - FNV-1a (32-bit / 64-bit)：極簡極速、編譯期最佳化、型別識別碼推薦。
 * - CRC32 (IEEE 802.3)：標準資料完整性校驗與二進位串流防竄改。
 * - MurmurHash3 (32-bit)：具備優秀雪崩效應 (Avalanche Effect) 的區塊雜湊。
 * - HashCombine：組合式雜湊運算。
 * - 使用者自訂常數後綴字面量 (User-defined Literals)。
 */
namespace hash
{

// =========================================================================
// 1. FNV-1a 雜湊 (Fowler–Noll–Vo 1a)
// =========================================================================

namespace detail
{
inline constexpr uint32_t kFnv1a32Prime = 16777619U;
inline constexpr uint32_t kFnv1a32OffsetBasis = 2166136261U;

inline constexpr uint64_t kFnv1a64Prime = 1099511628211ULL;
inline constexpr uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
}  // namespace detail

/**
 * @brief 32 位元 FNV-1a 雜湊運算 (constexpr)
 */
constexpr uint32_t Fnv1a32(const uint8_t *bytes, size_t size) noexcept
{
  uint32_t hash = detail::kFnv1a32OffsetBasis;
  for (size_t i = 0; i < size; ++i)
  {
    hash ^= static_cast<uint32_t>(bytes[i]);
    hash *= detail::kFnv1a32Prime;
  }
  return hash;
}

constexpr uint32_t Fnv1a32(std::string_view str) noexcept
{
  uint32_t hash = detail::kFnv1a32OffsetBasis;
  for (char c : str)
  {
    hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
    hash *= detail::kFnv1a32Prime;
  }
  return hash;
}

constexpr uint32_t Fnv1a32(std::span<const uint8_t> bytes) noexcept
{
  return Fnv1a32(bytes.data(), bytes.size());
}

inline uint32_t Fnv1a32(const void *data, size_t size) noexcept
{
  return Fnv1a32(static_cast<const uint8_t *>(data), size);
}

/**
 * @brief 64 位元 FNV-1a 雜湊運算 (constexpr)
 */
constexpr uint64_t Fnv1a64(const uint8_t *bytes, size_t size) noexcept
{
  uint64_t hash = detail::kFnv1a64OffsetBasis;
  for (size_t i = 0; i < size; ++i)
  {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= detail::kFnv1a64Prime;
  }
  return hash;
}

constexpr uint64_t Fnv1a64(std::string_view str) noexcept
{
  uint64_t hash = detail::kFnv1a64OffsetBasis;
  for (char c : str)
  {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
    hash *= detail::kFnv1a64Prime;
  }
  return hash;
}

constexpr uint64_t Fnv1a64(std::span<const uint8_t> bytes) noexcept
{
  return Fnv1a64(bytes.data(), bytes.size());
}

inline uint64_t Fnv1a64(const void *data, size_t size) noexcept
{
  return Fnv1a64(static_cast<const uint8_t *>(data), size);
}

// =========================================================================
// 2. CRC32 循環冗餘校驗碼 (IEEE 802.3 / ISO 3309)
// =========================================================================

namespace detail
{
constexpr auto GenerateCrc32Table() noexcept
{
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i)
  {
    uint32_t crc = i;
    for (int j = 0; j < 8; ++j)
    {
      crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
    table[i] = crc;
  }
  return table;
}

inline constexpr auto kCrc32Table = GenerateCrc32Table();
}  // namespace detail

/**
 * @brief CRC32 校驗碼計算 (constexpr)
 */
constexpr uint32_t Crc32(const uint8_t *bytes, size_t size, uint32_t seed = 0xFFFFFFFFU) noexcept
{
  uint32_t crc = seed;
  for (size_t i = 0; i < size; ++i)
  {
    crc = detail::kCrc32Table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

constexpr uint32_t Crc32(std::string_view str, uint32_t seed = 0xFFFFFFFFU) noexcept
{
  uint32_t crc = seed;
  for (char c : str)
  {
    crc = detail::kCrc32Table[(crc ^ static_cast<uint8_t>(c)) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

constexpr uint32_t Crc32(std::span<const uint8_t> bytes, uint32_t seed = 0xFFFFFFFFU) noexcept
{
  return Crc32(bytes.data(), bytes.size(), seed);
}

inline uint32_t Crc32(const void *data, size_t size, uint32_t seed = 0xFFFFFFFFU) noexcept
{
  return Crc32(static_cast<const uint8_t *>(data), size, seed);
}

// =========================================================================
// 3. MurmurHash3 (32-bit x86/ARM)
// =========================================================================

namespace detail
{
constexpr uint32_t RotateLeft32(uint32_t value, uint32_t count) noexcept
{
  return (value << count) | (value >> (32U - count));
}

constexpr uint32_t FMix32(uint32_t h) noexcept
{
  h ^= h >> 16U;
  h *= 0x85EBCA6BU;
  h ^= h >> 13U;
  h *= 0xC2B2AE35U;
  h ^= h >> 16U;
  return h;
}
}  // namespace detail

/**
 * @brief 32 位元 MurmurHash3 演算法 (constexpr)
 */
constexpr uint32_t MurmurHash3_32(const uint8_t *bytes, size_t size, uint32_t seed = 0) noexcept
{
  const size_t nblocks = size / 4;

  uint32_t h1 = seed;
  constexpr uint32_t c1 = 0xCC9E2D51U;
  constexpr uint32_t c2 = 0x1B873593U;

  // Body
  for (size_t i = 0; i < nblocks; ++i)
  {
    size_t idx = i * 4;
    uint32_t k1 = static_cast<uint32_t>(bytes[idx]) |
                  (static_cast<uint32_t>(bytes[idx + 1]) << 8U) |
                  (static_cast<uint32_t>(bytes[idx + 2]) << 16U) |
                  (static_cast<uint32_t>(bytes[idx + 3]) << 24U);

    k1 *= c1;
    k1 = detail::RotateLeft32(k1, 15U);
    k1 *= c2;

    h1 ^= k1;
    h1 = detail::RotateLeft32(h1, 13U);
    h1 = h1 * 5U + 0xE6546B64U;
  }

  // Tail
  const uint8_t *tail = bytes + (nblocks * 4);
  uint32_t k1 = 0;

  switch (size & 3U)
  {
    case 3:
      k1 ^= static_cast<uint32_t>(tail[2]) << 16U;
      [[fallthrough]];
    case 2:
      k1 ^= static_cast<uint32_t>(tail[1]) << 8U;
      [[fallthrough]];
    case 1:
      k1 ^= static_cast<uint32_t>(tail[0]);
      k1 *= c1;
      k1 = detail::RotateLeft32(k1, 15U);
      k1 *= c2;
      h1 ^= k1;
      break;
    default:
      break;
  }

  // Finalization
  h1 ^= static_cast<uint32_t>(size);
  return detail::FMix32(h1);
}

constexpr uint32_t MurmurHash3_32(std::string_view str, uint32_t seed = 0) noexcept
{
  const size_t nblocks = str.size() / 4;

  uint32_t h1 = seed;
  constexpr uint32_t c1 = 0xCC9E2D51U;
  constexpr uint32_t c2 = 0x1B873593U;

  for (size_t i = 0; i < nblocks; ++i)
  {
    size_t idx = i * 4;
    uint32_t k1 = static_cast<uint32_t>(static_cast<unsigned char>(str[idx])) |
                  (static_cast<uint32_t>(static_cast<unsigned char>(str[idx + 1])) << 8U) |
                  (static_cast<uint32_t>(static_cast<unsigned char>(str[idx + 2])) << 16U) |
                  (static_cast<uint32_t>(static_cast<unsigned char>(str[idx + 3])) << 24U);

    k1 *= c1;
    k1 = detail::RotateLeft32(k1, 15U);
    k1 *= c2;

    h1 ^= k1;
    h1 = detail::RotateLeft32(h1, 13U);
    h1 = h1 * 5U + 0xE6546B64U;
  }

  uint32_t k1 = 0;
  size_t tail_idx = nblocks * 4;
  switch (str.size() & 3U)
  {
    case 3:
      k1 ^= static_cast<uint32_t>(static_cast<unsigned char>(str[tail_idx + 2])) << 16U;
      [[fallthrough]];
    case 2:
      k1 ^= static_cast<uint32_t>(static_cast<unsigned char>(str[tail_idx + 1])) << 8U;
      [[fallthrough]];
    case 1:
      k1 ^= static_cast<uint32_t>(static_cast<unsigned char>(str[tail_idx]));
      k1 *= c1;
      k1 = detail::RotateLeft32(k1, 15U);
      k1 *= c2;
      h1 ^= k1;
      break;
    default:
      break;
  }

  h1 ^= static_cast<uint32_t>(str.size());
  return detail::FMix32(h1);
}

constexpr uint32_t MurmurHash3_32(std::span<const uint8_t> bytes, uint32_t seed = 0) noexcept
{
  return MurmurHash3_32(bytes.data(), bytes.size(), seed);
}

inline uint32_t MurmurHash3_32(const void *data, size_t size, uint32_t seed = 0) noexcept
{
  return MurmurHash3_32(static_cast<const uint8_t *>(data), size, seed);
}

// =========================================================================
// 4. HashCombine 組合式雜湊運算
// =========================================================================

/**
 * @brief 組合兩個雜湊值（黃金分割比質數分散法）
 */
template <typename T>
constexpr void HashCombine(size_t &seed, const T &value) noexcept
{
  if constexpr (sizeof(size_t) >= 8)
  {
    // 64-bit Golden Ratio Constant
    seed ^= static_cast<size_t>(value) + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U);
  }
  else
  {
    // 32-bit Golden Ratio Constant
    seed ^= static_cast<size_t>(value) + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
  }
}

template <typename T, typename... Rest>
constexpr void HashCombine(size_t &seed, const T &first, const Rest &...rest) noexcept
{
  HashCombine(seed, first);
  if constexpr (sizeof...(rest) > 0)
  {
    HashCombine(seed, rest...);
  }
}

}  // namespace hash

// 向上匯出常用別名至 ork::base 命名空間
using hash::Fnv1a32;
using hash::Fnv1a64;
using hash::Crc32;
using hash::MurmurHash3_32;
using hash::HashCombine;

// =========================================================================
// 5. 使用者自訂常數後綴字面量 (User-Defined Literals)
// =========================================================================
namespace literals
{
namespace hash_literals
{
constexpr uint64_t operator""_fnv64(const char *str, size_t len) noexcept
{
  return hash::Fnv1a64(std::string_view(str, len));
}

constexpr uint32_t operator""_fnv32(const char *str, size_t len) noexcept
{
  return hash::Fnv1a32(std::string_view(str, len));
}

constexpr uint32_t operator""_crc32(const char *str, size_t len) noexcept
{
  return hash::Crc32(std::string_view(str, len));
}

constexpr uint32_t operator""_mm32(const char *str, size_t len) noexcept
{
  return hash::MurmurHash3_32(std::string_view(str, len));
}
}  // namespace hash_literals
}  // namespace literals

}  // namespace ork::base
