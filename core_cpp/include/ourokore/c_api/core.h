#pragma once

#include <stdint.h>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
  #ifdef OUROKORE_EXPORTS
    #define ORK_API __declspec(dllexport)
  #else
    #define ORK_API __declspec(dllimport)
  #endif
  #define ORK_CALL __cdecl
#else
  #define ORK_API __attribute__((visibility("default")))
  #define ORK_CALL
#endif

#define ORK_ROOT_ID 0

// Status codes
#define ORK_STATUS_OK 0
#define ORK_STATUS_ERROR_INVALID_ARG -1
#define ORK_STATUS_ERROR_NOT_FOUND -2
#define ORK_STATUS_ERROR_ALREADY_EXISTS -3
#define ORK_STATUS_ERROR_EXCEPTION -4
#define ORK_STATUS_ERROR_OBJECT_DEAD -5

typedef uint64_t HandleID;

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct pointer representing the object
typedef struct OuroObject OuroObject;

/**
 * @brief Register a newly created object to the registry.
 * @param obj Pointer to the OuroObject instance.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_object(OuroObject* obj, HandleID* out_id);

/**
 * @brief Reserve a HandleID and create a ControlBlock with a null payload.
 * Used to establish ActiveOwnerContext before construction.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_reserve_object_id(HandleID* out_id);

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
 * @brief Register a directed ownership edge from an owner to a target.
 * Increases the target's strong reference count.
 * @param owner_id The HandleID of the parent/owner object (or ORK_ROOT_ID).
 * @param target_id The HandleID of the child/target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_edge(HandleID owner_id, HandleID target_id);

/**
 * @brief Unregister a directed ownership edge from an owner to a target.
 * Decreases the target's strong reference count.
 * @param owner_id The HandleID of the parent/owner object (or ORK_ROOT_ID).
 * @param target_id The HandleID of the child/target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unregister_edge(HandleID owner_id, HandleID target_id);

/**
 * @brief Add a weak reference to the target object.
 * Increases the target's weak reference count.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_register_weak(HandleID target_id);

/**
 * @brief Release a weak reference to the target object.
 * Decreases the target's weak reference count.
 * @param target_id The HandleID of the target object.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_unregister_weak(HandleID target_id);

/**
 * @brief Checks if the target object is alive (strong reference count > 0).
 * If the object is dead and the function returns false, this function will automatically
 * decrement the weak reference count and return ORK_STATUS_OK.
 * This supports the Lazy Pruning mechanism of WeakHandle.
 * @param target_id The HandleID of the target object.
 * @param out_alive Pointer to receive the boolean status (1 if alive, 0 if dead).
 * @param perform_pruning If non-zero, will perform lazy pruning (decrement weak count if dead).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_check_alive(HandleID target_id, int32_t* out_alive, int32_t perform_pruning);

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
 * This is used by OuroPtr to retrieve the actual object instance.
 * @param target_id The HandleID of the target object.
 * @param out_obj Pointer to receive the OuroObject pointer.
 * @return ORK_STATUS_OK on success, or an error code (e.g. if the object is dead).
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
