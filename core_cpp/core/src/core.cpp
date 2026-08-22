#include "ourokore/c_api/component_api.h"

#include "Registry.h"

extern "C"
{
  int32_t ORK_CALL ork_register_object(OuroObject *obj, HandleID *out_id)
  {
    if (!obj || !out_id)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      *out_id = ork::Registry::GetInstance().RegisterObject(reinterpret_cast<ork::OuroObject *>(obj));
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

  int32_t ORK_CALL ork_bind_object_payload(HandleID id, OuroObject *obj)
  {
    if (id == ORK_ROOT_ID)
    {
      return ORK_STATUS_ERROR_INVALID_ARG;
    }
    try
    {
      if (ork::Registry::GetInstance().BindPayload(id, reinterpret_cast<ork::OuroObject *>(obj)))
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

}  // extern "C"
