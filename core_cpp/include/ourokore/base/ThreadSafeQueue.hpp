#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

namespace ork::base
{

/**
 * @brief 執行緒安全阻塞佇列 (Thread-Safe Blocking MPMC Queue)
 *
 * 支援多生產者-多消費者 (MPMC)，具備阻塞等待、逾時等待與優雅關閉喚醒機制。
 *
 * @tparam T 存放的元素型別
 */
template <typename T>
class ThreadSafeQueue
{
public:
  ThreadSafeQueue() = default;
  ~ThreadSafeQueue()
  {
    stop();
  }

  ThreadSafeQueue(const ThreadSafeQueue &) = delete;
  ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;
  ThreadSafeQueue(ThreadSafeQueue &&) = delete;
  ThreadSafeQueue &operator=(ThreadSafeQueue &&) = delete;

  /**
   * @brief 推送元素至佇列尾端
   * @param item 要放入的元素
   * @return 若成功推入返回 true；若佇列已停止則返回 false
   */
  bool push(T item)
  {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_stopped)
      {
        return false;
      }
      m_queue.push(std::move(item));
    }
    m_cv.notify_one();
    return true;
  }

  /**
   * @brief 原地構造並推入元素
   */
  template <typename... Args>
  bool emplace(Args &&...args)
  {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_stopped)
      {
        return false;
      }
      m_queue.emplace(std::forward<Args>(args)...);
    }
    m_cv.notify_one();
    return true;
  }

  /**
   * @brief 阻塞取出隊首元素
   * @param[out] out_val 取出的元素
   * @return 成功取出返回 true；若佇列已停止且為空則返回 false
   */
  bool pop(T &out_val)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_stopped || !m_queue.empty(); });
    if (m_queue.empty())
    {
      return false;
    }
    out_val = std::move(m_queue.front());
    m_queue.pop();
    return true;
  }

  /**
   * @brief 在指定逾時時間內嘗試取出隊首元素
   * @tparam Rep 時間數值型別
   * @tparam Period 時間單位
   * @param[out] out_val 取出的元素
   * @param rel_time 最長等待時間
   * @return 成功取出返回 true；逾時或佇列已停止且空返回 false
   */
  template <typename Rep, typename Period>
  bool pop_for(T &out_val, const std::chrono::duration<Rep, Period> &rel_time)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cv.wait_for(lock, rel_time, [this]() { return m_stopped || !m_queue.empty(); }))
    {
      return false;
    }
    if (m_queue.empty())
    {
      return false;
    }
    out_val = std::move(m_queue.front());
    m_queue.pop();
    return true;
  }

  /**
   * @brief 非阻塞嘗試取出隊首元素
   * @param[out] out_val 取出的元素
   * @return 成功取出返回 true；佇列為空或已停止返回 false
   */
  bool try_pop(T &out_val)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_queue.empty())
    {
      return false;
    }
    out_val = std::move(m_queue.front());
    m_queue.pop();
    return true;
  }

  /**
   * @brief 停止佇列並喚醒所有等待中的執行緒
   */
  void stop()
  {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_stopped = true;
    }
    m_cv.notify_all();
  }

  /**
   * @brief 清空佇列中的所有剩餘元素
   */
  void clear()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    std::queue<T> empty;
    std::swap(m_queue, empty);
  }

  /**
   * @brief 查詢佇列是否為空
   */
  [[nodiscard]] bool empty() const
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.empty();
  }

  /**
   * @brief 查詢佇列目前的元素數量
   */
  [[nodiscard]] size_t size() const
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_queue.size();
  }

  /**
   * @brief 查詢佇列是否已處於停止狀態
   */
  [[nodiscard]] bool is_stopped() const
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_stopped;
  }

private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::queue<T> m_queue;
  bool m_stopped{false};
};

}  // namespace ork::base
