#pragma once

#include "ourokore/c_api/core.h"
#include "ourokore/c_api/component_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reserve a HandleID and create a ControlBlock with a null payload.
 * Used internally by CreateObject two-phase construction.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_reserve_object_id(HandleID* out_id);

/**
 * @brief Bind a constructed object payload to a reserved HandleID.
 * Used internally by CreateObject two-phase construction and Rehydrate.
 * @param id The HandleID reserved by ork_reserve_object_id.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_bind_object_payload(HandleID id, OuroObject* obj, ork_destroy_fn_t destroy_fn);

/**
 * @brief Unregister/release an object from the registry.
 * Used internally during exception rollback or object destruction.
 * @param id The HandleID of the object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unregister_object(HandleID id);

/**
 * @brief Sets the StorageState of an object's ControlBlock.
 * Used internally by core persistence and dehydration routines.
 * @param target_id The HandleID of the target object.
 * @param state The uint8_t StorageState value to set.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_set_storage_state(HandleID target_id, uint8_t state);

/**
 * @brief Sets the auto-rehydration callback function on an object's ControlBlock.
 * Used internally by CreateObject and Rehydrate to bind type-safe rehydration.
 * @param target_id The HandleID of the target object.
 * @param fn The rehydration callback function pointer.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_set_rehydrate_fn(HandleID target_id, ork_rehydrate_fn_t fn);

/**
 * @brief Sets the in-place deleter callback function on an object's ControlBlock.
 * Ensures the object is deleted in the CRT heap of the module that allocated it.
 * @param target_id The HandleID of the target object.
 * @param fn The deleter callback function pointer.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_set_destroy_fn(HandleID target_id, ork_destroy_fn_t fn);

/**
 * @brief Safely destroys the payload of an object using its registered module deleter hook.
 * Sets payload pointer to null and resets memory safely across DLL CRT boundaries.
 * Used internally by dehydration routines.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_destroy_payload(HandleID target_id);

/**
 * @brief Notify the runtime auto-dehydrator that an object was registered.
 */
ORK_API int32_t ORK_CALL ork_notify_object_registered(HandleID id, size_t size_bytes);

/**
 * @brief Notify the runtime auto-dehydrator that an object was dehydrated.
 */
ORK_API int32_t ORK_CALL ork_notify_object_dehydrated(HandleID id);

/**
 * @brief Notify the runtime auto-dehydrator that an object was rehydrated.
 */
ORK_API int32_t ORK_CALL ork_notify_object_rehydrated(HandleID id);

/**
 * @brief Trigger emergency dehydration rescue from the runtime auto-dehydrator during OOM.
 */
ORK_API int32_t ORK_CALL ork_trigger_dehydration_rescue(size_t bytes_needed, size_t* out_freed_bytes, int32_t* out_has_more);

#ifdef __cplusplus
}
#endif
