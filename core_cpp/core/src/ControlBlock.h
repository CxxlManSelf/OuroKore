#pragma once

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "ourokore/component/OuroObject.hpp"

namespace ork
{

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

  // Raw pointer to the physical object (the body)
  OuroObject *m_payload{nullptr};

  // Owner ID Roster for Upstream Cycle Search
  std::vector<HandleID> m_owners;
  std::mutex m_owners_mutex;

  explicit ControlBlock(OuroObject *payload) : m_payload(payload) {}

  ~ControlBlock()
  {
    // Payload should already be deleted when m_strong_count hits 0.
    // Doing a safety check here.
    if (m_payload)
    {
      delete m_payload;
      m_payload = nullptr;
    }
  }
};

}  // namespace ork
