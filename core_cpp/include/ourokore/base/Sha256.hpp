#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "ourokore/base/Result.hpp"
#include "ourokore/base/export.h"

namespace ork::base::crypto
{

/**
 * @brief 256 位元 (32 位元組) SHA-256 原始二進位摘要
 */
using Digest32 = std::array<uint8_t, 32>;

/**
 * @brief 標準 CIDv1 Multihash 二進位容器 (34 位元組)
 * [0x12 (sha2-256), 0x20 (32 bytes length), 32-byte hash digest]
 */
class ORK_BASE_API CidV1
{
public:
  static constexpr uint8_t CODE_SHA2_256 = 0x12;
  static constexpr uint8_t DIGEST_LENGTH = 0x20;
  static constexpr size_t MULTIHASH_SIZE = 34;

  std::array<uint8_t, MULTIHASH_SIZE> raw_bytes{};

  constexpr CidV1() noexcept = default;

  /**
   * @brief 由 32 位元組 SHA-256 摘要建構 CIDv1 Multihash
   */
  static CidV1 from_digest(const Digest32 &digest) noexcept;

  /**
   * @brief 自原始資料計算並建構 CIDv1
   */
  static CidV1 from_data(std::span<const uint8_t> data) noexcept;
  static CidV1 from_data(std::string_view data) noexcept;

  /**
   * @brief 解析 34 位元組二進位 Multihash
   */
  static Result<CidV1> from_bytes(std::span<const uint8_t> bytes) noexcept;

  /**
   * @brief 解析 68 字元之十六進位 Multihash 字串
   */
  static Result<CidV1> from_hex(std::string_view hex) noexcept;

  /**
   * @brief 解析標準 CID 字串 ("cid:sha256:<hex>" 或純 68 字元 Multihash hex)
   */
  static Result<CidV1> from_string(std::string_view str) noexcept;

  /**
   * @brief 取得 32 位元組原始摘要參照
   */
  std::span<const uint8_t, 32> digest() const noexcept;

  /**
   * @brief 取得 34 位元組 Multihash 二進位切片
   */
  std::span<const uint8_t, MULTIHASH_SIZE> bytes() const noexcept;

  /**
   * @brief 轉為小寫十六進位字串 (68 字元)
   */
  std::string to_hex() const;

  /**
   * @brief 轉為標準 CID 格式字串: "cid:sha256:<64-char-digest-hex>"
   */
  std::string to_string() const;

  bool operator==(const CidV1 &other) const noexcept = default;
};

/**
 * @brief 純 CPU SHA-256 密碼學雜湊計算器 (零外部依賴、零 OpenSSL / bcrypt)
 * 符合 FIPS PUB 180-4 與 RFC 6234 規範
 */
class ORK_BASE_API Sha256
{
public:
  Sha256() noexcept;

  /**
   * @brief 重置計算狀態
   */
  void reset() noexcept;

  /**
   * @brief 增量餵入資料
   */
  void update(const void *data, size_t length) noexcept;
  void update(std::span<const uint8_t> data) noexcept;
  void update(std::string_view str) noexcept;

  /**
   * @brief 完成雜湊計算並產生 32 位元組摘要
   */
  Digest32 finalize() noexcept;

  /**
   * @brief 一次性快速計算資料摘要
   */
  static Digest32 compute(std::span<const uint8_t> data) noexcept;
  static Digest32 compute(const void *data, size_t length) noexcept;
  static Digest32 compute(std::string_view str) noexcept;

  /**
   * @brief 一次性快速計算並輸出小寫十六進位字串 (64 字元)
   */
  static std::string compute_hex(std::span<const uint8_t> data);
  static std::string compute_hex(std::string_view str);

  /**
   * @brief 一次性計算並封裝為 CIDv1 Multihash
   */
  static CidV1 compute_cid(std::span<const uint8_t> data) noexcept { return CidV1::from_data(data); }
  static CidV1 compute_cid(std::string_view str) noexcept { return CidV1::from_data(str); }

  /**
   * @brief 將 32 位元組摘要轉為十六進位字串
   */
  static std::string to_hex(const Digest32 &digest);

private:
  uint32_t m_state[8]{};
  uint64_t m_count{0};
  uint8_t m_buffer[64]{};

  void transform(const uint8_t *data) noexcept;
};

}  // namespace ork::base::crypto

namespace ork::base
{
using CidV1 = crypto::CidV1;
using Sha256 = crypto::Sha256;
}  // namespace ork::base
