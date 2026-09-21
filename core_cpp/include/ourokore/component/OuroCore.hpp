#pragma once

#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "ourokore/c_api/core.h"
#include "ourokore/c_api/component_api.h"
#include "ourokore/component/AsyncResult.hpp"
#include "ourokore/component/BlueprintPackaging.hpp"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"  // IWYU pragma: export
#include "ourokore/component/RuntimeAPI.hpp"

namespace ork
{

namespace detail
{
template <typename F>
inline auto SubmitAsyncHelper(F&& f) -> std::future<std::invoke_result_t<F>>
{
  using ReturnType = std::invoke_result_t<F>;
  auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
  std::future<ReturnType> fut = task->get_future();
  SubmitRuntimeTask([task]() { (*task)(); });
  return fut;
}
}  // namespace detail

/**
 * @brief Save object state to persistent storage driver via pure streaming.
 * If object is Clean or Dehydrated, skips Save (O(1)).
 */
template <typename T>
bool Save(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Save Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  T *obj = ptr.get();
  if (!obj)
  {
    throw std::runtime_error("OuroKore Save Error: Cannot access payload.");
  }

  StorageState state = obj->GetStorageState();
  if (state == StorageState::Clean || state == StorageState::Dehydrated)
  {
    return true;  // Fast skip!
  }

  auto stream = detail::CreateRuntimeWriteStream(id);
  if (!stream)
  {
    throw std::runtime_error(
        "OuroKore Save Error: Storage driver not initialized or stream creation failed. Call ork::Init(driver) first."
    );
  }

  {
    OuroReadLock lock(*obj);
    PackBlueprint(*obj, *stream);
  }

  // 顯式提交串流（若 PackBlueprint 拋出例外，stream 自動解構回滾丟棄，不執行 Commit）
  stream->Commit();

  detail::MarkRuntimeObjectClean(id);
  return true;
}

/**
 * @brief Load (revert/refresh) object state from persistent storage driver into an existing living object via pure streaming.
 */
template <typename T>
bool Load(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore Load Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  T *obj = ptr.get();
  if (!obj)
  {
    throw std::runtime_error("OuroKore Load Error: Cannot access payload.");
  }

  auto stream = detail::OpenRuntimeReadStream(id);
  if (!stream)
  {
    return false;
  }

  {
    OuroWriteLock lock(*obj);
    UnpackBlueprint(*obj, *stream);
  }

  detail::MarkRuntimeObjectClean(id);
  return true;
}

/**
 * @brief 透過 HandleID 直接脫水物件（由底層 core.dll 跨模組安全執行）
 *
 * 100% In-Flight 記憶體安全保證：
 * 核心永遠只檢查單一條件：root_count 必須為 0！
 * 若 root_count > 0（代表有任何執行緒或作用域正持有 OuroPtr 活躍執行中），核心一律安全略過並傳回 false。
 *
 * @param id 目標物件全域唯一 HandleID
 * @return 成功脫水傳回 true；若物件正忙 (In-Flight)、已脫水或不存在則安全略過並傳回 false。
 */
inline bool Dehydrate(HandleID id)
{
  return detail::DehydrateRuntime(id);
}

/**
 * @brief 透過右值移動（Move）對物件發起脫水（所有權消耗語意）
 *
 * 【重要使用規範】
 * 本函式採用右值消耗語意（Rvalue Consume）。呼叫此函式後，傳入的原 OuroPtr 物件
 * 將會被立即釋放並重置（HandleID 歸零），「使用過後原本的物件指標將不可以再被使用」！
 * 若日後需要再次存取該物件，請使用 Rehydrate<T>(id) 重新取得全新的 OuroPtr。
 *
 * @param ptr 要脫水的目標 OuroPtr（傳入後所有權將被轉移並清空）
 * @return 成功脫水傳回 true；若其他執行緒同時持有該物件 (root_count > 1) 則傳回 false。
 */
template <typename T>
inline bool Dehydrate(OuroPtr<T> &&ptr)
{
  if (!ptr)
  {
    return false;
  }

  HandleID id = ptr.GetTargetID();

  // 1. 檢查是否有其他活躍 OuroPtr (root_count > 1)
  uint32_t root_count = 0;
  if (ork_get_root_edge_count(id, &root_count) != ORK_STATUS_OK || root_count > 1)
  {
    return false;  // 有其他並行執行緒正在使用中，安全略過，保留 ptr
  }

  // 2. 自身為唯一持有者，釋放指標並由核心安全執行脫水
  ptr.Release();
  return Dehydrate(id);
}

/**
 * @brief 查詢指定 HandleID 之物件是否依然存活 (strong_count > 0)
 * @param id 目標物件 HandleID
 * @return 存活傳回 true，已死亡或不存在傳回 false
 */
inline bool IsAlive(HandleID id)
{
  int32_t alive = 0;
  if (ork_check_alive(id, &alive, 0) == ORK_STATUS_OK)
  {
    return alive != 0;
  }
  return false;
}

/**
 * @brief 查詢指定 HandleID 之物件儲存/脫水狀態 (Clean / Dirty / Dehydrated)
 * @param id 目標物件 HandleID
 * @return 物件當前 StorageState
 */
inline StorageState GetStorageState(HandleID id)
{
  uint8_t state = 0;
  if (ork_get_storage_state(id, &state) == ORK_STATUS_OK)
  {
    return static_cast<StorageState>(state);
  }
  return StorageState::Clean;
}

/**
 * @brief 查詢指定 HandleID 當前被活躍 OuroPtr 持有之根邊緣數量
 * @param id 目標物件 HandleID
 * @return 活躍根邊緣計數
 */
inline uint32_t GetRootEdgeCount(HandleID id)
{
  uint32_t count = 0;
  ork_get_root_edge_count(id, &count);
  return count;
}


/**
 * @brief 向後相容別名
 */
inline bool DehydrateByID(HandleID id)
{
  return Dehydrate(id);
}

// Forward declaration within OuroCore.hpp for mutual recursion between RehydratePayload and RehydrateCallback
template <typename T>
::OuroObject *RehydrateCallback(HandleID id);

namespace detail
{
template <typename T>
inline void ObjectDeleter(::OuroObject *raw_obj)
{
  delete static_cast<T *>(reinterpret_cast<OuroObject *>(raw_obj));
}

template <typename T>
inline void RehydratePayload(HandleID id)
{
  // 1. RAII Exclusive Lock on ControlBlock to guarantee thread-safe serialization for concurrent Rehydrate/Dehydrate calls
  struct RehydrateExclusiveLock
  {
    HandleID m_id;
    explicit RehydrateExclusiveLock(HandleID target_id) : m_id(target_id)
    {
      ork_lock_object(m_id);
    }
    ~RehydrateExclusiveLock()
    {
      ork_unlock_object(m_id);
    }
  } lock_guard(id);

  // 2. Double-Checked Locking: check if object payload was restored while waiting for the lock
  uint8_t state_val = 0;
  if (ork_get_storage_state(id, &state_val) == ORK_STATUS_OK &&
      static_cast<StorageState>(state_val) != StorageState::Dehydrated)
  {
    ::OuroObject *raw_obj = nullptr;
    if (ork_acquire_object_pointer(id, &raw_obj) == ORK_STATUS_OK && raw_obj)
    {
      return;  // Object payload is already loaded
    }
  }

  auto stream = detail::OpenRuntimeReadStream(id);
  if (!stream)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Blueprint stream not found in storage driver or driver not initialized.");
  }

  // 3. Set ActiveOwnerGuard so child handles constructed in T() inherit this object's ID as owner
  ActiveOwnerGuard guard(id);

  // 記憶體配置與 OOM 緊急脫水自救重試機制（以候選冷物件存亡為終止條件，防範並發搶奪）
  void *mem = nullptr;
  while (true)
  {
    try
    {
      mem = ::operator new(sizeof(T));
      break;
    }
    catch (const std::bad_alloc &)
    {
      size_t bytes_needed = sizeof(T);
      if (!detail::TriggerRuntimeRescue(bytes_needed))
      {
        throw;
      }
    }
  }

  T *empty_shell = nullptr;
  try
  {
    detail::ActiveObjectGuard obj_guard;
    empty_shell = ::new (mem) T();
  }
  catch (...)
  {
    ::operator delete(mem);
    throw;
  }

  // RAII guard to prevent memory leak if UnpackBlueprint throws exception
  std::unique_ptr<T> shell_guard(empty_shell);
  shell_guard->SetObjectID(id);

  // 4. Unpack Payload & Edge Roster (Exceptions safely bubble up while shell_guard frees memory)
  UnpackBlueprint(*shell_guard, *stream);

  // 5. Re-bind payload pointer and atomic-bind in-place deleter & rehydrator to ControlBlock
  if (!detail::BindRuntimeObjectPayload(id,
                                        reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(shell_guard.get())),
                                        &ObjectDeleter<T>,
                                        &RehydrateCallback<T>))
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Failed to re-bind payload pointer to ControlBlock.");
  }

  shell_guard.release();
  detail::MarkRuntimeObjectClean(id);

  // 6. 通知自動脫水模組物件已復水
  detail::NotifyRuntimeObjectRehydrated(id);
}
}  // namespace detail

/**
 * @brief Rehydrate a dehydrated object: allocate new empty shell T(), stream load blueprint, and rebind payload to
 * ControlBlock. Target HandleID & child connection handles remain 100% stable!
 */
template <typename T>
OuroPtr<T> Rehydrate(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore Rehydrate Error: Invalid HandleID.");
  }
  detail::RehydratePayload<T>(id);
  return OuroPtr<T>(id);
}

template <typename T>
OuroPtr<T> Rehydrate(const OuroPtr<T> &ptr)
{
  return Rehydrate<T>(ptr.GetTargetID());
}

/**
 * @brief 型別專屬的自動復水回呼函式（Automatic Rehydration Callback）
 *
 * 當受管物件處於脫水狀態（Dehydrated）且底層嘗試存取其指標時（如 ork_acquire_object_pointer），
 * ControlBlock 會透過預先註冊的函式指標觸發此回呼，自動完成物件之記憶體重建、狀態還原（RehydratePayload）
 * 與指標重新綁定，實現對呼叫端透明的延遲復水（Transparent On-Demand Rehydration）。
 *
 * @tparam T 物件型別
 * @param id 物件的 HandleID
 * @return ::OuroObject* 復水後重建的底層物件指標
 */
template <typename T>
inline ::OuroObject *RehydrateCallback(HandleID id)
{
  detail::RehydratePayload<T>(id);
  ::OuroObject *raw_obj = nullptr;
  ork_acquire_object_pointer(id, &raw_obj);
  return raw_obj;
}

// =========================================================================
// --- 受管物件建立工廠 (Managed Object Creation Factory) ---
// =========================================================================

namespace detail
{
template <typename T, typename... Args>
HandleID CreateObjectInternal(Args &&...args)
{
  static_assert(std::is_base_of_v<OuroObject, T>, "T must inherit from OuroObject");
  static_assert(
      std::is_convertible_v<T *, OuroObject *>, "T* must be convertible to OuroObject* (Diamond Inheritance forbidden)"
  );

  HandleID reserved_id = detail::ReserveRuntimeObjectID();

  ActiveOwnerGuard guard(reserved_id);

  // 記憶體配置與 OOM 緊急脫水自救重試機制（以候選冷物件存亡為終止條件，防範並發搶奪）
  void *mem = nullptr;
  while (true)
  {
    try
    {
      mem = ::operator new(sizeof(T));
      break;
    }
    catch (const std::bad_alloc &)
    {
      size_t bytes_needed = sizeof(T);
      if (!detail::TriggerRuntimeRescue(bytes_needed))
      {
        detail::RollbackRuntimeObjectID(reserved_id);
        throw;
      }
    }
  }

  T *obj = nullptr;
  try
  {
    detail::ActiveObjectGuard obj_guard;
    obj = ::new (mem) T(std::forward<Args>(args)...);
  }
  catch (...)
  {
    ::operator delete(mem);
    detail::RollbackRuntimeObjectID(reserved_id);
    throw;
  }

  if (!detail::BindRuntimeObjectPayload(reserved_id,
                                        reinterpret_cast<::OuroObject *>(static_cast<OuroObject *>(obj)),
                                        &ObjectDeleter<T>,
                                        &RehydrateCallback<T>))
  {
    delete obj;
    detail::RollbackRuntimeObjectID(reserved_id);
    throw std::runtime_error("OuroKore Error: Failed to bind payload to reserved HandleID.");
  }

  return reserved_id;
}
}  // namespace detail

/**
 * @brief 建立受管物件（預設自動通報脫水外掛模組進行追蹤與大小登記）
 */
template <typename T, typename... Args>
OuroPtr<T> CreateObject(Args &&...args)
{
  HandleID id = detail::CreateObjectInternal<T>(std::forward<Args>(args)...);
  OuroPtr<T> ptr(id);
  detail::NotifyRuntimeObjectRegistered(id, sizeof(T));
  return ptr;
}

/**
 * @brief 建立永久常駐物件（完全不通報脫水模組，生生世世常駐於記憶體）
 */
template <typename T, typename... Args>
OuroPtr<T> CreatePermanentObject(Args &&...args)
{
  HandleID id = detail::CreateObjectInternal<T>(std::forward<Args>(args)...);
  return OuroPtr<T>(id);
}

// =========================================================================
// --- 核心非同步 I/O 操作 (Async I/O with Context Preservation) ---
// =========================================================================

/**
 * @brief 非同步儲存物件狀態至儲存體
 *
 * 內部透過在任務中建立獨立的 OuroPtr<T> 生命週期守衛，保證在背景寫盤落盤前物件絕不被提前銷毀。
 * @param ptr 目標物件指標
 * @return std::future<AsyncResult<T>> 自帶 HandleID、成功狀態與物件指標的 Future 結果
 */
template <typename T>
inline std::future<AsyncResult<T>> SaveAsync(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore SaveAsync Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  OuroPtr<T> guard(id);

  return detail::SubmitAsyncHelper([guard = std::move(guard)]() mutable -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = guard.GetTargetID();
    try
    {
      if (Save(guard))
      {
        result.success = true;
        result.ptr = std::move(guard);
      }
      else
      {
        result.success = false;
        result.error = "Storage driver save operation returned false.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during SaveAsync.";
    }
    return result;
  });
}

/**
 * @brief 非同步從儲存體載入 / 刷新物件狀態（以寫鎖反序列化覆蓋舊狀態）
 */
template <typename T>
inline std::future<AsyncResult<T>> LoadAsync(const OuroPtr<T> &ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore LoadAsync Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  OuroPtr<T> guard(id);

  return detail::SubmitAsyncHelper([guard = std::move(guard)]() mutable -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = guard.GetTargetID();
    try
    {
      if (Load(guard))
      {
        result.success = true;
        result.ptr = std::move(guard);
      }
      else
      {
        result.success = false;
        result.error = "Storage driver load operation returned false.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during LoadAsync.";
    }
    return result;
  });
}

/**
 * @brief 非同步復水：背景配置空殼、讀檔反序列化並重新綁定 Payload
 * @return std::future<AsyncResult<T>> 包含 HandleID、成功狀態與復水後之全新 OuroPtr<T>
 */
template <typename T>
inline std::future<AsyncResult<T>> RehydrateAsync(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore RehydrateAsync Error: Invalid HandleID.");
  }

  return detail::SubmitAsyncHelper([id]() -> AsyncResult<T> {
    AsyncResult<T> result;
    result.id = id;
    try
    {
      result.ptr = Rehydrate<T>(id);
      result.success = (result.ptr.GetTargetID() != 0);
      if (!result.success)
      {
        result.error = "Rehydrate failed to produce a valid OuroPtr.";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during RehydrateAsync.";
    }
    return result;
  });
}

/**
 * @brief 依 HandleID 發起非同步脫水
 */
inline std::future<AsyncResult<void>> DehydrateAsync(HandleID id)
{
  if (id == 0)
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Invalid HandleID.");
  }

  return detail::SubmitAsyncHelper([id]() -> AsyncResult<void> {
    AsyncResult<void> result;
    result.id = id;
    try
    {
      result.success = Dehydrate(id);
      if (!result.success)
      {
        result.error = "Dehydrate returned false (object might be in-flight, missing, or already dehydrated).";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during DehydrateAsync.";
    }
    return result;
  });
}

/**
 * @brief 透過右值移動 OuroPtr 發起非同步脫水（右值所有權轉移）
 */
template <typename T>
inline std::future<AsyncResult<void>> DehydrateAsync(OuroPtr<T> &&ptr)
{
  if (!ptr)
  {
    throw std::runtime_error("OuroKore DehydrateAsync Error: Invalid or null OuroPtr.");
  }

  HandleID id = ptr.GetTargetID();
  return detail::SubmitAsyncHelper([target_ptr = std::move(ptr), id]() mutable -> AsyncResult<void> {
    AsyncResult<void> result;
    result.id = id;
    try
    {
      result.success = Dehydrate(std::move(target_ptr));
      if (!result.success)
      {
        result.error = "Dehydrate with move-ptr failed (other in-flight references may exist).";
      }
    }
    catch (const std::exception &e)
    {
      result.success = false;
      result.error = e.what();
    }
    catch (...)
    {
      result.success = false;
      result.error = "Unknown exception occurred during DehydrateAsync.";
    }
    return result;
  });
}

// =========================================================================
// --- 多核心批次並行操作 (Parallel Batch APIs) ---
// =========================================================================

/**
 * @brief 批次多核心平行儲存
 * @param batch 要儲存的物件集合
 * @return 依序回傳各物件之 AsyncResult<T> 結果向量
 */
template <typename T>
inline std::vector<AsyncResult<T>> SaveBatch(const std::vector<OuroPtr<T>> &batch)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(batch.size());
  for (const auto &item : batch)
  {
    futures.push_back(SaveAsync(item));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行載入
 */
template <typename T>
inline std::vector<AsyncResult<T>> LoadBatch(const std::vector<OuroPtr<T>> &batch)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(batch.size());
  for (const auto &item : batch)
  {
    futures.push_back(LoadAsync(item));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行復水
 */
template <typename T>
inline std::vector<AsyncResult<T>> RehydrateBatch(const std::vector<HandleID> &ids)
{
  std::vector<std::future<AsyncResult<T>>> futures;
  futures.reserve(ids.size());
  for (HandleID id : ids)
  {
    futures.push_back(RehydrateAsync<T>(id));
  }

  std::vector<AsyncResult<T>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

/**
 * @brief 批次多核心平行脫水
 */
inline std::vector<AsyncResult<void>> DehydrateBatch(const std::vector<HandleID> &ids)
{
  std::vector<std::future<AsyncResult<void>>> futures;
  futures.reserve(ids.size());
  for (HandleID id : ids)
  {
    futures.push_back(DehydrateAsync(id));
  }

  std::vector<AsyncResult<void>> results;
  results.reserve(futures.size());
  for (auto &f : futures)
  {
    results.push_back(f.get());
  }
  return results;
}

}  // namespace ork
