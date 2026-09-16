#include "DeferredDeleteQueue.h"

#include "ControlBlock.h"
#include "Registry.h"

namespace ork
{

DeferredDeleteQueue &DeferredDeleteQueue::GetInstance()
{
  static DeferredDeleteQueue *instance = new DeferredDeleteQueue();
  return *instance;
}

DeferredDeleteQueue::DeferredDeleteQueue()
{
  Start();
}

DeferredDeleteQueue::~DeferredDeleteQueue()
{
  Stop();
}

void DeferredDeleteQueue::Start(size_t worker_threads)
{
  if (m_running.load(std::memory_order_acquire))
  {
    return;
  }

  m_running.store(true, std::memory_order_release);
  m_thread_pool = std::make_unique<base::FixedThreadPool>(worker_threads);
}

void DeferredDeleteQueue::Stop()
{
  if (!m_running.load(std::memory_order_acquire))
  {
    return;
  }

  // 先等待所有排隊任務完成
  Flush();

  m_running.store(false, std::memory_order_release);
  if (m_thread_pool)
  {
    m_thread_pool->stop();
    m_thread_pool.reset();
  }
}

void DeferredDeleteQueue::Push(HandleID id)
{
  if (id == ORK_ROOT_ID)
  {
    return;
  }

  // 無論後續是就地同步執行或非同步排隊，統一在此遞增任務計數，
  // 確保與 ProcessItem 結尾的 fetch_sub(1) 嚴格成對，防止同步模式下溢 (Underflow)
  m_pending_tasks.fetch_add(1, std::memory_order_relaxed);

  if (m_sync_mode.load(std::memory_order_acquire))
  {
    // 同步模式：直接就地執行銷毀
    ProcessItem(id);
    return;
  }

  bool submitted = false;
  if (m_thread_pool && m_running.load(std::memory_order_acquire))
  {
    submitted = m_thread_pool->submit_detached([this, id]() { ProcessItem(id); });
  }

  if (!submitted)
  {
    // 若執行緒池未啟動、已停止或佇列拒絕，降級為當前執行緒就地銷毀，確保任務必達且計數不洩漏
    ProcessItem(id);
  }
}

void DeferredDeleteQueue::ProcessItem(HandleID id)
{
  ControlBlock *cb = Registry::GetInstance().GetControlBlock(id);
  if (cb)
  {
    // 第一階段：多執行緒平行物理銷毀 Payload
    // 壓平遞迴解構：若 payload 解構時引發子物件 ork_unregister_edge 且 Strong==0，
    // 子物件會被 Push 進 DeferredDeleteQueue，而不是在當前呼叫棧深處遞迴！
    OuroObject *to_delete = nullptr;
    bool should_notify = false;
    {
      std::unique_lock<std::shared_mutex> payload_lock(cb->m_rw_lock);
      if (cb->m_strong_count.load(std::memory_order_acquire) == 0)
      {
        if (cb->m_payload)
        {
          to_delete = cb->m_payload;
          cb->m_payload = nullptr;
        }
        bool expected = false;
        if (cb->m_destruction_notified.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
          should_notify = true;
        }
      }
    }

    if (to_delete)
    {
      delete to_delete;
    }

    // 邏輯銷毀通知：在無任何核心/讀寫鎖保護下觸發全域銷毀回呼（通知 IAutoDehydrator 與 IStorageDriver）
    if (should_notify)
    {
      Registry::GetInstance().NotifyObjectDestroyed(id);
    }

    // 第二階段：若 WeakCount 亦為 0，自 Registry 抹除 ControlBlock
    Registry::GetInstance().DestroyControlBlockIfDead(id);
  }

  // 任務計數遞減並喚醒可能的 Flush 等待者
  size_t prev = m_pending_tasks.fetch_sub(1, std::memory_order_acq_rel);
  if (prev == 1)
  {
    std::lock_guard<std::mutex> lock(m_flush_mutex);
    m_flush_cv.notify_all();
  }
}

void DeferredDeleteQueue::Flush()
{
  std::unique_lock<std::mutex> lock(m_flush_mutex);
  m_flush_cv.wait(lock, [this]() { return m_pending_tasks.load(std::memory_order_acquire) == 0; });
}

size_t DeferredDeleteQueue::PendingCount() const
{
  return m_pending_tasks.load(std::memory_order_relaxed);
}

}  // namespace ork
