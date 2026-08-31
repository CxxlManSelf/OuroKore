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

/**
 * @brief Atomically claim one-way host initialization for OuroKore Core across the entire process.
 * Only the first call returns ORK_STATUS_OK. Subsequent calls from plugins return ORK_STATUS_ERROR_ALREADY_EXISTS.
 * @return ORK_STATUS_OK on first successful call, ORK_STATUS_ERROR_ALREADY_EXISTS otherwise.
 */
ORK_API int32_t ORK_CALL ork_try_initialize_core(void);

/**
 * @brief Reserve a HandleID and create a ControlBlock with a null payload.
 * @param out_id Pointer to receive the allocated HandleID.
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_reserve_object_id(HandleID* out_id);

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
 * If the object is dead and perform_pruning is non-zero, this function will automatically
 * decrement the weak reference count and return ORK_STATUS_OK.
 * @param target_id The HandleID of the target object.
 * @param out_alive Pointer to receive the boolean status (1 if alive, 0 if dead).
 * @param perform_pruning If non-zero, will perform lazy pruning (decrement weak count if dead).
 * @return ORK_STATUS_OK on success, or an error code.
 */
ORK_API int32_t ORK_CALL ork_check_alive(HandleID target_id, int32_t* out_alive, int32_t perform_pruning);

#ifdef __cplusplus
}
#endif
