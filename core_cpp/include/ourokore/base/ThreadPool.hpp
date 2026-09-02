#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "ThreadSafeQueue.hpp"

namespace ork::base
{

/**
 * @brief 固定數量執行緒池 (Fixed Thread Pool)
 *
 * 建立固定數量的工作執行緒，適用於 CPU 密集型運算或具有恆定併發需求的任務排程。
 */
class FixedThreadPool
{
public:
  using Task = std::function<void()>;

  /**
   * @brief 建構子
   * @param thread_count 執行緒數量（預設為硬體核心數，若無法取得則為 4）
   */
  explicit FixedThreadPool(size_t thread_count = 0)
  {
    if (thread_count == 0)
    {
      thread_count = std::thread::hardware_concurrency();
      if (thread_count == 0)
      {
        thread_count = 4;
      }
    }
    m_worker_count = thread_count;
    m_running.store(true, std::memory_order_release);

    m_workers.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i)
    {
      m_workers.emplace_back([this]() { worker_loop(); });
    }
  }

  /**
   * @brief 解構子（自動優雅關閉並等待所有已排隊任務完成）
   */
  ~FixedThreadPool()
  {
    stop();
  }

  FixedThreadPool(const FixedThreadPool &) = delete;
  FixedThreadPool &operator=(const FixedThreadPool &) = delete;
  FixedThreadPool(FixedThreadPool &&) = delete;
  FixedThreadPool &operator=(FixedThreadPool &&) = delete;

  /**
   * @brief 提交可調用物件並取得 Future 結果
   * @tparam F 可調用物件型別
   * @tparam Args 參數型別
   * @param f 函式或可調用物件
   * @param args 傳入參數
   * @return std::future<ReturnType>
   */
  template <typename F, typename... Args>
  auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
  {
    using ReturnType = std::invoke_result_t<F, Args...>;

    if (!m_running.load(std::memory_order_acquire))
    {
      throw std::runtime_error("FixedThreadPool has been stopped; cannot submit new tasks.");
    }

    auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
        [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
          return func(captured_args...);
        });

    std::future<ReturnType> future_result = task_ptr->get_future();

    bool success = m_task_queue.push([task_ptr]() { (*task_ptr)(); });
    if (!success)
    {
      throw std::runtime_error("FixedThreadPool task queue rejected the task (stopped).");
    }

    return future_result;
  }

  /**
   * @brief 提交無需等待結果之任務 (Fire-and-Forget)
   *
   * 避免 std::packaged_task 與 std::future 的內部堆積配置開銷。
   */
  template <typename F, typename... Args>
  bool submit_detached(F &&f, Args &&...args)
  {
    if (!m_running.load(std::memory_order_acquire))
    {
      return false;
    }

    auto task = [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
      func(captured_args...);
    };

    return m_task_queue.push(std::move(task));
  }

  /**
   * @brief 停止執行緒池並等待所有已排入佇列之任務完成
   */
  void stop()
  {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
      m_task_queue.stop();
      for (auto &worker : m_workers)
      {
        if (worker.joinable())
        {
          worker.join();
        }
      }
      m_workers.clear();
    }
  }

  /**
   * @brief 阻塞等待直到所有排隊任務與目前正在執行的任務皆已完成（空閒狀態）
   */
  void wait_idle()
  {
    std::unique_lock<std::mutex> lock(m_idle_mutex);
    m_idle_cv.wait(lock, [this]() {
      return m_task_queue.empty() && (m_active_workers.load(std::memory_order_acquire) == 0);
    });
  }

  /**
   * @brief 取得配置之 Worker 執行緒總數
   */
  [[nodiscard]] size_t get_worker_count() const noexcept
  {
    return m_worker_count;
  }

  /**
   * @brief 取得目前正在執行任務的 Worker 數量
   */
  [[nodiscard]] size_t get_active_worker_count() const noexcept
  {
    return m_active_workers.load(std::memory_order_relaxed);
  }

  /**
   * @brief 取得目前佇列中等待執行的任務數量
   */
  [[nodiscard]] size_t get_queue_size() const
  {
    return m_task_queue.size();
  }

  /**
   * @brief 查詢執行緒池是否處於運作狀態
   */
  [[nodiscard]] bool is_running() const noexcept
  {
    return m_running.load(std::memory_order_relaxed);
  }

private:
  void worker_loop()
  {
    while (true)
    {
      Task task;
      if (!m_task_queue.pop(task))
      {
        // 佇列停止且無殘留任務，退出 Worker
        break;
      }

      m_active_workers.fetch_add(1, std::memory_order_relaxed);
      try
      {
        if (task)
        {
          task();
        }
      }
      catch (...)
      {
        // 防禦性捕捉任務內未處理例外，確保 Worker 執行緒不致崩潰
      }
      m_active_workers.fetch_sub(1, std::memory_order_release);

      // 通知可能的 wait_idle 等待者
      if (m_task_queue.empty() && m_active_workers.load(std::memory_order_acquire) == 0)
      {
        std::lock_guard<std::mutex> lock(m_idle_mutex);
        m_idle_cv.notify_all();
      }
    }
  }

  size_t m_worker_count{0};
  std::vector<std::thread> m_workers;
  ThreadSafeQueue<Task> m_task_queue;

  std::atomic<bool> m_running{false};
  std::atomic<size_t> m_active_workers{0};

  mutable std::mutex m_idle_mutex;
  std::condition_variable m_idle_cv;
};

/**
 * @brief 動態伸縮執行緒池 (Dynamic / Elastic Thread Pool)
 *
 * 依據任務負載量自動增減 Worker 執行緒數量：
 * 1. 負載增加且現有 Worker 皆在忙碌時，動態擴增 Worker 直至 max_threads。
 * 2. 負載降低且 Worker 空閒超過指定逾時時間（idle_timeout）時，自動縮容回收執行緒直至 min_threads。
 */
class DynamicThreadPool
{
public:
  using Task = std::function<void()>;

  /**
   * @brief 建構子
   * @param min_threads 核心常駐執行緒數（預設 2）
   * @param max_threads 最大上限執行緒數（預設 硬體並發數 * 2 或最少 8）
   * @param idle_timeout Worker 空閒回收逾時（預設 3000 毫秒）
   */
  explicit DynamicThreadPool(size_t min_threads = 2,
                             size_t max_threads = 0,
                             std::chrono::milliseconds idle_timeout = std::chrono::milliseconds(3000))
      : m_min_threads(min_threads == 0 ? 1 : min_threads),
        m_idle_timeout(idle_timeout)
  {
    if (max_threads == 0)
    {
      size_t hw = std::thread::hardware_concurrency();
      max_threads = (hw > 0) ? (hw * 2) : 8;
    }
    if (max_threads < m_min_threads)
    {
      max_threads = m_min_threads;
    }
    m_max_threads = max_threads;

    m_running.store(true, std::memory_order_release);

    // 啟動核心常駐線程
    for (size_t i = 0; i < m_min_threads; ++i)
    {
      spawn_worker();
    }
  }

  /**
   * @brief 解構子（自動停止並等待所有工作執行緒退出）
   */
  ~DynamicThreadPool()
  {
    stop();
  }

  DynamicThreadPool(const DynamicThreadPool &) = delete;
  DynamicThreadPool &operator=(const DynamicThreadPool &) = delete;
  DynamicThreadPool(DynamicThreadPool &&) = delete;
  DynamicThreadPool &operator=(DynamicThreadPool &&) = delete;

  /**
   * @brief 提交任務並取得 Future 結果
   */
  template <typename F, typename... Args>
  auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
  {
    using ReturnType = std::invoke_result_t<F, Args...>;

    if (!m_running.load(std::memory_order_acquire))
    {
      throw std::runtime_error("DynamicThreadPool has been stopped; cannot submit new tasks.");
    }

    auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
        [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
          return func(captured_args...);
        });

    std::future<ReturnType> future_result = task_ptr->get_future();

    bool success = m_task_queue.push([task_ptr]() { (*task_ptr)(); });
    if (!success)
    {
      throw std::runtime_error("DynamicThreadPool task queue rejected the task (stopped).");
    }

    check_and_expand_workers();
    return future_result;
  }

  /**
   * @brief 提交非阻塞任務 (Fire-and-Forget)
   */
  template <typename F, typename... Args>
  bool submit_detached(F &&f, Args &&...args)
  {
    if (!m_running.load(std::memory_order_acquire))
    {
      return false;
    }

    auto task = [func = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
      func(captured_args...);
    };

    bool success = m_task_queue.push(std::move(task));
    if (success)
    {
      check_and_expand_workers();
    }
    return success;
  }

  /**
   * @brief 停止執行緒池並等待所有 Worker 執行緒終止
   */
  void stop()
  {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
      m_task_queue.stop();

      // 等待所有 Worker 執行緒退出
      std::unique_lock<std::mutex> lock(m_thread_lifecycle_mutex);
      m_all_threads_done_cv.wait(lock, [this]() {
        return m_current_threads.load(std::memory_order_acquire) == 0;
      });
    }
  }

  /**
   * @brief 阻塞等待直到所有排隊任務與目前正在執行的任務皆已完成
   */
  void wait_idle()
  {
    std::unique_lock<std::mutex> lock(m_idle_mutex);
    m_idle_cv.wait(lock, [this]() {
      return m_task_queue.empty() && (m_active_workers.load(std::memory_order_acquire) == 0);
    });
  }

  /**
   * @brief 取得當前存活的 Worker 執行緒數量
   */
  [[nodiscard]] size_t get_current_worker_count() const noexcept
  {
    return m_current_threads.load(std::memory_order_relaxed);
  }

  /**
   * @brief 取得核心最小執行緒數
   */
  [[nodiscard]] size_t get_min_threads() const noexcept
  {
    return m_min_threads;
  }

  /**
   * @brief 取得最大上限執行緒數
   */
  [[nodiscard]] size_t get_max_threads() const noexcept
  {
    return m_max_threads;
  }

  /**
   * @brief 取得目前正在執行任務的 Worker 數量
   */
  [[nodiscard]] size_t get_active_worker_count() const noexcept
  {
    return m_active_workers.load(std::memory_order_relaxed);
  }

  /**
   * @brief 取得目前佇列中等待執行的任務數量
   */
  [[nodiscard]] size_t get_queue_size() const
  {
    return m_task_queue.size();
  }

  /**
   * @brief 查詢執行緒池是否處於運作狀態
   */
  [[nodiscard]] bool is_running() const noexcept
  {
    return m_running.load(std::memory_order_relaxed);
  }

private:
  void spawn_worker()
  {
    m_current_threads.fetch_add(1, std::memory_order_acq_rel);
    std::thread([this]() { worker_loop(); }).detach();
  }

  void check_and_expand_workers()
  {
    // 如果佇列有任務且現有 Worker 都在忙碌，且尚未達到 max_threads 上限，則建立新 Worker
    size_t current = m_current_threads.load(std::memory_order_acquire);
    size_t active = m_active_workers.load(std::memory_order_acquire);

    if (active >= current && current < m_max_threads)
    {
      std::lock_guard<std::mutex> lock(m_thread_lifecycle_mutex);
      if (m_current_threads.load(std::memory_order_acquire) < m_max_threads)
      {
        spawn_worker();
      }
    }
  }

  void worker_loop()
  {
    while (true)
    {
      Task task;
      bool has_task = false;

      if (!m_running.load(std::memory_order_acquire))
      {
        // 系統已要求停止，非阻塞嘗試清空殘留任務
        has_task = m_task_queue.try_pop(task);
        if (!has_task)
        {
          break;
        }
      }
      else
      {
        // 正常運行：嘗試在 idle_timeout 內等待任務
        has_task = m_task_queue.pop_for(task, m_idle_timeout);
        if (!has_task)
        {
          // 逾時未取到任務：判斷是否需要縮容
          if (!m_running.load(std::memory_order_acquire))
          {
            break;
          }

          size_t curr = m_current_threads.load(std::memory_order_acquire);
          if (curr > m_min_threads)
          {
            // 超過核心線程數，退出並回收本執行緒
            break;
          }
          else
          {
            // 處於核心線程數以內，繼續下一輪等待
            continue;
          }
        }
      }

      if (has_task && task)
      {
        m_active_workers.fetch_add(1, std::memory_order_relaxed);
        try
        {
          task();
        }
        catch (...)
        {
          // 防禦未捕捉之異常
        }
        m_active_workers.fetch_sub(1, std::memory_order_release);

        if (m_task_queue.empty() && m_active_workers.load(std::memory_order_acquire) == 0)
        {
          std::lock_guard<std::mutex> lock(m_idle_mutex);
          m_idle_cv.notify_all();
        }
      }
    }

    // 執行緒即將終止退出，更新計數並喚醒可能的等待者
    size_t remaining = m_current_threads.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0)
    {
      std::lock_guard<std::mutex> lock(m_thread_lifecycle_mutex);
      m_all_threads_done_cv.notify_all();
    }
  }

  size_t m_min_threads{2};
  size_t m_max_threads{8};
  std::chrono::milliseconds m_idle_timeout{3000};

  ThreadSafeQueue<Task> m_task_queue;

  std::atomic<bool> m_running{false};
  std::atomic<size_t> m_current_threads{0};
  std::atomic<size_t> m_active_workers{0};

  mutable std::mutex m_thread_lifecycle_mutex;
  std::condition_variable m_all_threads_done_cv;

  mutable std::mutex m_idle_mutex;
  std::condition_variable m_idle_cv;
};

}  // namespace ork::base
