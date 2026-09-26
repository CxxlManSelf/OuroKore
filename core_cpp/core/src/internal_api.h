#pragma once

#include "ourokore/c_api/core.h"
#include "ourokore/c_api/component_api.h"

namespace ork::internal
{

/**
 * @brief Reserve a HandleID and create a ControlBlock with a null payload.
 * Used internally by CreateObject two-phase construction.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t ReserveObjectId(HandleID* out_id);

/**
 * @brief Bind a constructed object payload to a reserved HandleID.
 * Used internally by CreateObject two-phase construction and Rehydrate.
 * @param id The HandleID reserved by ReserveObjectId.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t BindObjectPayload(HandleID id, ::OuroObject* obj, ork_destroy_fn_t destroy_fn);

/**
 * @brief Unregister/release an object from the registry.
 * Used internally during exception rollback or object destruction.
 * @param id The HandleID of the object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t UnregisterObject(HandleID id);

/**
 * @brief Sets the StorageState of an object's ControlBlock.
 * Used internally by core persistence and dehydration routines.
 * @param target_id The HandleID of the target object.
 * @param state The uint8_t StorageState value to set.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t SetStorageState(HandleID target_id, uint8_t state);

/**
 * @brief Sets the auto-rehydration callback function on an object's ControlBlock.
 * Used internally by CreateObject and Rehydrate to bind type-safe rehydration.
 * @param target_id The HandleID of the target object.
 * @param fn The rehydration callback function pointer.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t SetRehydrateFn(HandleID target_id, ork_rehydrate_fn_t fn);

/**
 * @brief Sets the in-place deleter callback function on an object's ControlBlock.
 * Ensures the object is deleted in the CRT heap of the module that allocated it.
 * @param target_id The HandleID of the target object.
 * @param fn The deleter callback function pointer.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t SetDestroyFn(HandleID target_id, ork_destroy_fn_t fn);

/**
 * @brief Safely destroys the payload of an object using its registered module deleter hook.
 * Sets payload pointer to null and resets memory safely across DLL CRT boundaries.
 * Used internally by dehydration routines.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
int32_t DestroyPayload(HandleID target_id);

/**
 * @brief Notify the runtime auto-dehydrator that an object was registered.
 */
int32_t NotifyObjectRegistered(HandleID id, size_t size_bytes);

/**
 * @brief Notify the runtime auto-dehydrator that an object was dehydrated.
 */
int32_t NotifyObjectDehydrated(HandleID id);

/**
 * @brief Notify the runtime auto-dehydrator that an object was rehydrated.
 */
int32_t NotifyObjectRehydrated(HandleID id);

/**
 * @brief 驗證 HandleID 是否處於合法的兩階段建構預留或脫水復水情境中
 */
bool IsValidRescueContext(HandleID id);

}  // namespace ork::internal
