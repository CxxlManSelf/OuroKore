#pragma once

#include "ourokore/c_api/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct pointer representing the C++ object payload
typedef struct OuroObject OuroObject;

/**
 * @brief Function pointer callback type for destroying an object's payload in its creating module's CRT.
 */
typedef void (*ork_destroy_fn_t)(OuroObject* payload);

/**
 * @brief Function pointer callback type for auto-rehydrating a dehydrated object.
 */
typedef void (*ork_rehydrate_fn_t)(HandleID id);

/**
 * @brief Register a newly created object to the registry.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_object(OuroObject* obj, ork_destroy_fn_t destroy_fn, HandleID* out_id);

/**
 * @brief Lock the object's control block mutex in exclusive (write) mode.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_lock_object(HandleID target_id);

/**
 * @brief Unlock the object's control block mutex in exclusive (write) mode.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unlock_object(HandleID target_id);

/**
 * @brief Lock the object's control block mutex in shared (read) mode.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_lock_object_shared(HandleID target_id);

/**
 * @brief Unlock the object's control block mutex in shared (read) mode.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unlock_object_shared(HandleID target_id);

/**
 * @brief Retrieve the raw OuroObject pointer after locking it.
 * Used by internal smart pointers (e.g., OuroPtr) to retrieve the actual object instance.
 * @param target_id The HandleID of the target object.
 * @param out_obj Pointer to receive the OuroObject pointer.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_acquire_object_pointer(HandleID target_id, OuroObject** out_obj);

/**
 * @brief Thread-Local Active Owner context operations.
 * Sets the current thread's active owner ID.
 * @param owner_id The owner HandleID to set.
 * @return ORK_STATUS_OK on success.
 */
ORK_API int32_t ORK_CALL ork_set_active_owner(HandleID owner_id);

/**
 * @brief Gets the current thread's active owner ID.
 * @param out_owner_id Pointer to receive the owner HandleID.
 * @return ORK_STATUS_OK on success.
 */
ORK_API int32_t ORK_CALL ork_get_active_owner(HandleID* out_owner_id);

/**
 * @brief Gets the StorageState of an object's ControlBlock.
 * @param target_id The HandleID of the target object.
 * @param out_state Pointer to receive the uint8_t StorageState value.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_get_storage_state(HandleID target_id, uint8_t* out_state);

/**
 * @brief Automatically marks an object's ControlBlock as Dirty if currently Clean.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_mark_dirty(HandleID target_id);

/**
 * @brief Gets the number of active root edges (ORK_ROOT_ID) held on an object (active OuroPtr instances).
 * @param target_id The HandleID of the target object.
 * @param out_count Pointer to receive the root edge count.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_get_root_edge_count(HandleID target_id, uint32_t* out_count);

/**
 * @brief Dehydrate an object by HandleID using the core runtime context.
 * Performs in-flight root edge checking, exclusive locking, stream serialization (if dirty),
 * payload memory deletion, and auto-dehydrator notification.
 * @param id The HandleID of the object to dehydrate.
 * @param out_success Pointer to receive the boolean result (1 if dehydrated, 0 if skipped/failed).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_dehydrate_object(HandleID id, int32_t* out_success);

#ifdef __cplusplus
}
#endif

