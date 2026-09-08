#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "IAutoDehydrator.hpp"
#include "OuroCore.hpp"
#include "ourokore/base/Semaphore.hpp"

namespace ork
{

/**
 * @brief OuroKore 內建 LRU（最近最少使用）自動脫水器
 *
 * 核心特性：
 * 1. O(1) 雙向鏈結串列與 Hash 表維護存取熱度。
 * 2. 脫水時從最冷端向熱端推進，優先釋放最久未使用的物件。
 * 3. 內建 base::Event 背景定時排程引擎，支援週期掃描與無延遲優雅退出。
 * 4. 支援記憶體配額限制（Memory Quota），精確脫水至安全容量。
 * 5. 結合 In-Flight 保護，安全跳過正在使用中的活躍物件。
 */
class OuroLRUAutoDehydrator : public IAutoDehydrator
{
public:
  struct TrackedNode
  {
    HandleID id{0};
    size_t size_bytes{0};
    bool is_dehydrated{false};
    std::list<HandleID>::iterator lru_iter;
  };

  /**
   * @brief 建構子
   * @param interval 背景排程週期（預設 3000ms，0 表示不自動啟動背景執行緒）
   * @param memory_limit_bytes 記憶體列管上限（預設 0 表示不設上限，依排程批次脫水）
   * @param batch_size 單次無上限脫水時的預設批次數量（預設 10）
   */
  explicit OuroLRUAutoDehydrator(std::chrono::milliseconds interval = std::chrono::milliseconds(0),
                                size_t memory_limit_bytes = 0,
                                size_t batch_size = 10) :
      m_interval(interval),
      m_memory_limit_bytes(memory_limit_bytes),
      m_batch_size(batch_size > 0 ? batch_size : 1),
      m_wake_event(ork::base::EventResetMode::AutoReset, false)
  {
    if (m_interval.count() > 0)
    {
      Start(m_interval);
    }
  }

  /**
   * @brief 解構子（RAII 優雅關閉背景執行緒）
   */
  ~OuroLRUAutoDehydrator() override
  {
    Stop();
  }

  OuroLRUAutoDehydrator(const OuroLRUAutoDehydrator &) = delete;
  OuroLRUAutoDehydrator &operator=(const OuroLRUAutoDehydrator &) = delete;
  OuroLRUAutoDehydrator(OuroLRUAutoDehydrator &&) = delete;
  OuroLRUAutoDehydrator &operator=(OuroLRUAutoDehydrator &&) = delete;

  // =========================================================================
  // --- IAutoDehydrator SPI 介面實作 ---
  // =========================================================================

  /**
   * @brief 將物件納入 LRU 候選名冊，放置於熱端（MRU Head）
   */
  void Register(HandleID id, size_t size_bytes) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_node_map.find(id);
    if (it != m_node_map.end())
    {
      if (!it->second.is_dehydrated)
      {
        m_tracked_memory_bytes -= it->second.size_bytes;
      }
      it->second.size_bytes = size_bytes;
      it->second.is_dehydrated = false;
      m_tracked_memory_bytes += size_bytes;
      m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second.lru_iter);
      return;
    }

    m_lru_list.push_front(id);
    TrackedNode node;
    node.id = id;
    node.size_bytes = size_bytes;
    node.is_dehydrated = false;
    node.lru_iter = m_lru_list.begin();

    m_node_map[id] = node;
    m_tracked_memory_bytes += size_bytes;
  }

  /**
   * @brief 從名冊中移除物件並扣減記憶體容量統計
   */
  void Unregister(HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_node_map.find(id);
    if (it != m_node_map.end())
    {
      if (!it->second.is_dehydrated)
      {
        m_tracked_memory_bytes -= it->second.size_bytes;
      }
      m_lru_list.erase(it->second.lru_iter);
      m_node_map.erase(it);
    }
  }

  /**
   * @brief 查詢物件是否受到列管
   */
  bool IsTracked(HandleID id) const override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_node_map.find(id) != m_node_map.end();
  }

  /**
   * @brief 取得目前列管活體物件的總位元組數（已脫水者不計入）
   */
  size_t GetTrackedMemoryBytes() const override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracked_memory_bytes;
  }

  /**
   * @brief 核心回報物件已脫水
   */
  void OnObjectDehydrated(HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_node_map.find(id);
    if (it != m_node_map.end() && !it->second.is_dehydrated)
    {
      it->second.is_dehydrated = true;
      if (m_tracked_memory_bytes >= it->second.size_bytes)
      {
        m_tracked_memory_bytes -= it->second.size_bytes;
      }
      else
      {
        m_tracked_memory_bytes = 0;
      }
    }
  }

  /**
   * @brief 核心回報物件已復水，更新熱度至 LRU 最前端 (MRU)
   */
  void OnObjectRehydrated(HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_node_map.find(id);
    if (it != m_node_map.end())
    {
      if (it->second.is_dehydrated)
      {
        it->second.is_dehydrated = false;
        m_tracked_memory_bytes += it->second.size_bytes;
      }
      m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second.lru_iter);
    }
  }

  /**
   * @brief 觸發一輪 LRU 脫水評估
   *
   * 從鏈結串列最冷端（LRU Tail）向熱端評估，優先脫水最久未使用的活體物件。
   * @return 本輪成功脫水的物件數量
   */
  size_t TriggerDehydration() override
  {
    std::vector<HandleID> candidates;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_node_map.empty())
      {
        return 0;
      }

      bool check_quota = (m_memory_limit_bytes > 0);
      size_t quota = m_memory_limit_bytes;
      size_t current_mem = m_tracked_memory_bytes;

      // 若設定了配額且目前未超標，則無須脫水
      if (check_quota && current_mem <= quota)
      {
        return 0;
      }

      // 從尾端（最冷資料）向前收集未脫水的候選物件
      for (auto it = m_lru_list.rbegin(); it != m_lru_list.rend(); ++it)
      {
        HandleID id = *it;
        auto node_it = m_node_map.find(id);
        if (node_it != m_node_map.end() && !node_it->second.is_dehydrated)
        {
          candidates.push_back(id);

          if (check_quota)
          {
            if (current_mem > node_it->second.size_bytes)
            {
              current_mem -= node_it->second.size_bytes;
            }
            else
            {
              current_mem = 0;
            }
            if (current_mem <= quota)
            {
              break;
            }
          }
          else
          {
            if (candidates.size() >= m_batch_size)
            {
              break;
            }
          }
        }
      }
    }

    // 在釋放內部互斥鎖的情況下呼叫核心 Dehydrate，防範死鎖
    size_t successful_dehydrations = 0;
    std::vector<HandleID> failed_ids;

    for (HandleID id : candidates)
    {
      if (ork::Dehydrate(id))
      {
        ++successful_dehydrations;
      }
      else
      {
        failed_ids.push_back(id);
      }
    }

    // 若有物件脫水失敗（通常是因為 In-Flight 活躍使用中或暫時鎖定），
    // 將其移至 MRU 隊首重新排隊，避免長期霸佔隊尾導致後續冷物件發生飢餓與卡死 (Head-of-Line Blocking)
    if (!failed_ids.empty())
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (HandleID id : failed_ids)
      {
        auto it = m_node_map.find(id);
        if (it != m_node_map.end())
        {
          m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second.lru_iter);
        }
      }
    }

    m_total_dehydrated_count.fetch_add(successful_dehydrations, std::memory_order_relaxed);
    m_total_runs.fetch_add(1, std::memory_order_relaxed);
    return successful_dehydrations;
  }

  // =========================================================================
  // --- 背景排程與配置管理介面 ---
  // =========================================================================

  /**
   * @brief 啟動背景定時脫水執行緒
   * @param interval 掃描週期（毫秒）
   * @return 若成功啟動返回 true；若已在運行則返回 false
   */
  bool Start(std::chrono::milliseconds interval)
  {
    if (interval.count() <= 0)
    {
      return false;
    }

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
      return false;
    }

    m_interval = interval;
    m_worker_thread = std::thread([this]() { worker_loop(); });
    return true;
  }

  /**
   * @brief 停止背景定時執行緒（無延遲即時喚醒並 join）
   */
  void Stop()
  {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
      m_wake_event.set();
      if (m_worker_thread.joinable())
      {
        m_worker_thread.join();
      }
    }
  }

  /**
   * @brief 手動立即喚醒背景執行緒執行一輪脫水掃描
   */
  void TriggerNow()
  {
    if (m_running.load(std::memory_order_acquire))
    {
      m_wake_event.set();
    }
    else
    {
      TriggerDehydration();
    }
  }

  /**
   * @brief 設定記憶體上限配額（位元組）
   * @param limit_bytes 上限值（0 表示不設容量上限，純週期性脫水）
   */
  void SetMemoryLimit(size_t limit_bytes)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_memory_limit_bytes = limit_bytes;
  }

  /**
   * @brief 取得記憶體上限配額
   */
  size_t GetMemoryLimit() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_memory_limit_bytes;
  }

  /**
   * @brief 設定無上限時的單次掃描脫水批次大小
   */
  void SetBatchSize(size_t batch_size)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batch_size = (batch_size > 0 ? batch_size : 1);
  }

  /**
   * @brief 取得批次大小
   */
  size_t GetBatchSize() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_batch_size;
  }

  /**
   * @brief 查詢背景執行緒是否運行中
   */
  bool IsRunning() const noexcept
  {
    return m_running.load(std::memory_order_relaxed);
  }

  /**
   * @brief 取得目前排程週期
   */
  std::chrono::milliseconds GetInterval() const noexcept
  {
    return m_interval;
  }

  /**
   * @brief 取得累計脫水成功物件總數
   */
  uint64_t GetTotalDehydratedCount() const noexcept
  {
    return m_total_dehydrated_count.load(std::memory_order_relaxed);
  }

  /**
   * @brief 取得累計觸發執行的輪數
   */
  uint64_t GetTotalRuns() const noexcept
  {
    return m_total_runs.load(std::memory_order_relaxed);
  }

private:
  void worker_loop()
  {
    while (m_running.load(std::memory_order_acquire))
    {
      // 阻塞等待間隔時間，或被 TriggerNow / Stop 立即喚醒
      m_wake_event.wait_for(m_interval);

      if (!m_running.load(std::memory_order_acquire))
      {
        break;
      }

      TriggerDehydration();
    }
  }

  mutable std::mutex m_mutex;
  std::list<HandleID> m_lru_list;
  std::unordered_map<HandleID, TrackedNode> m_node_map;
  // 目前名冊中活體（未脫水）物件所佔用的總記憶體位元組數
  size_t m_tracked_memory_bytes{0};

  // 記憶體目標配額上限（位元組）；若為 0 表示不設上限，改依批次數量進行脫水
  size_t m_memory_limit_bytes{0};
  // 當未設定記憶體配額上限（0）時，單輪脫水預設處理的候選物件批次數量（預設 10）
  size_t m_batch_size{10};

  std::chrono::milliseconds m_interval{0};
  std::atomic<bool> m_running{false};
  std::thread m_worker_thread;
  ork::base::Event m_wake_event;

  std::atomic<uint64_t> m_total_dehydrated_count{0};
  std::atomic<uint64_t> m_total_runs{0};
};

}  // namespace ork
