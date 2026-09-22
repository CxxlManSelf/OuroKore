#pragma once

#include <atomic>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "ControlBlock.h"

namespace ork
{

class ControlBlock;
class Registry;

/**
 * @brief RAII 守衛：將 ControlBlock 的生命週期安全釘住 (Pinning)
 *
 * 核心價值：
 * 藉由暫時遞增 ControlBlock 的 WeakCount，保證在守衛存活期間，
 * 即使業務執行緒或 DeferredDeleteQueue 將 StrongCount 降至 0 並銷毀 Payload，
 * 也絕對無法 delete ControlBlock 本體，從根源徹底消除 Use-After-Free (UAF)。
 */
class ControlBlockPinGuard
{
public:
  ControlBlockPinGuard() noexcept = default;
  ControlBlockPinGuard(HandleID id, ControlBlock *cb) noexcept;
  ~ControlBlockPinGuard();

  ControlBlockPinGuard(ControlBlockPinGuard &&other) noexcept;
  ControlBlockPinGuard &operator=(ControlBlockPinGuard &&other) noexcept;

  ControlBlockPinGuard(const ControlBlockPinGuard &) = delete;
  ControlBlockPinGuard &operator=(const ControlBlockPinGuard &) = delete;

  ControlBlock *Get() const noexcept { return m_cb; }
  ControlBlock *operator->() const noexcept { return m_cb; }
  ControlBlock &operator*() const noexcept { return *m_cb; }
  HandleID GetID() const noexcept { return m_id; }
  explicit operator bool() const noexcept { return m_cb != nullptr; }

  void Reset();

private:
  HandleID m_id{0};
  ControlBlock *m_cb{nullptr};
};

class Registry
{
public:
  static Registry &GetInstance();

  // Prevent copy/move
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;

  /**
   * @brief Registers an OuroObject and creates its ControlBlock.
   */
  HandleID RegisterObject(OuroObject *obj, DestroyFn destroy_fn = nullptr);

  /**
   * @brief Reserves a HandleID and creates a ControlBlock with a null payload.
   * Used to establish ActiveOwnerContext before construction.
   */
  HandleID ReserveID();

  /**
   * @brief Binds a constructed object payload to a reserved HandleID.
   */
  bool BindPayload(HandleID id, OuroObject *obj, DestroyFn destroy_fn = nullptr);

  /**
   * @brief Forcefully unregisters an object by ID.
   */
  bool UnregisterObject(HandleID id);

  /**
   * @brief Registers an ownership edge (owner_id -> target_id).
   */
  bool RegisterEdge(HandleID owner_id, HandleID target_id);

  /**
   * @brief Unregisters an ownership edge.
   * @param silent If true, do not enqueue to suspect queue (used during cycle island destruction).
   */
  bool UnregisterEdge(HandleID owner_id, HandleID target_id, bool silent = false);

  /**
   * @brief 安全獲取並釘住 ControlBlock (Pinning)，返回 RAII 守衛
   * 在讀鎖保護下原子遞增 WeakCount，保證守衛持有期間 ControlBlock 絕不被 delete。
   */
  ControlBlockPinGuard AcquireControlBlock(HandleID target_id);

  /**
   * @brief 釋放釘住的 ControlBlock
   */
  void ReleasePin(HandleID target_id, ControlBlock *cb);

  /**
   * @brief Directly acquire a ControlBlock pointer (Registry shared-lock must be held or thread-safe access).
   */
  ControlBlock *GetControlBlock(HandleID target_id) const;

  /**
   * @brief Checks and performs cleanup of ControlBlock if both strong and weak counts are 0.
   */
  bool DestroyControlBlockIfDead(HandleID target_id);

  /**
   * @brief Registers a weak reference to target_id.
   */
  bool RegisterWeak(HandleID target_id);

  /**
   * @brief Unregisters a weak reference.
   */
  bool UnregisterWeak(HandleID target_id);

  /**
   * @brief Checks if an object is alive. Supports optional lazy pruning.
   */
  bool CheckAlive(HandleID target_id, bool perform_pruning);

  /**
   * @brief Atomically attempts to lock a weak reference by registering a root edge if strong_count > 0.
   */
  bool TryLockWeak(HandleID target_id);

  /**
   * @brief Lock object's control block in exclusive mode.
   */
  bool LockObject(HandleID target_id);

  /**
   * @brief Unlock object's control block in exclusive mode.
   */
  bool UnlockObject(HandleID target_id);

  /**
   * @brief Lock object's control block in shared mode.
   */
  bool LockObjectShared(HandleID target_id);

  /**
   * @brief Unlock object's control block in shared mode.
   */
  bool UnlockObjectShared(HandleID target_id);

  /**
   * @brief Acquire the raw object pointer. Lock must be held outside.
   */
  OuroObject *AcquireObjectPointer(HandleID target_id);

  // StorageState management
  uint8_t GetStorageState(HandleID target_id) const;
  bool SetStorageState(HandleID target_id, uint8_t state);
  bool MarkDirty(HandleID target_id);
  bool SetRehydrateFn(HandleID target_id, RehydrateFn fn);
  bool SetDestroyFn(HandleID target_id, DestroyFn fn);
  bool DestroyPayload(HandleID target_id);
  uint32_t GetRootEdgeCount(HandleID target_id) const;

  // Destruction hook
  using ObjectDestroyedFn = void (*)(HandleID id);
  void SetObjectDestroyedCallback(ObjectDestroyedFn fn)
  {
    m_object_destroyed_cb.store(fn, std::memory_order_release);
  }

  /**
   * @brief 在無鎖狀態下觸發全域物件邏輯銷毀通知回呼
   * @param id 被銷毀物件的 HandleID
   */
  void NotifyObjectDestroyed(HandleID id);

  /**
   * @brief 徹底清空註冊表殘留物件與墓碑，還原為初始白紙狀態
   * 採用兩階段無鎖置換技術（Swap-out Two-Phase Destruction），確保重入解構時絕不死鎖。
   */
  void Clear();

  // Thread-Local Active Owner context
  void SetActiveOwner(HandleID owner_id);
  HandleID GetActiveOwner() const;

private:
  Registry();
  ~Registry();

  HandleID GenerateUniqueID();

  /**
   * @brief 在持有 m_registry_mutex 獨佔寫入鎖的前提下，嚴格判定並銷毀 ControlBlock。
   * 銷毀天條：
   * 1. m_strong_count == 0
   * 2. m_weak_count == 0
   * 3. m_payload == nullptr（保證肉體已銷毀，防止搶跑 UAF）
   */
  bool TryDestroyControlBlockLocked(HandleID id, ControlBlock *cb);

  // Map protecting the registry structure
  mutable std::shared_mutex m_registry_mutex;
  std::unordered_map<HandleID, ControlBlock *> m_object_map;

  // Global destruction callback
  std::atomic<ObjectDestroyedFn> m_object_destroyed_cb{nullptr};

  // Persistent ID maps (reserved for future rehydration)
  mutable std::shared_mutex m_persistent_mutex;
  std::unordered_map<std::string, HandleID> m_persistent_map;

  // Random generator for unique ID
  std::mt19937_64 m_rng;
  std::mutex m_rng_mutex;
};

}  // namespace ork
