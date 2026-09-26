#pragma once

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "ourokore/component/OuroObject.hpp"

namespace ork
{

using RehydrateFn = OuroObject *(*)(HandleID id);
using DestroyFn = void (*)(OuroObject *payload);

/**
 * @brief 私有控制區塊，代表受管理物件的「核心」或「墓碑」。
 * 它負責管理生命週期、參考計數、讀寫鎖，以及擁有者追蹤。
 */
struct ControlBlock
{
  std::atomic<uint32_t> m_strong_count{0};
  std::atomic<uint32_t> m_weak_count{0};
  std::atomic<uint8_t> m_storage_state{0};  // 0: UnsavedNew, 1: Clean, 2: Dirty, 3: Dehydrated

  // Shared mutex supporting the two-stage locking contract
  // 支援兩階段鎖定協定的共享互斥鎖
  std::shared_mutex m_rw_lock;

  // Raw pointer to the physical object (the body) - atomic for thread-safe access
  std::atomic<OuroObject *> m_payload{nullptr};

  // Auto-rehydration function pointer callback (persisted across dehydration)
  RehydrateFn m_rehydrate_fn{nullptr};

  // In-place deleter callback function pointer (executes in creating module's CRT)
  DestroyFn m_destroy_fn{nullptr};

  // Owner ID Roster for Upstream Cycle Search
  std::vector<HandleID> m_owners;
  std::mutex m_owners_mutex;

  // 循環參照嫌疑犯佇列防重複入隊原子旗標 (CAS Guard)
  std::atomic<bool> m_in_suspect_queue{false};

  // 孤島拆解中旗標（防止併發加邊與殭屍復活）
  std::atomic<bool> m_is_destructing{false};

  // 銷毀通知已觸發原子旗標（保證邏輯銷毀通知只觸發一次，防重複通知）
  std::atomic<bool> m_destruction_notified{false};

  // 兩階段構造預留旗標（預留期間強弱計數與 payload 皆為 0，嚴禁任何回收機制誤殺）
  std::atomic<bool> m_is_reserved{false};

  explicit ControlBlock(OuroObject *payload = nullptr, DestroyFn destroy_fn = nullptr) :
      m_payload(payload),
      m_destroy_fn(destroy_fn)
  {}

  /**
   * @brief 安全釋放 Payload 記憶體，必定回到物件所屬模組的 CRT 堆疊釋放
   */
  void DeletePayload()
  {
    OuroObject *to_delete = m_payload.exchange(nullptr, std::memory_order_acq_rel);
    if (to_delete)
    {
      DestroyFn destroy_fn = m_destroy_fn;
      m_destroy_fn = nullptr;
      if (destroy_fn)
      {
        destroy_fn(to_delete);
      }
      else
      {
        to_delete->DestroySelf();
      }
    }
  }

  ~ControlBlock()
  {
    DeletePayload();
  }
};

}  // namespace ork
