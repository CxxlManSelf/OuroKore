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
 * @brief Register a newly created object with explicit TypeID to the registry.
 * @param obj Pointer to the OuroObject instance.
 * @param destroy_fn Pointer to in-place deleter function (executes in creating module's CRT).
 * @param type_id The 64-bit unique identifier of this object's type.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_object_with_type(OuroObject* obj, ork_destroy_fn_t destroy_fn, ork_type_id_t type_id, HandleID* out_id);

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
 * @brief Register a type and its inheritance relationship to the core TypeRegistry.
 * @param type_id The 64-bit unique identifier for the type.
 * @param name_utf8 The UTF-8 string name of the type.
 * @param parent_type_id The TypeID of the immediate parent class, or ORK_INVALID_TYPE_ID if root.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_type(ork_type_id_t type_id, const char* name_utf8, ork_type_id_t parent_type_id);

/**
 * @brief Gets the TypeID of an object's ControlBlock.
 * This operation is 100% thread-safe and never triggers dehydration or object rehydration.
 * @param target_id The HandleID of the target object.
 * @param out_type_id Pointer to receive the ork_type_id_t value.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_get_object_type(HandleID target_id, ork_type_id_t* out_type_id);

/**
 * @brief Checks if the object denoted by target_id is an instance of target_type_id or derived from it.
 * This operation is 100% thread-safe and never triggers dehydration or object rehydration.
 * @param target_id The HandleID of the target object.
 * @param target_type_id The TypeID to test against.
 * @param out_is_instance Pointer to receive 1 if instance of target_type_id, 0 otherwise.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_is_instance_of(HandleID target_id, ork_type_id_t target_type_id, int32_t* out_is_instance);

/**
 * @brief Checks if a derived TypeID inherits from a base TypeID in the type hierarchy.
 * @param derived_type The derived TypeID.
 * @param base_type The base TypeID.
 * @param out_is_subclass Pointer to receive 1 if derived_type is/inherits from base_type, 0 otherwise.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_is_subclass_of(ork_type_id_t derived_type, ork_type_id_t base_type, int32_t* out_is_subclass);

#ifdef __cplusplus
}
#endif

