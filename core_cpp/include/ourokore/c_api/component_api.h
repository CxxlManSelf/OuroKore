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
typedef OuroObject* (*ork_rehydrate_fn_t)(HandleID id);

/**
 * @brief Register a newly created object to the registry.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_object(OuroObject* obj, ork_destroy_fn_t destroy_fn, HandleID* out_id);

/**
 * @brief Bind a constructed object payload to a reserved HandleID.
 * @param id The HandleID reserved by ork_reserve_object_id.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_bind_object_payload(HandleID id, OuroObject* obj, ork_destroy_fn_t destroy_fn);

/**
 * @brief Unregister/release an object from the registry (typically called when strong ref count becomes 0).
 * @param id The HandleID of the object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unregister_object(HandleID id);

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
 * @brief Sets the StorageState of an object's ControlBlock.
 * @param target_id The HandleID of the target object.
 * @param state The uint8_t StorageState value to set.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_set_storage_state(HandleID target_id, uint8_t state);

/**
 * @brief Automatically marks an object's ControlBlock as Dirty if currently Clean.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_mark_dirty(HandleID target_id);

/**
 * @brief Sets the auto-rehydration callback function on an object's ControlBlock.
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
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_destroy_payload(HandleID target_id);

/**
 * @brief Gets the number of active root edges (ORK_ROOT_ID) held on an object (active OuroPtr instances).
 * @param target_id The HandleID of the target object.
 * @param out_count Pointer to receive the root edge count.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_get_root_edge_count(HandleID target_id, uint32_t* out_count);

/**
 * @brief Function pointer callback type invoked when an object is destroyed in Registry.
 */
typedef void (*ork_object_destroyed_fn_t)(HandleID id);

/**
 * @brief Sets the global callback invoked when an object's ControlBlock is destroyed.
 * @param fn The callback function pointer.
 * @return ORK_STATUS_OK on success.
 */
ORK_API int32_t ORK_CALL ork_set_object_destroyed_callback(ork_object_destroyed_fn_t fn);

/**
 * @brief Dehydrate an object by HandleID using the core runtime context.
 * Performs in-flight root edge checking, exclusive locking, stream serialization (if dirty),
 * payload memory deletion, and auto-dehydrator notification.
 * @param id The HandleID of the object to dehydrate.
 * @param out_success Pointer to receive the boolean result (1 if dehydrated, 0 if skipped/failed).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_dehydrate_object(HandleID id, int32_t* out_success);

/**
 * @brief Flush and wait for all pending deferred deletions and background storage/destruction tasks.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_flush_storage(void);

/**
 * @brief Gracefully shutdown the core runtime context and all background threads.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_shutdown_runtime(void);

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

