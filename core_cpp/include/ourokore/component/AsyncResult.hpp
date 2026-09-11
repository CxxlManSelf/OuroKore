#pragma once

#include <string>
#include <type_traits>
#include <utility>

#include "Handles.hpp"
#include "Types.hpp"

namespace ork
{

/**
 * @brief 非同步操作通用回傳結果封裝
 *
 * 核心保證：
 * 1. res.id 永遠有效（必定為當初操作之目標 HandleID，方便除錯、日誌記錄與針對性重試）。
 * 2. res.ptr 僅在 success == true 時有效；失敗時保證為空（!res.ptr == true），落實防呆原則。
 * 3. res.error 記錄失敗時之具體例外訊息或錯誤原因，成功時為空字串。
 * 4. 支援 if (res) 情境布林判斷（Contextual Conversion to bool，防止意外隱式轉為整數等型別）。
 * 5. 本結構為 Move-Only 型別（禁止拷貝，確保 OuroPtr 之 Root Edge 生命周期唯一性）。
 */
template <typename T>
struct AsyncResult
{
  template <typename U>
  friend struct AsyncResult;

  HandleID id{0};       ///< 目標物件全域唯一 HandleID（無論成敗必定有效）
  bool success{false};  ///< 操作是否成功
  OuroPtr<T> ptr;       ///< 操作完成後之物件指標（成功時有效，失敗時必定為空）
  std::string error;    ///< 失敗錯誤訊息（成功時為空字串）

  AsyncResult() = default;

  AsyncResult(HandleID target_id, bool is_success, OuroPtr<T> object_ptr, std::string err_msg = "") :
      id(target_id),
      success(is_success),
      ptr(std::move(object_ptr)),
      error(std::move(err_msg))
  {
  }

  // 顯式宣告 Move-Only 語意，禁止拷貝以維護 OuroPtr Root Edge 唯一性
  AsyncResult(const AsyncResult &) = delete;
  AsyncResult &operator=(const AsyncResult &) = delete;

  AsyncResult(AsyncResult &&) noexcept = default;
  AsyncResult &operator=(AsyncResult &&) noexcept = default;

  // 支援多型子類別轉換移動（例如 AsyncResult<Derived> -> AsyncResult<Base>）
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  AsyncResult(AsyncResult<U> &&other) noexcept :
      id(other.id),
      success(other.success),
      ptr(std::move(other.ptr)),
      error(std::move(other.error))
  {
  }

  // 支援多型子類別轉換移動賦值
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  AsyncResult &operator=(AsyncResult<U> &&other) noexcept
  {
    id = other.id;
    success = other.success;
    ptr = std::move(other.ptr);
    error = std::move(other.error);
    return *this;
  }

  /// @brief 情境布林轉換（Contextual Conversion），操作成功時回傳 true
  explicit operator bool() const noexcept
  {
    return success;
  }
};

/**
 * @brief 非同步脫水（DehydrateAsync）特化結果封裝
 *
 * 脫水操作以釋放實體記憶體肉體為目的，故無需亦不應回傳活體 OuroPtr。
 */
template <>
struct AsyncResult<void>
{
  template <typename U>
  friend struct AsyncResult;

  HandleID id{0};       ///< 目標物件全域唯一 HandleID（無論成敗必定有效）
  bool success{false};  ///< 操作是否成功
  std::string error;    ///< 失敗錯誤訊息（成功時為空字串）

  AsyncResult() = default;

  AsyncResult(HandleID target_id, bool is_success, std::string err_msg = "") :
      id(target_id),
      success(is_success),
      error(std::move(err_msg))
  {
  }

  AsyncResult(const AsyncResult &) = default;
  AsyncResult &operator=(const AsyncResult &) = default;
  AsyncResult(AsyncResult &&) noexcept = default;
  AsyncResult &operator=(AsyncResult &&) noexcept = default;

  // 支援從具體型別 AsyncResult<U> 抹除型別轉換為 AsyncResult<void>
  template <typename U>
  AsyncResult(AsyncResult<U> &&other) noexcept :
      id(other.id),
      success(other.success),
      error(std::move(other.error))
  {
  }

  /// @brief 情境布林轉換（Contextual Conversion），操作成功時回傳 true
  explicit operator bool() const noexcept
  {
    return success;
  }
};

}  // namespace ork
