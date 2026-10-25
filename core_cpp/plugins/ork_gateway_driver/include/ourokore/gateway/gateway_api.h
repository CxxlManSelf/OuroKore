#pragma once

#include "gateway_export.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *ork_gateway_handle_t;

// 狀態碼定義
#define ORK_GATEWAY_OK                      0
#define ORK_GATEWAY_ERROR_INVALID_ARG      -1
#define ORK_GATEWAY_ERROR_IO               -2
#define ORK_GATEWAY_ERROR_NOT_FOUND        -3
#define ORK_GATEWAY_ERROR_SECURITY         -4
#define ORK_GATEWAY_ERROR_BUFFER_TOO_SMALL -5
#define ORK_GATEWAY_ERROR_INTERNAL         -99

/**
 * @brief 初始化次世代零件網關驅動實體
 * @param cas_root_dir CAS 內容定址根目錄路徑 (UTF-8)
 * @param out_handle 輸出之網關不透明控制句柄
 * @return 狀態碼 (ORK_GATEWAY_OK 為成功)
 */
ORK_GATEWAY_API int32_t ork_gateway_create(const char *cas_root_dir, ork_gateway_handle_t *out_handle);

/**
 * @brief 銷毀網關驅動實體並釋放相關資源
 * @param handle 網關句柄
 */
ORK_GATEWAY_API void ork_gateway_destroy(ork_gateway_handle_t handle);

/**
 * @brief 將資料以原子防損毀方式寫入 CAS 內容定址儲存庫
 * @param handle 網關句柄
 * @param data 二進位資料指標
 * @param size 資料長度 (bytes)
 * @param out_cid_hex 輸出之 CIDv1 Multihash 16 進位字串緩衝區 (最少需 69 bytes，含 null 結尾)
 * @param max_len 緩衝區最大長度
 * @return 狀態碼
 */
ORK_GATEWAY_API int32_t ork_gateway_put_cas(
    ork_gateway_handle_t handle,
    const uint8_t *data,
    size_t size,
    char *out_cid_hex,
    size_t max_len);

/**
 * @brief 依據 CID 自 CAS 內容定址儲存庫讀取資料
 * @param handle 網關句柄
 * @param cid_hex 68 字元之 Multihash 16 進位字串 (UTF-8)
 * @param out_buffer 輸出二進位資料緩衝區 (若為 NULL，則僅透過 out_actual_size 返回所需大小)
 * @param buffer_size 輸出緩衝區大小
 * @param out_actual_size 實際讀取到的位元組數
 * @return 狀態碼
 */
ORK_GATEWAY_API int32_t ork_gateway_get_cas(
    ork_gateway_handle_t handle,
    const char *cid_hex,
    uint8_t *out_buffer,
    size_t buffer_size,
    size_t *out_actual_size);

/**
 * @brief 檢查指定 CID 之內容是否存在於 CAS 儲存庫中
 * @param handle 網關句柄
 * @param cid_hex Multihash 16 進位字串
 * @param out_exists 輸出是否存在 (1: 存在, 0: 不存在)
 * @return 狀態碼
 */
ORK_GATEWAY_API int32_t ork_gateway_has_cas(
    ork_gateway_handle_t handle,
    const char *cid_hex,
    int32_t *out_exists);

/**
 * @brief 解析三段式工廠提示並安全解析外掛模組實體路徑 (具備目錄穿越防禦)
 * @param handle 網關句柄
 * @param factory_tag 三段式標籤 (Vendor/Plugin/Version)
 * @param relative_module_path 外掛相對路徑 (如 "bin/weapon.dll")
 * @param out_full_path 輸出之正規化絕對路徑緩衝區 (UTF-8)
 * @param max_len 輸出緩衝區最大長度
 * @return 狀態碼
 */
ORK_GATEWAY_API int32_t ork_gateway_resolve_plugin_path(
    ork_gateway_handle_t handle,
    const char *factory_tag,
    const char *relative_module_path,
    char *out_full_path,
    size_t max_len);

#ifdef __cplusplus
}
#endif
