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
   */
  bool UnregisterEdge(HandleID owner_id, HandleID target_id);

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
