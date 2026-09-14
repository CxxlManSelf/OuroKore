#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ourokore/c_api/core.h"

namespace ork
{

/**
 * @brief 循環參照收集器 (Cycle Collector)
 *
 * 專職負責將無根循環參照孤島（Cyclic Garbage）進行外科手術式解開與純物理銷毀。
 *
 * 架構規範：
 * 1. 嚴格單一背景執行緒：消滅反向走訪時產生的鎖順序死鎖（哲學家就餐問題）與快取顛簸。
 * 2. 嚴格純物理銷毀：嚴禁觸碰脫水（Dehydration）與藍圖打包（PackBlueprint）。
 * 3. 雙層標記防禦：
 *    - visited 集合：單次走訪防無限循環。
 *    - 已知存活快取：碰觸到 ORK_ROOT_ID 或已知存活節點時 Early Exit。
 * 4. 併發變更防禦（二階段確認）：
 *    - 判定為孤島後，按 HandleID 升冪排序鎖定名冊，確認無外部連線後標記 Destructing 態。
 *    - 靜音模式解除內部連線，強引用自然跌至 0，交由 DeferredDeleteQueue 處理物理釋放。
 */
class CycleCollector
{
public:
  static CycleCollector &GetInstance();

  CycleCollector(const CycleCollector &) = delete;
  CycleCollector &operator=(const CycleCollector &) = delete;

  /**
   * @brief 啟動背景收集器執行緒
   */
  void Start();

  /**
   * @brief 停止收集器執行緒並等待退出
   */
  void Stop();

  /**
   * @brief 將可疑物件推入嫌疑犯佇列 (O(1) 常數時間)
   * 由 ork_unregister_edge 在 StrongCount > 0 且 CAS 成功搶入時呼叫。
   */
  void PushSuspect(HandleID target_id);

  /**
   * @brief 明確執行一次循環收集處理（同步阻塞，供單元測試或手動排程使用）
   */
  void CollectCyclesExplicit();

  /**
   * @brief 取得當前佇列中排隊的嫌疑犯數量
   */
  size_t SuspectCount() const;

private:
  CycleCollector();
  ~CycleCollector();

  void WorkerLoop();
  void ProcessSuspect(HandleID suspect_id);
  void DestructIsland(const std::vector<HandleID> &island_nodes);

  std::atomic<bool> m_running{false};
  std::vector<HandleID> m_suspect_queue;
  mutable std::mutex m_queue_mutex;
  std::condition_variable m_cv;
  std::thread m_worker_thread;

  // 走訪時單執行緒私有的本地容器（零鎖開銷）
  std::unordered_set<HandleID> m_visited_in_run;
  std::unordered_set<HandleID> m_known_alive_in_run;
};

}  // namespace ork
