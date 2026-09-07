#pragma once

#include <cstdint>
#include <string>

namespace ork
{

using HandleID = uint64_t;

template <typename T>
class OuroPtr;

/**
 * @brief 非同步操作通用回傳結果封裝
 *
 * 核心保證：
 * 1. res.id 永遠有效（必定為當初操作之目標 HandleID，方便除錯、日誌記錄與針對性重試）。
 * 2. res.ptr 僅在 success == true 時有效；失敗時保證為空（!res.ptr == true），落實防呆原則。
 * 3. res.error 記錄失敗時之具體例外訊息或錯誤原因，成功時為空字串。
 * 4. 支援 if (res) 隱式布林判斷。
 */
template <typename T>
struct AsyncResult
{
  HandleID id{0};       ///< 目標物件全域唯一 HandleID（無論成敗必定有效）
  bool success{false};  ///< 操作是否成功
  OuroPtr<T> ptr;       ///< 操作完成後之物件指標（成功時有效，失敗時必定為空）
  std::string error;    ///< 失敗錯誤訊息（成功時為空字串）

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
  HandleID id{0};       ///< 目標物件全域唯一 HandleID（無論成敗必定有效）
  bool success{false};  ///< 操作是否成功
  std::string error;    ///< 失敗錯誤訊息（成功時為空字串）

  explicit operator bool() const noexcept
  {
    return success;
  }
};

}  // namespace ork
