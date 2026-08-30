#pragma once

#include <cstddef>
#include <cstdint>

namespace ork
{

using HandleID = uint64_t;

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

  // --- 1. 模組生命週期 ---
  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual bool IsRunning() const = 0;

  // --- 2. 名冊與大小追蹤 (Info Stream) ---
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

  // --- 3. 存取監聽勾點 ---
  /**
   * @brief 物件存取通知勾點（供未來 LRU 或熱度統計策略使用）
   */
  virtual void OnObjectAccess(HandleID id) = 0;

  // --- 4. 脫水調度入口 ---
  /**
   * @brief 觸發一輪自動脫水掃描與評估
   * @return 本輪成功脫水的物件數量
   */
  virtual size_t TriggerDehydration() = 0;
};

}  // namespace ork
