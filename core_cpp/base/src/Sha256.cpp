#include "ourokore/base/Sha256.hpp"

#include <bit>
#include <cstring>
#include <format>
#include <iomanip>
#include <sstream>

namespace ork::base::crypto
{

namespace
{

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline constexpr uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) noexcept
{
  return (x & y) ^ (~x & z);
}

inline constexpr uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) noexcept
{
  return (x & y) ^ (x & z) ^ (y & z);
}

inline constexpr uint32_t Sigma0(uint32_t x) noexcept
{
  return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

inline constexpr uint32_t Sigma1(uint32_t x) noexcept
{
  return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

inline constexpr uint32_t sigma0(uint32_t x) noexcept
{
  return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
}

inline constexpr uint32_t sigma1(uint32_t x) noexcept
{
  return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10);
}

}  // namespace

// ============================================================================
// CidV1 實作
// ============================================================================

CidV1 CidV1::from_digest(const Digest32 &digest) noexcept
{
  CidV1 cid;
  cid.raw_bytes[0] = CODE_SHA2_256;
  cid.raw_bytes[1] = DIGEST_LENGTH;
  std::memcpy(cid.raw_bytes.data() + 2, digest.data(), 32);
  return cid;
}

CidV1 CidV1::from_data(std::span<const uint8_t> data) noexcept
{
  return from_digest(Sha256::compute(data));
}

CidV1 CidV1::from_data(std::string_view data) noexcept
{
  return from_digest(Sha256::compute(data));
}

Result<CidV1> CidV1::from_bytes(std::span<const uint8_t> bytes) noexcept
{
  if (bytes.size() != MULTIHASH_SIZE)
  {
    return unexpected(Status::InvalidArgument("Multihash 長度不符：預期 34 位元組"));
  }
  if (bytes[0] != CODE_SHA2_256 || bytes[1] != DIGEST_LENGTH)
  {
    return unexpected(Status::InvalidArgument("不支援的 Multihash 標頭：預期 0x12, 0x20"));
  }

  CidV1 cid;
  std::memcpy(cid.raw_bytes.data(), bytes.data(), MULTIHASH_SIZE);
  return cid;
}

std::span<const uint8_t, 32> CidV1::digest() const noexcept
{
  return std::span<const uint8_t, 32>(raw_bytes.data() + 2, 32);
}

std::span<const uint8_t, CidV1::MULTIHASH_SIZE> CidV1::bytes() const noexcept
{
  return std::span<const uint8_t, MULTIHASH_SIZE>(raw_bytes.data(), MULTIHASH_SIZE);
}

std::string CidV1::to_hex() const
{
  static constexpr char hex_chars[] = "0123456789abcdef";
  std::string s;
  s.reserve(MULTIHASH_SIZE * 2);
  for (uint8_t b : raw_bytes)
  {
    s.push_back(hex_chars[(b >> 4) & 0x0F]);
    s.push_back(hex_chars[b & 0x0F]);
  }
  return s;
}

std::string CidV1::to_string() const
{
  Digest32 d;
  std::memcpy(d.data(), raw_bytes.data() + 2, 32);
  return "cid:sha256:" + Sha256::to_hex(d);
}

Result<CidV1> CidV1::from_hex(std::string_view hex) noexcept
{
  if (hex.size() != MULTIHASH_SIZE * 2)
  {
    return unexpected(Status::InvalidArgument("CIDv1 十六進位字串長度不符：預期 68 字元"));
  }

  auto parse_nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };

  std::array<uint8_t, MULTIHASH_SIZE> buf{};
  for (size_t i = 0; i < MULTIHASH_SIZE; ++i)
  {
    int hi = parse_nibble(hex[i * 2]);
    int lo = parse_nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0)
    {
      return unexpected(Status::InvalidArgument("CIDv1 包含非法十六進位字元"));
    }
    buf[i] = static_cast<uint8_t>((hi << 4) | lo);
  }

  return from_bytes(buf);
}

Result<CidV1> CidV1::from_string(std::string_view str) noexcept
{
  static constexpr std::string_view prefix = "cid:sha256:";
  if (str.starts_with(prefix))
  {
    auto hex_digest = str.substr(prefix.size());
    if (hex_digest.size() != 64)
    {
      return unexpected(Status::InvalidArgument("標準 CID SHA-256 摘要長度不符：預期 64 字元"));
    }
    auto parse_nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    Digest32 d{};
    for (size_t i = 0; i < 32; ++i)
    {
      int hi = parse_nibble(hex_digest[i * 2]);
      int lo = parse_nibble(hex_digest[i * 2 + 1]);
      if (hi < 0 || lo < 0)
      {
        return unexpected(Status::InvalidArgument("摘要包含非法十六進位字元"));
      }
      d[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return from_digest(d);
  }
  return from_hex(str);
}


// ============================================================================
// Sha256 實作
// ============================================================================

Sha256::Sha256() noexcept
{
  reset();
}

void Sha256::reset() noexcept
{
  m_state[0] = 0x6a09e667;
  m_state[1] = 0xbb67ae85;
  m_state[2] = 0x3c6ef372;
  m_state[3] = 0xa54ff53a;
  m_state[4] = 0x510e527f;
  m_state[5] = 0x9b05688c;
  m_state[6] = 0x1f83d9ab;
  m_state[7] = 0x5be0cd19;
  m_count = 0;
  std::memset(m_buffer, 0, sizeof(m_buffer));
}

void Sha256::update(const void *data, size_t length) noexcept
{
  if (!data || length == 0)
  {
    return;
  }

  const auto *input = static_cast<const uint8_t *>(data);
  size_t buffer_idx = static_cast<size_t>(m_count % 64);
  m_count += length;

  size_t part_len = 64 - buffer_idx;
  size_t i = 0;

  if (length >= part_len)
  {
    std::memcpy(&m_buffer[buffer_idx], input, part_len);
    transform(m_buffer);

    for (i = part_len; i + 63 < length; i += 64)
    {
      transform(&input[i]);
    }
    buffer_idx = 0;
  }

  std::memcpy(&m_buffer[buffer_idx], &input[i], length - i);
}

void Sha256::update(std::span<const uint8_t> data) noexcept
{
  update(data.data(), data.size());
}

void Sha256::update(std::string_view str) noexcept
{
  update(str.data(), str.size());
}

void Sha256::transform(const uint8_t *data) noexcept
{
  uint32_t w[64];
  for (size_t t = 0; t < 16; ++t)
  {
    w[t] = (static_cast<uint32_t>(data[t * 4]) << 24) |
           (static_cast<uint32_t>(data[t * 4 + 1]) << 16) |
           (static_cast<uint32_t>(data[t * 4 + 2]) << 8) |
           (static_cast<uint32_t>(data[t * 4 + 3]));
  }
  for (size_t t = 16; t < 64; ++t)
  {
    w[t] = sigma1(w[t - 2]) + w[t - 7] + sigma0(w[t - 15]) + w[t - 16];
  }

  uint32_t a = m_state[0];
  uint32_t b = m_state[1];
  uint32_t c = m_state[2];
  uint32_t d = m_state[3];
  uint32_t e = m_state[4];
  uint32_t f = m_state[5];
  uint32_t g = m_state[6];
  uint32_t h = m_state[7];

  for (size_t t = 0; t < 64; ++t)
  {
    uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + K[t] + w[t];
    uint32_t t2 = Sigma0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  m_state[0] += a;
  m_state[1] += b;
  m_state[2] += c;
  m_state[3] += d;
  m_state[4] += e;
  m_state[5] += f;
  m_state[6] += g;
  m_state[7] += h;
}

Digest32 Sha256::finalize() noexcept
{
  uint8_t final_count[8];
  uint64_t total_bits = m_count * 8;
  for (size_t i = 0; i < 8; ++i)
  {
    final_count[i] = static_cast<uint8_t>((total_bits >> ((7 - i) * 8)) & 0xFF);
  }

  uint8_t pad = 0x80;
  update(&pad, 1);

  uint8_t zero = 0x00;
  while ((m_count % 64) != 56)
  {
    update(&zero, 1);
  }

  update(final_count, 8);

  Digest32 digest;
  for (size_t i = 0; i < 8; ++i)
  {
    digest[i * 4] = static_cast<uint8_t>((m_state[i] >> 24) & 0xFF);
    digest[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 16) & 0xFF);
    digest[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 8) & 0xFF);
    digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i] & 0xFF);
  }

  reset();
  return digest;
}

Digest32 Sha256::compute(std::span<const uint8_t> data) noexcept
{
  Sha256 ctx;
  ctx.update(data);
  return ctx.finalize();
}

Digest32 Sha256::compute(const void *data, size_t length) noexcept
{
  Sha256 ctx;
  ctx.update(data, length);
  return ctx.finalize();
}

Digest32 Sha256::compute(std::string_view str) noexcept
{
  Sha256 ctx;
  ctx.update(str);
  return ctx.finalize();
}

std::string Sha256::compute_hex(std::span<const uint8_t> data)
{
  return to_hex(compute(data));
}

std::string Sha256::compute_hex(std::string_view str)
{
  return to_hex(compute(str));
}

std::string Sha256::to_hex(const Digest32 &digest)
{
  static constexpr char hex_chars[] = "0123456789abcdef";
  std::string s;
  s.reserve(64);
  for (uint8_t b : digest)
  {
    s.push_back(hex_chars[(b >> 4) & 0x0F]);
    s.push_back(hex_chars[b & 0x0F]);
  }
  return s;
}

}  // namespace ork::base::crypto
