#include "ourokore/base/Uuid.hpp"
#include "ourokore/base/uuid_api.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <random>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <bcrypt.h>
#elif defined(__linux__) || defined(__unix__)
  #include <sys/random.h>
#endif

namespace ork::base
{

namespace
{

/**
 * @brief 密碼學安全隨機位元組產生器
 */
bool GetSecureRandomBytes(uint8_t *buffer, size_t size) noexcept
{
  if (!buffer || size == 0) return false;

#if defined(_WIN32)
  NTSTATUS status = BCryptGenRandom(NULL, buffer, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (BCRYPT_SUCCESS(status))
  {
    return true;
  }
#elif defined(__linux__)
  ssize_t ret = getrandom(buffer, size, 0);
  if (ret == static_cast<ssize_t>(size))
  {
    return true;
  }
#endif

  // 跨平台後備方案：std::random_device + std::mt19937_64
  try
  {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    size_t i = 0;
    while (i + 8 <= size)
    {
      uint64_t val = gen();
      std::memcpy(buffer + i, &val, 8);
      i += 8;
    }
    if (i < size)
    {
      uint64_t val = gen();
      std::memcpy(buffer + i, &val, size - i);
    }
    return true;
  }
  catch (...)
  {
    return false;
  }
}

/**
 * @brief 取得當前 Unix 紀元毫秒時間戳
 */
uint64_t GetCurrentUnixTimeMs() noexcept
{
  using namespace std::chrono;
  auto now = system_clock::now();
  return static_cast<uint64_t>(duration_cast<milliseconds>(now.time_since_epoch()).count());
}

/**
 * @brief RFC 9562 UUIDv7 單調性狀態守衛
 */
struct V7MonotonicState
{
  std::mutex mtx;
  uint64_t last_ms{0};
  uint16_t counter{0};  // 12 位元單調計數器 (0..4095)
};

static V7MonotonicState g_v7_state;

}  // namespace

// =========================================================================
// 1. C++ Uuid 核心生成實作
// =========================================================================

Uuid Uuid::GenerateV4() noexcept
{
  std::array<uint8_t, 16> bytes{};
  GetSecureRandomBytes(bytes.data(), 16);

  // RFC 4122 / RFC 9562 格式規範：
  // 1. 版本號 4 (高 4 位元設為 0b0100)
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
  // 2. 變體號 1 (高 2 位元設為 0b10)
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

  return Uuid(bytes);
}

Uuid Uuid::GenerateV7() noexcept
{
  uint64_t now_ms = GetCurrentUnixTimeMs();
  uint16_t seq = 0;
  uint64_t gen_ms = 0;

  {
    std::lock_guard<std::mutex> lock(g_v7_state.mtx);
    if (now_ms > g_v7_state.last_ms)
    {
      g_v7_state.last_ms = now_ms;
      g_v7_state.counter = 0;
    }
    else
    {
      // 同一毫秒或時鐘回撥：遞增計數器以維持嚴格單調性
      g_v7_state.counter++;
      if (g_v7_state.counter >= 4096)
      {
        // 12 位元溢位：人為向前推推進 1 毫秒
        g_v7_state.last_ms++;
        g_v7_state.counter = 0;
      }
    }
    gen_ms = g_v7_state.last_ms;
    seq = g_v7_state.counter;
  }

  std::array<uint8_t, 16> bytes{};
  // 隨機填充後續隨機位元
  uint8_t rand_bytes[10]{};
  GetSecureRandomBytes(rand_bytes, sizeof(rand_bytes));

  // 1. 前 48 位元：毫秒時間戳 (大端序)
  bytes[0] = static_cast<uint8_t>((gen_ms >> 40) & 0xFF);
  bytes[1] = static_cast<uint8_t>((gen_ms >> 32) & 0xFF);
  bytes[2] = static_cast<uint8_t>((gen_ms >> 24) & 0xFF);
  bytes[3] = static_cast<uint8_t>((gen_ms >> 16) & 0xFF);
  bytes[4] = static_cast<uint8_t>((gen_ms >> 8) & 0xFF);
  bytes[5] = static_cast<uint8_t>(gen_ms & 0xFF);

  // 2. 接著 12 位元：4 位元版本 7 (0b0111) + 12 位元單調計數器 (seq)
  bytes[6] = static_cast<uint8_t>(0x70 | ((seq >> 8) & 0x0F));
  bytes[7] = static_cast<uint8_t>(seq & 0xFF);

  // 3. 變體號 1 (0b10) + 其餘隨機熵 (62 位元隨機)
  bytes[8] = static_cast<uint8_t>(0x80 | (rand_bytes[0] & 0x3F));
  for (size_t i = 0; i < 7; ++i)
  {
    bytes[9 + i] = rand_bytes[1 + i];
  }

  return Uuid(bytes);
}

Uuid Uuid::FromUnixTimeMsV7(uint64_t timestamp_ms) noexcept
{
  std::array<uint8_t, 16> bytes{};
  uint8_t rand_bytes[10]{};
  GetSecureRandomBytes(rand_bytes, sizeof(rand_bytes));

  // 前 48 位元：時間戳
  bytes[0] = static_cast<uint8_t>((timestamp_ms >> 40) & 0xFF);
  bytes[1] = static_cast<uint8_t>((timestamp_ms >> 32) & 0xFF);
  bytes[2] = static_cast<uint8_t>((timestamp_ms >> 24) & 0xFF);
  bytes[3] = static_cast<uint8_t>((timestamp_ms >> 16) & 0xFF);
  bytes[4] = static_cast<uint8_t>((timestamp_ms >> 8) & 0xFF);
  bytes[5] = static_cast<uint8_t>(timestamp_ms & 0xFF);

  // 版本 7 與隨機 sub-ms/seq
  uint16_t sub_seq = static_cast<uint16_t>((rand_bytes[8] << 8) | rand_bytes[9]) & 0x0FFF;
  bytes[6] = static_cast<uint8_t>(0x70 | ((sub_seq >> 8) & 0x0F));
  bytes[7] = static_cast<uint8_t>(sub_seq & 0xFF);

  // 變體 1 與隨機熵
  bytes[8] = static_cast<uint8_t>(0x80 | (rand_bytes[0] & 0x3F));
  for (size_t i = 0; i < 7; ++i)
  {
    bytes[9 + i] = rand_bytes[1 + i];
  }

  return Uuid(bytes);
}

}  // namespace ork::base

// =========================================================================
// 2. 純 C ABI 函式實作 (Cross-Language FFI)
// =========================================================================

extern "C" {

ork_uuid_t ork_uuid_nil(void)
{
  return ork::base::Uuid::Nil().ToCApi();
}

ork_uuid_t ork_uuid_max(void)
{
  return ork::base::Uuid::Max().ToCApi();
}

ork_uuid_t ork_uuid_generate_v4(void)
{
  try
  {
    return ork::base::Uuid::GenerateV4().ToCApi();
  }
  catch (...)
  {
    return ork_uuid_nil();
  }
}

ork_uuid_t ork_uuid_generate_v7(void)
{
  try
  {
    return ork::base::Uuid::GenerateV7().ToCApi();
  }
  catch (...)
  {
    return ork_uuid_nil();
  }
}

ork_uuid_t ork_uuid_generate_v7_with_time(uint64_t timestamp_ms)
{
  try
  {
    return ork::base::Uuid::FromUnixTimeMsV7(timestamp_ms).ToCApi();
  }
  catch (...)
  {
    return ork_uuid_nil();
  }
}

int32_t ork_uuid_is_nil(const ork_uuid_t *uuid)
{
  if (!uuid) return -1;
  return ork::base::Uuid(*uuid).IsNil() ? 1 : 0;
}

int32_t ork_uuid_is_max(const ork_uuid_t *uuid)
{
  if (!uuid) return -1;
  return ork::base::Uuid(*uuid).IsMax() ? 1 : 0;
}

int32_t ork_uuid_get_version(const ork_uuid_t *uuid)
{
  if (!uuid) return -1;
  return ork::base::Uuid(*uuid).Version();
}

int32_t ork_uuid_get_variant(const ork_uuid_t *uuid)
{
  if (!uuid) return -1;
  return ork::base::Uuid(*uuid).Variant();
}

int32_t ork_uuid_get_timestamp_ms(const ork_uuid_t *uuid, uint64_t *out_ms)
{
  if (!uuid || !out_ms) return -1;
  ork::base::Uuid cpp_uuid(*uuid);
  if (cpp_uuid.Version() != 7)
  {
    return -2;  // 非 UUIDv7
  }
  *out_ms = cpp_uuid.TimestampMs();
  return 0;
}

int32_t ork_uuid_to_string(const ork_uuid_t *uuid, char *out_buf, size_t buf_size, int32_t uppercase)
{
  if (!uuid || !out_buf || buf_size < 37) return -1;
  try
  {
    ork::base::Uuid cpp_uuid(*uuid);
    cpp_uuid.ToChars(out_buf, uppercase != 0);
    out_buf[36] = '\0';
    return 36;
  }
  catch (...)
  {
    return -2;
  }
}

int32_t ork_uuid_from_string(const char *str, ork_uuid_t *out_uuid)
{
  if (!str || !out_uuid) return -1;
  try
  {
    auto res = ork::base::Uuid::FromString(str);
    if (!res.has_value())
    {
      return -2;
    }
    *out_uuid = res.value().ToCApi();
    return 0;
  }
  catch (...)
  {
    return -3;
  }
}

int32_t ork_uuid_compare(const ork_uuid_t *a, const ork_uuid_t *b)
{
  if (!a && !b) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return std::memcmp(a->bytes, b->bytes, 16);
}

}  // extern "C"
