#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ourokore/base/export.h"

// ----------------------------------------------------------------------------
// 編譯期方案選擇 (Compile-Time Policy Selection)
// ----------------------------------------------------------------------------
// 方案 A: Debug 完整追蹤 (檔名/行號)；Release 完全關閉 (零開銷直通)
#define ORK_HEAP_POLICY_A 1

// 方案 B: Debug 完整追蹤 (檔名/行號)；Release 輕量無鎖原子計數 (仍可驗收清空)
#define ORK_HEAP_POLICY_B 2

// 若編譯時未指定 ORK_HEAP_POLICY，預設採用方案 A
#ifndef ORK_HEAP_POLICY
  #define ORK_HEAP_POLICY ORK_HEAP_POLICY_A
#endif

// ----------------------------------------------------------------------------
// 根據所選方案與 Debug / Release (NDEBUG) 自動推導底層運作模式
// ----------------------------------------------------------------------------
#define ORK_HEAP_TRACK_DISABLED 0 // 模式 0: 完全關閉直通 (零開銷，無鎖無記帳)
#define ORK_HEAP_TRACK_ATOMIC   1 // 模式 1: 輕量原子計數 (極速無鎖，驗收 Heap 是否清空)
#define ORK_HEAP_TRACK_DETAILED 2 // 模式 2: 詳細診斷 (記錄位址、大小、序號、檔名與行號)

#ifndef ORK_HEAP_TRACKING_MODE
  #if defined(NDEBUG)
    #if (ORK_HEAP_POLICY == ORK_HEAP_POLICY_A)
      // 方案 A 之 Release: 完全關閉
      #define ORK_HEAP_TRACKING_MODE ORK_HEAP_TRACK_DISABLED
    #else
      // 方案 B 之 Release: 輕量原子計數
      #define ORK_HEAP_TRACKING_MODE ORK_HEAP_TRACK_ATOMIC
    #endif
  #else
    // Debug 模式下無論方案 A 或 B，皆統一為詳細定位診斷
    #define ORK_HEAP_TRACKING_MODE ORK_HEAP_TRACK_DETAILED
  #endif
#endif

namespace ork
{

/**
 * @brief 單筆記憶體配置追蹤記錄 (詳細診斷時使用)
 */
struct AllocationRecord
{
  void *address{nullptr};           // 配置之記憶體位址
  size_t size{0};                   // 配置大小 (bytes)
  size_t alignment{0};              // 對齊要求 (0 代表預設對齊)
  const char *file{nullptr};        // 來源檔案名稱 (UTF-8 字串)
  int line{0};                      // 來源程式碼行號
  uint64_t sequence_id{0};          // 全域累計配置序號 (1-based)
};

/**
 * @brief Heap 記憶體整體統計數據
 */
struct HeapStats
{
  size_t active_allocations{0};     // 當前存活配置區塊數
  size_t active_bytes{0};           // 當前存活佔用位元組數
  size_t total_allocations{0};      // 歷史累計配置次數
  size_t total_deallocations{0};    // 歷史累計釋放次數
  size_t peak_bytes{0};             // 峰值存活位元組數
};

class HeapTrackerImpl;

/**
 * @brief 堆記憶體追蹤核心管理器 (Heap Tracker)
 *
 * 【編譯期方案支援】：
 * - 方案 A (ORK_HEAP_POLICY_A): Debug 詳細診斷、Release 完全關閉 (零開銷直通)。
 * - 方案 B (ORK_HEAP_POLICY_B): Debug 詳細診斷、Release 輕量原子計數 (發布版兼顧清空檢驗)。
 */
class ORK_BASE_API HeapTracker
{
public:
  HeapTracker();
  ~HeapTracker();

  // 禁用拷貝與移動
  HeapTracker(const HeapTracker &) = delete;
  HeapTracker &operator=(const HeapTracker &) = delete;
  HeapTracker(HeapTracker &&) = delete;
  HeapTracker &operator=(HeapTracker &&) = delete;

  /**
   * @brief 取得全域預設之 HeapTracker 實例
   */
  static HeapTracker &get_default_tracker() noexcept;

  /**
   * @brief 查詢當前編譯選定的方案 (1 代表方案 A，2 代表方案 B)
   */
  static constexpr int get_compile_policy() noexcept
  {
    return ORK_HEAP_POLICY;
  }

  /**
   * @brief 查詢當前生效的底層模式 (0: Disabled, 1: Atomic, 2: Detailed)
   */
  static constexpr int get_compile_mode() noexcept
  {
    return ORK_HEAP_TRACKING_MODE;
  }

  /**
   * @brief 配置指定大小之記憶體並追蹤記錄
   */
  void *allocate(size_t size, const char *file = nullptr, int line = 0);

  /**
   * @brief 配置指定對齊與大小之記憶體 (C++17 Aligned Alloc)
   */
  void *allocate_aligned(size_t size, size_t alignment, const char *file = nullptr, int line = 0);

  /**
   * @brief 釋放記憶體
   */
  void deallocate(void *ptr) noexcept;

  /**
   * @brief 釋放對齊記憶體 (C++17 Aligned Delete)
   */
  void deallocate_aligned(void *ptr, size_t alignment) noexcept;

  /**
   * @brief 查詢當前 Heap 是否已經完全清空（零存活配置）
   * @return true 代表所有配置已釋放完畢；false 代表尚有記憶體未釋放
   */
  bool is_clean() const noexcept;

  /**
   * @brief 取得當前 Heap 整體統計數據快照
   */
  HeapStats get_stats() const noexcept;

  /**
   * @brief 取得當前所有未釋放之洩漏配置快照 (僅在詳細診斷模式下有內容)
   */
  std::vector<AllocationRecord> get_leaks() const;

  /**
   * @brief 產生格式化之 UTF-8 記憶體狀態與洩漏報告
   */
  std::string dump_leaks_to_string(std::string_view context_name = "") const;

  /**
   * @brief 斷言 Heap 必須已完全清空；若未清空則輸出診斷並拋出 std::runtime_error
   */
  void assert_clean(std::string_view context_name = "") const;

  /**
   * @brief 重設所有統計數據與記錄
   */
  void reset() noexcept;

private:
  std::unique_ptr<HeapTrackerImpl> m_impl;
};

} // namespace ork
