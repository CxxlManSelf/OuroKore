#pragma once

#include <cstddef>
#include <cstdint>

namespace ork
{

using HandleID = uint64_t;

/**
 * @brief 脫水作業成效報告 (Dehydration Execution Report)
 */
struct DehydrationReport
{
  size_t freed_bytes{0};           ///< 本輪實際釋放的實體記憶體位元組數
  size_t dehydrated_count{0};      ///< 成功脫水的物件總數
  bool has_more_candidates{false}; ///< 佇列中是否仍有可脫水的候選冷物件（若為 false 代表全數已脫水或正被鎖定）

  explicit operator bool() const noexcept { return freed_bytes > 0; }
};

/**
 * @brief 自動脫水外掛模組抽象介面 (Auto-Dehydration Plugin SPI)
 *
 * 職責定義：
 * 1. 接收核心與建立工廠的情報通報（註冊、物件大小、銷毀清理、存取熱度）。
 * 2. 依據特定策略（如 LRU、記憶體配額）主動向核心發起 ork::DehydrateByID(id) 脫水指令。
 * 3. 脫水單元僅能管理已登記入自身名冊的物件；未登記（或已註銷）的物件將完全免疫於自動脫水，永久常駐記憶體。
 */
class IAutoDehydrator
{
public:
  virtual ~IAutoDehydrator() = default;

  // --- 1. 名冊與大小追蹤 (Info Stream) ---
  /**
   * @brief 將物件納入自動脫水候選名冊，並告知物件大小
   * @param id 物件全域唯一 HandleID
   * @param size_bytes 物件在記憶體中佔用的位元組大小（如 sizeof(T)）
   */
  virtual void Register(HandleID id, size_t size_bytes) = 0;

  /**
   * @brief 將物件移出脫水名冊，並扣減列管記憶體容量。
   *
   * 【重要呼叫時機】：
   * 1. 業務端主動將物件轉為常駐物件時。
   * 2. 當物件被徹底銷毀 / 刪除（Destroyed / Released）時，系統必須主動呼叫此函式通知脫水模組，
   *    以清理名冊並釋放追蹤資源，防範懸空 ID 與記憶體統計洩漏。
   *
   * @param id 物件 HandleID
   */
  virtual void Unregister(HandleID id) = 0;

  /**
   * @brief 查詢物件是否受到脫水追蹤
   */
  virtual bool IsTracked(HandleID id) const = 0;

  /**
   * @brief 取得目前脫水模組列管追蹤的物件佔用記憶體總位元組數
   */
  virtual size_t GetTrackedMemoryBytes() const = 0;

  // --- 2. 脫水與復水生命週期通知 (Dehydration & Rehydration Lifecycle) ---
  /**
   * @brief 當物件成功脫水（記憶體肉體釋放）時由核心通知
   * @param id 物件 HandleID
   * @note 純狀態通報。模組若有列管該 id，可據此更新其脫水狀態與扣減記憶體統計；未列管物件請直接忽略。
   */
  virtual void OnObjectDehydrated(HandleID id) = 0;

  /**
   * @brief 當物件被復水（重新載入記憶體）時由核心通知
   * @param id 物件 HandleID
   * @note 純狀態通報，絕不改變物件列管狀態。模組若有列管該 id，可據此恢復記憶體統計與候選佇列；未列管物件請直接忽略。
   */
  virtual void OnObjectRehydrated(HandleID id) = 0;

  // --- 3. 脫水調度入口 ---
  /**
   * @brief 觸發一輪自動脫水掃描與評估
   * @param target_bytes_to_free 期望釋放的記憶體位元組數：
   *        - 若為 0：常態常規巡檢，依據模組自身配額門檻或預設批次大小執行。
   *        - 若 > 0：緊急/需求驅動（例如 OOM 緊急自救），繞過常規配額門檻，
   *                  優先淘汰最冷物件直到釋放量達到目標或所有候選者耗盡。
   * @return 本輪脫水的詳細成效報告 (DehydrationReport)
   */
  virtual DehydrationReport TriggerDehydration(size_t target_bytes_to_free = 0) = 0;
};

}  // namespace ork
