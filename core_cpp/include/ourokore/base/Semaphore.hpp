#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace ork::base
{

/**
 * @brief 計數訊號量 (Counting Semaphore)
 *
 * 封裝跨平台計數訊號量，支援阻塞獲取、帶逾時獲取、非阻塞獲取與批次釋放。
 */
class Semaphore
{
public:
  /**
   * @brief 建構子
   * @param initial_count 初始可用資源計數（預設為 0）
   */
  explicit Semaphore(ptrdiff_t initial_count = 0) :
      m_count(initial_count)
  {
  }

  ~Semaphore() = default;

  Semaphore(const Semaphore &) = delete;
  Semaphore &operator=(const Semaphore &) = delete;
  Semaphore(Semaphore &&) = delete;
  Semaphore &operator=(Semaphore &&) = delete;

  /**
   * @brief 釋放資源並增加計數
   * @param update 釋放的數量（預設為 1）
   */
  void release(ptrdiff_t update = 1)
  {
    if (update <= 0)
    {
      return;
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_count += update;
    if (update == 1)
    {
      m_cv.notify_one();
    }
    else
    {
      m_cv.notify_all();
    }
  }

  /**
   * @brief 獲取一個資源（若計數為 0 則阻塞等待）
   */
  void acquire()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_count > 0; });
    --m_count;
  }

  /**
   * @brief 嘗試獲取一個資源（非阻塞）
   * @return 成功獲取返回 true，無可用資源返回 false
   */
  bool try_acquire()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_count > 0)
    {
      --m_count;
      return true;
    }
    return false;
  }

  /**
   * @brief 在指定逾時時間內嘗試獲取一個資源
   * @tparam Rep 時間數值型別
   * @tparam Period 時間單位
   * @param rel_time 等待持續時間
   * @return 成功獲取返回 true，逾時返回 false
   */
  template <typename Rep, typename Period>
  bool try_acquire_for(const std::chrono::duration<Rep, Period> &rel_time)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cv.wait_for(lock, rel_time, [this]() { return m_count > 0; }))
    {
      return false;
    }
    --m_count;
    return true;
  }

  /**
   * @brief 在指定時間點前嘗試獲取一個資源
   * @tparam Clock 時鐘型別
   * @tparam Duration 持續時間型別
   * @param abs_time 目標時間點
   * @return 成功獲取返回 true，逾時返回 false
   */
  template <typename Clock, typename Duration>
  bool try_acquire_until(const std::chrono::time_point<Clock, Duration> &abs_time)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cv.wait_until(lock, abs_time, [this]() { return m_count > 0; }))
    {
      return false;
    }
    --m_count;
    return true;
  }

  /**
   * @brief 取得目前可用資源計數（即時快照）
   */
  [[nodiscard]] ptrdiff_t available() const
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_count;
  }

private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  ptrdiff_t m_count{0};
};

/**
 * @brief 事件通知重設模式
 */
enum class EventResetMode
{
  AutoReset,   ///< 自動重設：單一等待執行緒被喚醒後，自動重設為未觸發狀態
  ManualReset  ///< 手動重設：所有等待執行緒均被喚醒，需手動呼叫 reset() 才會回到未觸發狀態
};

/**
 * @brief 事件同步原語 (Event Primitive)
 *
 * 支援跨平台 AutoReset 與 ManualReset 兩種模式，適用於執行緒啟動同步、關閉通知或事件廣播。
 */
class Event
{
public:
  /**
   * @brief 建構子
   * @param mode 重設模式（AutoReset 或 ManualReset）
   * @param initially_signaled 初始是否處於觸發狀態（預設 false）
   */
  explicit Event(EventResetMode mode = EventResetMode::AutoReset, bool initially_signaled = false) :
      m_mode(mode),
      m_signaled(initially_signaled)
  {
  }

  ~Event() = default;

  Event(const Event &) = delete;
  Event &operator=(const Event &) = delete;
  Event(Event &&) = delete;
  Event &operator=(Event &&) = delete;

  /**
   * @brief 觸發事件（將事件設為 Signaled）
   */
  void set()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_signaled = true;
    if (m_mode == EventResetMode::AutoReset)
    {
      m_cv.notify_one();
    }
    else
    {
      m_cv.notify_all();
    }
  }

  /**
   * @brief 重設事件為未觸發狀態（Non-Signaled）
   */
  void reset()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_signaled = false;
  }

  /**
   * @brief 阻塞等待事件觸發
   */
  void wait()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_signaled; });
    if (m_mode == EventResetMode::AutoReset)
    {
      m_signaled = false;
    }
  }

  /**
   * @brief 逾時等待事件觸發
   * @param rel_time 最長等待時間
   * @return 成功被觸發返回 true，逾時返回 false
   */
  template <typename Rep, typename Period>
  bool wait_for(const std::chrono::duration<Rep, Period> &rel_time)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cv.wait_for(lock, rel_time, [this]() { return m_signaled; }))
    {
      return false;
    }
    if (m_mode == EventResetMode::AutoReset)
    {
      m_signaled = false;
    }
    return true;
  }

  /**
   * @brief 查詢目前是否處於觸發狀態（不清除狀態）
   */
  [[nodiscard]] bool is_set() const
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_signaled;
  }

private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  EventResetMode m_mode;
  bool m_signaled{false};
};

}  // namespace ork::base
