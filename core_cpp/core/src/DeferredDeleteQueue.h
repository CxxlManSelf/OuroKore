#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "ourokore/base/ThreadPool.hpp"
#include "ourokore/c_api/core.h"

namespace ork
{

/**
 * @brief 延遲物理銷毀佇列 (Deferred Deletion Queue)
 *
 * 專職負責將 StrongCount == 0 的物件移交背景執行緒池進行物理銷毀。
 * 核心價值：
 * 1. 壓平深層物件樹之連鎖遞迴解構，徹底避免呼叫堆疊溢位 (Stack Overflow)。
 * 2. 藉由多執行緒平行銷毀獨立的堆積記憶體，消除主執行緒卡頓。
 */
class DeferredDeleteQueue
{
public:
  static DeferredDeleteQueue &GetInstance();

  DeferredDeleteQueue(const DeferredDeleteQueue &) = delete;
  DeferredDeleteQueue &operator=(const DeferredDeleteQueue &) = delete;

  /**
   * @brief 啟動延遲銷毀執行緒池
   * @param worker_threads 工作執行緒數量（預設 2）
   */
  void Start(size_t worker_threads = 2);

  /**
   * @brief 停止延遲銷毀佇列並等待所有已排隊的銷毀任務完成
   */
  void Stop();

  /**
   * @brief 將 StrongCount == 0 的物件 ID 推入銷毀佇列 (O(1) 常數時間非阻塞)
   * @param id 物件 HandleID
   */
  void Push(HandleID id);

  /**
   * @brief 同步等待目前所有已排入佇列的銷毀任務全部完成（供單元測試或特定同步情境使用）
   */
  void Flush();

  /**
   * @brief 設定是否為同步即時銷毀模式（主要用於單元測試情境）
   */
  void SetSyncMode(bool sync_mode)
  {
    m_sync_mode.store(sync_mode, std::memory_order_release);
  }

  bool IsSyncMode() const
  {
    return m_sync_mode.load(std::memory_order_acquire);
  }

  /**
   * @brief 取得目前正在排隊的任務數量
   */
  size_t PendingCount() const;

private:
  DeferredDeleteQueue();
  ~DeferredDeleteQueue();

  void ProcessItem(HandleID id);

  mutable std::mutex m_lifecycle_mutex;
  std::unique_ptr<base::FixedThreadPool> m_thread_pool;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_sync_mode{false};
  std::atomic<size_t> m_pending_tasks{0};
  mutable std::mutex m_flush_mutex;
  std::condition_variable m_flush_cv;
};

}  // namespace ork
