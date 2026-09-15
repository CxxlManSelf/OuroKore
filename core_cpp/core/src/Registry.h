#pragma once

#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "ControlBlock.h"

namespace ork
{

class OuroObject;

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
  HandleID RegisterObject(OuroObject *obj);

  /**
   * @brief Reserves a HandleID and creates a ControlBlock with a null payload.
   * Used to establish ActiveOwnerContext before construction.
   */
  HandleID ReserveID();

  /**
   * @brief Binds a constructed object payload to a reserved HandleID.
   */
  bool BindPayload(HandleID id, OuroObject *obj);

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
  uint32_t GetRootEdgeCount(HandleID target_id) const;

  // Destruction hook
  using ObjectDestroyedFn = void (*)(HandleID id);
  void SetObjectDestroyedCallback(ObjectDestroyedFn fn)
  {
    m_object_destroyed_cb = fn;
  }

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
  ObjectDestroyedFn m_object_destroyed_cb{nullptr};

  // Persistent ID maps (reserved for future rehydration)
  mutable std::shared_mutex m_persistent_mutex;
  std::unordered_map<std::string, HandleID> m_persistent_map;

  // Random generator for unique ID
  std::mt19937_64 m_rng;
  std::mutex m_rng_mutex;
};

}  // namespace ork
