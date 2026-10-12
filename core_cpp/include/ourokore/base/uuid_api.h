#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ourokore/base/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 128 位元 UUID 二進位結構體 (純 C ABI)
 * 內部為 16 位元組連續記憶體，符合 RFC 4122 / RFC 9562 規範。
 */
typedef struct ork_uuid_t {
  uint8_t bytes[16];
} ork_uuid_t;

/**
 * @brief 產生全 0 的 Nil UUID (00000000-0000-0000-0000-000000000000)
 */
ORK_BASE_API ork_uuid_t ORK_CALL ork_uuid_nil(void);

/**
 * @brief 產生全 0xFF 的 Max UUID (ffffffff-ffff-ffff-ffff-ffffffffffff)
 */
ORK_BASE_API ork_uuid_t ORK_CALL ork_uuid_max(void);

/**
 * @brief 產生密碼學安全隨機 UUIDv4 (RFC 4122 / RFC 9562)
 */
ORK_BASE_API ork_uuid_t ORK_CALL ork_uuid_generate_v4(void);

/**
 * @brief 產生時間排序型 UUIDv7 (RFC 9562)
 * 前 48 位元為 Unix 紀元毫秒時間戳，具備嚴格單調時序與極佳資料庫索引親和性。
 */
ORK_BASE_API ork_uuid_t ORK_CALL ork_uuid_generate_v7(void);

/**
 * @brief 指定毫秒時間戳記產生 UUIDv7
 * @param timestamp_ms Unix 紀元毫秒時間戳記
 */
ORK_BASE_API ork_uuid_t ORK_CALL ork_uuid_generate_v7_with_time(uint64_t timestamp_ms);

/**
 * @brief 判定是否為 Nil UUID (全 0)
 * @return 1 代表是 Nil；0 代表否；-1 代表無效指標
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_is_nil(const ork_uuid_t *uuid);

/**
 * @brief 判定是否為 Max UUID (全 0xFF)
 * @return 1 代表是 Max；0 代表否；-1 代表無效指標
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_is_max(const ork_uuid_t *uuid);

/**
 * @brief 取得 UUID 版本號 (Version: 1~8)
 * @return 版本號 (如 4 或 7)；若指標為空傳回 -1
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_get_version(const ork_uuid_t *uuid);

/**
 * @brief 取得 UUID 變體代碼 (Variant)
 * @return RFC 4122 / 9562 為 1 (0b10)；若指標為空傳回 -1
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_get_variant(const ork_uuid_t *uuid);

/**
 * @brief 提取 UUIDv7 之 Unix 紀元毫秒時間戳記
 * @param uuid 輸入 UUID 指標
 * @param out_ms 接收毫秒數的指標
 * @return 0 成功；非 0 失敗 (如空指標或非 v7 版本)
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_get_timestamp_ms(const ork_uuid_t *uuid, uint64_t *out_ms);

/**
 * @brief 將 UUID 轉為標準 36 字元橫線格式字串 (含結尾 null 字元，共需 37 位元組)
 * @param uuid 輸入 UUID
 * @param out_buf 輸出字元緩衝區 (長度需 >= 37)
 * @param buf_size 緩衝區容量
 * @param uppercase 1 代表大寫 (ABCDEF)，0 代表小寫 (abcdef)
 * @return 成功寫入之字元數 (36)；失敗傳回負數錯誤碼
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_to_string(const ork_uuid_t *uuid, char *out_buf, size_t buf_size, int32_t uppercase);

/**
 * @brief 從字串解析 UUID (支援標準 36 字元橫線、32 字元緊湊、38 字元大括號格式，不分大小寫)
 * @param str 輸入字串 (null 結尾 UTF-8)
 * @param out_uuid 接收解析結果之 UUID 結構指標
 * @return 0 代表成功；非 0 代表解析錯誤
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_from_string(const char *str, ork_uuid_t *out_uuid);

/**
 * @brief 比較兩個 UUID (16 位元組字典序)
 * @return <0 代表 a < b；0 代表 a == b；>0 代表 a > b
 */
ORK_BASE_API int32_t ORK_CALL ork_uuid_compare(const ork_uuid_t *a, const ork_uuid_t *b);

#ifdef __cplusplus
}
#endif
