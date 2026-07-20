#pragma once

#include "ourokore/c_api/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct pointer representing the C++ object payload
typedef struct OuroObject OuroObject;

/**
 * @brief Register a newly created object to the registry.
 * @param obj Pointer to the OuroObject instance.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_object(OuroObject* obj, HandleID* out_id);

/**
 * @brief Bind a constructed object payload to a reserved HandleID.
 * @param id The HandleID reserved by ork_reserve_object_id.
 * @param obj Pointer to the OuroObject instance.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_bind_object_payload(HandleID id, OuroObject* obj);

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

#ifdef __cplusplus
}
#endif
