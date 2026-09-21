#include <atomic>

#include "ourokore/c_api/core.h"
#include "ourokore/c_api/component_api.h"
#include "ourokore/c_api/host_api.h"
#include "internal_api.h"

#include "CycleCollector.h"
#include "DeferredDeleteQueue.h"
#include "Registry.h"
#include "RuntimeContext.h"
#include "ourokore/component/RuntimeAPI.hpp"
#include "ourokore/host/HostRuntimeAPI.hpp"

namespace
{
std::atomic<bool> g_core_initialized{false};
}

extern "C"
{
  int32_t ORK_CALL ork_try_initialize_core(void)
  {
    bool expected = false;
    if (!g_core_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
      return ORK_STATUS_ERROR_ALREADY_EXISTS;
    }
    ork::CycleCollector::GetInstance().Start();
    ork::DeferredDeleteQueue::GetInstance().Start();
    return ORK_STATUS_OK;
  }

  int32_t ORK_CALL ork_register_object(OuroObject *obj, ork_destroy_fn_t destroy_fn, HandleID *out_id)
  {
    if (!obj || !out_id)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_id = ork::Registry::GetInstance().RegisterObject(reinterpret_cast<ork::OuroObject *>(obj),
                                                            reinterpret_cast<ork::DestroyFn>(destroy_fn));
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_reserve_object_id(HandleID *out_id)
  {
    if (!out_id)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_id = ork::Registry::GetInstance().ReserveID();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_bind_object_payload(HandleID id, OuroObject *obj, ork_destroy_fn_t destroy_fn)
  {
    if (id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().BindPayload(id, reinterpret_cast<ork::OuroObject *>(obj),
                                                   reinterpret_cast<ork::DestroyFn>(destroy_fn)))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_unregister_object(HandleID id)
  {
    if (id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().UnregisterObject(id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_register_edge(HandleID owner_id, HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      auto *cb = ork::Registry::GetInstance().GetControlBlock(target_id);
      if (!cb)
      {
        return ORK_STATUS_ERROR_NOT_FOUND;
      }
      if (cb->m_is_destructing.load(std::memory_order_acquire))
      {
        return ORK_STATUS_ERROR_DESTRUCTING;
      }

      if (ork::Registry::GetInstance().RegisterEdge(owner_id, target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_unregister_edge(HandleID owner_id, HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().UnregisterEdge(owner_id, target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_register_weak(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().RegisterWeak(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_unregister_weak(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().UnregisterWeak(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_check_alive(HandleID target_id, int32_t *out_alive, int32_t perform_pruning)
  {
    if (target_id == ORK_ROOT_ID || !out_alive)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      bool alive = ork::Registry::GetInstance().CheckAlive(target_id, perform_pruning != 0);
      *out_alive = alive ? 1 : 0;
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_try_lock_weak(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().TryLockWeak(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_OBJECT_DEAD;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_lock_object(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().LockObject(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_unlock_object(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().UnlockObject(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_lock_object_shared(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().LockObjectShared(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_unlock_object_shared(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().UnlockObjectShared(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_acquire_object_pointer(HandleID target_id, OuroObject **out_obj)
  {
    if (target_id == ORK_ROOT_ID || !out_obj)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      ork::OuroObject *obj = ork::Registry::GetInstance().AcquireObjectPointer(target_id);
      if (!obj)
      {
        return ORK_STATUS_ERROR_OBJECT_DEAD;
      }
      *out_obj = reinterpret_cast<::OuroObject *>(obj);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_active_owner(HandleID owner_id)
  {
    try
    {
      ork::Registry::GetInstance().SetActiveOwner(owner_id);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_get_active_owner(HandleID *out_owner_id)
  {
    if (!out_owner_id)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_owner_id = ork::Registry::GetInstance().GetActiveOwner();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_get_storage_state(HandleID target_id, uint8_t *out_state)
  {
    if (!out_state)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_state = ork::Registry::GetInstance().GetStorageState(target_id);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_storage_state(HandleID target_id, uint8_t state)
  {
    try
    {
      if (ork::Registry::GetInstance().SetStorageState(target_id, state))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_mark_dirty(HandleID target_id)
  {
    try
    {
      if (ork::Registry::GetInstance().MarkDirty(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_rehydrate_fn(HandleID target_id, ork_rehydrate_fn_t fn)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().SetRehydrateFn(target_id, reinterpret_cast<ork::RehydrateFn>(fn)))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_destroy_fn(HandleID target_id, ork_destroy_fn_t fn)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().SetDestroyFn(target_id, reinterpret_cast<ork::DestroyFn>(fn)))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_destroy_payload(HandleID target_id)
  {
    if (target_id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().DestroyPayload(target_id))
      {
        return ORK_STATUS_OK;
      }
      return ORK_STATUS_ERROR_NOT_FOUND;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_get_root_edge_count(HandleID target_id, uint32_t *out_count)
  {
    if (!out_count)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_count = ork::Registry::GetInstance().GetRootEdgeCount(target_id);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_object_destroyed_callback(ork_object_destroyed_fn_t fn)
  {
    ork::Registry::GetInstance().SetObjectDestroyedCallback(fn);
    return ORK_STATUS_OK;
  }

  int32_t ORK_CALL ork_collect_cycles(void)
  {
    try
    {
      ork::CycleCollector::GetInstance().CollectCyclesExplicit();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_flush_deferred_deletions(void)
  {
    try
    {
      ork::DeferredDeleteQueue::GetInstance().Flush();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_deferred_delete_mode(int32_t mode)
  {
    try
    {
      ork::DeferredDeleteQueue::GetInstance().SetSyncMode(mode == ORK_DEFERRED_DELETE_SYNC);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  uint64_t ORK_CALL ork_get_deferred_delete_pending_count(void)
  {
    try
    {
      return static_cast<uint64_t>(ork::DeferredDeleteQueue::GetInstance().PendingCount());
    }
    catch (...)
    {
      return 0;
    }
  }

  int32_t ORK_CALL ork_stop_cycle_collector(void)
  {
    try
    {
      ork::CycleCollector::GetInstance().Stop();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_stop_deferred_deletions(void)
  {
    try
    {
      ork::DeferredDeleteQueue::GetInstance().Stop();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_shutdown_core(void)
  {
    // 直接調用完整版的核心復位函式，確保背景執行緒終止、名冊清空與狀態復位一次到位
    return ork_reset_core_state();
  }

  int32_t ORK_CALL ork_reset_core_state(void)
  {
    try
    {
      // 1. 防禦性收斂：若執行時環境仍在運作且尚未執行關閉，優先觸發完整優雅終止與排空
      if (ork::RuntimeContext::GetInstance().IsInitialized() &&
          !ork::RuntimeContext::GetInstance().IsShutdownRunning())
      {
        ork::RuntimeContext::GetInstance().Shutdown();
        return ORK_STATUS_OK;
      }

      // 2. 解除全域銷毀回呼，防止殘留回呼指向已卸載函式
      ork_set_object_destroyed_callback(nullptr);

      // 3. 防禦性確保所有背景工作執行緒皆已終止（若已停止則為安全 No-Op）
      ork::CycleCollector::GetInstance().Stop();
      ork::DeferredDeleteQueue::GetInstance().Stop();

      // 4. 復位延遲銷毀隊列配置（還原為預設非同步模式）
      ork::DeferredDeleteQueue::GetInstance().SetSyncMode(false);

      // 5. 徹底清空註冊表殘留物件與墓碑，還原為白紙狀態（兩階段無鎖置換防死鎖）
      ork::Registry::GetInstance().Clear();

      // 6. 重置全域執行時上下文
      ork::RuntimeContext::GetInstance().Reset();

      // 7. 原子復位核心初始化旗標，保證跨執行緒完全可見
      g_core_initialized.store(false, std::memory_order_seq_cst);

      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_dehydrate_object(HandleID id, int32_t *out_success)
  {
    if (!out_success)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_success = ork::RuntimeContext::GetInstance().Dehydrate(id) ? 1 : 0;
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      *out_success = 0;
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_flush_storage(void)
  {
    try
    {
      ork::RuntimeContext::GetInstance().FlushStorage();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_shutdown_runtime(void)
  {
    try
    {
      ork::RuntimeContext::GetInstance().Shutdown();
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_notify_object_registered(HandleID id, size_t size_bytes)
  {
    try
    {
      ork::RuntimeContext::GetInstance().NotifyObjectRegistered(id, size_bytes);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_notify_object_dehydrated(HandleID id)
  {
    try
    {
      ork::RuntimeContext::GetInstance().NotifyObjectDehydrated(id);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_notify_object_rehydrated(HandleID id)
  {
    try
    {
      ork::RuntimeContext::GetInstance().NotifyObjectRehydrated(id);
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_trigger_dehydration_rescue(size_t bytes_needed, size_t *out_freed_bytes, int32_t *out_has_more)
  {
    if (!out_freed_bytes || !out_has_more)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      auto report = ork::RuntimeContext::GetInstance().TriggerDehydrationRescue(bytes_needed);
      *out_freed_bytes = report.freed_bytes;
      *out_has_more = report.has_more_candidates ? 1 : 0;
      return ORK_STATUS_OK;
    }
    catch (...)
    {
      *out_freed_bytes = 0;
      *out_has_more = 0;
      return ORK_STATUS_ERROR_EXCEPTION;
    }
  }

  int32_t ORK_CALL ork_set_storage_state_for_testing(HandleID target_id, uint8_t state)
  {
    return ork_set_storage_state(target_id, state);
  }

  int32_t ORK_CALL ork_clear_object_payload_for_testing(HandleID target_id)
  {
    return ork_destroy_payload(target_id);
  }

}  // extern "C"

namespace ork::detail
{

bool InitializeRuntime(std::shared_ptr<IStorageDriver> driver,
                       std::shared_ptr<IAutoDehydrator> auto_dehydrator,
                       std::shared_ptr<ork::base::FixedThreadPool> thread_pool)
{
  return RuntimeContext::GetInstance().Initialize(std::move(driver),
                                                  std::move(auto_dehydrator),
                                                  std::move(thread_pool));
}

std::shared_ptr<IStorageDriver> GetRuntimeStorageDriver()
{
  return RuntimeContext::GetInstance().GetStorageDriver();
}

std::shared_ptr<IAutoDehydrator> GetRuntimeAutoDehydrator()
{
  return RuntimeContext::GetInstance().GetAutoDehydrator();
}

std::shared_ptr<ork::base::FixedThreadPool> GetRuntimeThreadPool()
{
  return RuntimeContext::GetInstance().GetThreadPool();
}

void SetRuntimeAutoDehydrator(std::shared_ptr<IAutoDehydrator> dehydrator)
{
  RuntimeContext::GetInstance().SetAutoDehydrator(std::move(dehydrator));
}

void SetRuntimeStorageDriver(std::shared_ptr<IStorageDriver> driver)
{
  RuntimeContext::GetInstance().SetStorageDriver(std::move(driver));
}

std::unique_ptr<OuroStream> CreateRuntimeWriteStream(HandleID id)
{
  auto driver = RuntimeContext::GetInstance().GetStorageDriver();
  if (!driver)
  {
    return nullptr;
  }
  return driver->CreateWriteStream(id);
}

std::unique_ptr<OuroStream> OpenRuntimeReadStream(HandleID id)
{
  auto driver = RuntimeContext::GetInstance().GetStorageDriver();
  if (!driver)
  {
    return nullptr;
  }
  return driver->OpenReadStream(id);
}

void SubmitRuntimeTask(std::function<void()> task)
{
  auto pool = RuntimeContext::GetInstance().GetThreadPool();
  if (!pool || !pool->is_running())
  {
    throw std::runtime_error("OuroKore Error: Core ThreadPool not initialized or stopped.");
  }
  pool->submit(std::move(task));
}

bool TriggerRuntimeRescue(size_t bytes_needed)
{
  auto report = RuntimeContext::GetInstance().TriggerDehydrationRescue(bytes_needed);
  if (report.freed_bytes == 0 || (!report.has_more_candidates && report.freed_bytes < bytes_needed))
  {
    return false;
  }
  return true;
}

void NotifyRuntimeObjectRegistered(HandleID id, size_t size_bytes)
{
  RuntimeContext::GetInstance().NotifyObjectRegistered(id, size_bytes);
}

void NotifyRuntimeObjectDehydrated(HandleID id)
{
  RuntimeContext::GetInstance().NotifyObjectDehydrated(id);
}

void NotifyRuntimeObjectRehydrated(HandleID id)
{
  RuntimeContext::GetInstance().NotifyObjectRehydrated(id);
}

bool DehydrateRuntime(HandleID id)
{
  return RuntimeContext::GetInstance().Dehydrate(id);
}

HandleID ReserveRuntimeObjectID()
{
  HandleID reserved_id = 0;
  if (ork_reserve_object_id(&reserved_id) != ORK_STATUS_OK)
  {
    throw std::runtime_error("OuroKore Error: Failed to reserve HandleID from Registry.");
  }
  return reserved_id;
}

bool BindRuntimeObjectPayload(HandleID id,
                              ::OuroObject* payload,
                              void (*destroy_fn)(::OuroObject*),
                              ::OuroObject* (*rehydrate_fn)(HandleID))
{
  if (ork_bind_object_payload(id,
                              payload,
                              reinterpret_cast<ork_destroy_fn_t>(destroy_fn)) != ORK_STATUS_OK)
  {
    return false;
  }
  ork_set_rehydrate_fn(id, reinterpret_cast<ork_rehydrate_fn_t>(rehydrate_fn));
  return true;
}

void RollbackRuntimeObjectID(HandleID id)
{
  ork_unregister_object(id);
}

void MarkRuntimeObjectClean(HandleID id)
{
  ork_set_storage_state(id, 1);  // 1 = StorageState::Clean
}

}  // namespace ork::detail



