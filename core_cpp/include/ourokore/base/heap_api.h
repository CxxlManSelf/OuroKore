#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ourokore/base/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 透過 HeapTracker 配置記憶體 (純 C ABI)
 * @param size 配置位元組數
 * @param file 來源檔案名稱 (UTF-8, 可為 NULL)
 * @param line 來源程式碼行號
 * @return 配置成功傳回記憶體指標；失敗傳回 NULL
 */
ORK_BASE_API void *ORK_CALL ork_heap_allocate(size_t size, const char *file, int32_t line);

/**
 * @brief 透過 HeapTracker 配置指定對齊之記憶體 (純 C ABI)
 */
ORK_BASE_API void *ORK_CALL ork_heap_allocate_aligned(size_t size, size_t alignment, const char *file, int32_t line);

/**
 * @brief 釋放透過 HeapTracker 配置之記憶體 (純 C ABI)
 * @param ptr 待釋放記憶體指標 (可為 NULL)
 */
ORK_BASE_API void ORK_CALL ork_heap_deallocate(void *ptr);

/**
 * @brief 釋放對齊記憶體 (純 C ABI)
 */
ORK_BASE_API void ORK_CALL ork_heap_deallocate_aligned(void *ptr, size_t alignment);

/**
 * @brief 檢查當前 Heap 是否已經完全清空 (純 C ABI)
 * @return 1 代表已清空 (Clean, 零洩漏)；0 代表尚有未釋放配置
 */
ORK_BASE_API int32_t ORK_CALL ork_heap_is_clean(void);

/**
 * @brief 取得當前存活配置區塊數 (純 C ABI)
 */
ORK_BASE_API uint64_t ORK_CALL ork_heap_get_active_allocations(void);

/**
 * @brief 取得當前存活配置佔用位元組數 (純 C ABI)
 */
ORK_BASE_API uint64_t ORK_CALL ork_heap_get_active_bytes(void);

/**
 * @brief 輸出記憶體洩漏報告至使用者緩衝區 (純 C ABI, 保證 100% UTF-8)
 * @param out_buf 接收緩衝區指標
 * @param buf_size 接收緩衝區大小
 * @return 成功寫入之字元數（若緩衝區為 NULL 則傳回所需緩衝區長度）
 */
ORK_BASE_API int32_t ORK_CALL ork_heap_dump_leaks(char *out_buf, size_t buf_size);

/**
 * @brief 斷言 Heap 必須已完全清空 (純 C ABI)
 * @param context_name 上下文名稱 (UTF-8, 可為 NULL)
 * @return 0 代表清空成功；非 0 代表斷言失敗（偵測到洩漏）
 */
ORK_BASE_API int32_t ORK_CALL ork_heap_assert_clean(const char *context_name);

/**
 * @brief 重設 HeapTracker 統計數據與記錄 (純 C ABI，僅供測試模擬)
 */
ORK_BASE_API void ORK_CALL ork_heap_reset(void);

#ifdef __cplusplus
}
#endif
