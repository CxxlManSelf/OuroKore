#pragma once

#include "ourokore/c_api/core.h"
#include "ourokore/c_api/component_api.h"  // IWYU pragma: export
#include "ourokore/c_api/internal_api.h"   // IWYU pragma: export

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 原子化宣告進程級別的主程式初始化權限（僅主程式首次呼叫回傳 OK）
 */
ORK_API int32_t ORK_CALL ork_try_initialize_core(void);

/**
 * @brief 顯式同步觸發一輪循環參照收集判定
 */
ORK_API int32_t ORK_CALL ork_collect_cycles(void);

/**
 * @brief 顯式同步排空並等待背景延遲銷毀任務全數完成
 */
ORK_API int32_t ORK_CALL ork_flush_deferred_deletions(void);

// 延遲銷毀模式
#define ORK_DEFERRED_DELETE_ASYNC 0
#define ORK_DEFERRED_DELETE_SYNC  1

/**
 * @brief 配置延遲銷毀執行模式（0: 背景非同步執行緒池, 1: 即時同步執行）
 */
ORK_API int32_t ORK_CALL ork_set_deferred_delete_mode(int32_t mode);

/**
 * @brief 取得目前排隊等待物理銷毀之任務數量
 */
ORK_API uint64_t ORK_CALL ork_get_deferred_delete_pending_count(void);

/**
 * @brief 停止循環參照收集器背景巡檢執行緒
 */
ORK_API int32_t ORK_CALL ork_stop_cycle_collector(void);

/**
 * @brief 停止延遲銷毀執行緒池並等待排隊任務排空退出
 */
ORK_API int32_t ORK_CALL ork_stop_deferred_deletions(void);

/**
 * @brief 優雅終止核心背景執行緒池（包含 CycleCollector 與 DeferredDeleteQueue）
 */
ORK_API int32_t ORK_CALL ork_shutdown_core(void);

/**
 * @brief 復位核心初始化旗標（支援測試重用）
 */
ORK_API int32_t ORK_CALL ork_reset_core_state(void);

/**
 * @brief 等待並排空所有背景落盤與儲存清理任務
 */
ORK_API int32_t ORK_CALL ork_flush_storage(void);

/**
 * @brief 優雅終止核心執行時環境與所有背景任務
 */
ORK_API int32_t ORK_CALL ork_shutdown_runtime(void);

/**
 * @brief 全域物件銷毀回呼函式指標
 */
typedef void (*ork_object_destroyed_fn_t)(HandleID id);

/**
 * @brief 設定核心全域物件銷毀監聽回呼
 */
ORK_API int32_t ORK_CALL ork_set_object_destroyed_callback(ork_object_destroyed_fn_t fn);

#ifdef __cplusplus
}
#endif
