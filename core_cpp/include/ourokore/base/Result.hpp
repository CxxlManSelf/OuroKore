#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace ork::base
{

/**
 * @brief 通用標準狀態碼列舉 (StatusCode)
 * 涵蓋常見系統與業務錯誤，並與純 C ABI 之狀態碼保持對齊相容。
 */
enum class StatusCode : int32_t
{
  Ok = 0,
  InvalidArgument = -1,
  NotFound = -2,
  AlreadyExists = -3,
  PermissionDenied = -4,
  ResourceExhausted = -5,
  FailedPrecondition = -6,
  Aborted = -7,
  OutOfRange = -8,
  Unimplemented = -9,
  Internal = -10,
  Unavailable = -11,
  DataLoss = -12,
  Timeout = -13,
  IoError = -14,
  Corrupted = -15
};

/**
 * @brief 通用狀態與錯誤描述物件 (Status)
 * 結合整數狀態碼與 UTF-8 錯誤訊息，兼具高效率與強大診斷能力。
 */
class Status
{
public:
  Status() noexcept :
      m_code(static_cast<int32_t>(StatusCode::Ok))
  {
  }

  Status(StatusCode code, std::string message = "") :
      m_code(static_cast<int32_t>(code)),
      m_message(std::move(message))
  {
  }

  Status(int32_t raw_code, std::string message = "") :
      m_code(raw_code),
      m_message(std::move(message))
  {
  }

  [[nodiscard]] bool ok() const noexcept
  {
    return m_code == 0;
  }
  [[nodiscard]] bool is_error() const noexcept
  {
    return m_code != 0;
  }

  [[nodiscard]] int32_t code() const noexcept
  {
    return m_code;
  }
  [[nodiscard]] StatusCode status_code() const noexcept
  {
    return static_cast<StatusCode>(m_code);
  }
  [[nodiscard]] const std::string &message() const noexcept
  {
    return m_message;
  }

  [[nodiscard]] std::string to_string() const
  {
    if (ok())
    {
      return "OK";
    }
    if (m_message.empty())
    {
      return std::format("Error(code={})", m_code);
    }
    return std::format("Error(code={}, message=\"{}\")", m_code, m_message);
  }

  // 快捷靜態工廠方法
  static Status Ok() noexcept
  {
    return Status();
  }
  static Status InvalidArgument(std::string_view msg = "")
  {
    return Status(StatusCode::InvalidArgument, std::string(msg));
  }
  static Status NotFound(std::string_view msg = "")
  {
    return Status(StatusCode::NotFound, std::string(msg));
  }
  static Status AlreadyExists(std::string_view msg = "")
  {
    return Status(StatusCode::AlreadyExists, std::string(msg));
  }
  static Status PermissionDenied(std::string_view msg = "")
  {
    return Status(StatusCode::PermissionDenied, std::string(msg));
  }
  static Status ResourceExhausted(std::string_view msg = "")
  {
    return Status(StatusCode::ResourceExhausted, std::string(msg));
  }
  static Status FailedPrecondition(std::string_view msg = "")
  {
    return Status(StatusCode::FailedPrecondition, std::string(msg));
  }
  static Status Aborted(std::string_view msg = "")
  {
    return Status(StatusCode::Aborted, std::string(msg));
  }
  static Status OutOfRange(std::string_view msg = "")
  {
    return Status(StatusCode::OutOfRange, std::string(msg));
  }
  static Status Unimplemented(std::string_view msg = "")
  {
    return Status(StatusCode::Unimplemented, std::string(msg));
  }
  static Status Internal(std::string_view msg = "")
  {
    return Status(StatusCode::Internal, std::string(msg));
  }
  static Status Unavailable(std::string_view msg = "")
  {
    return Status(StatusCode::Unavailable, std::string(msg));
  }
  static Status DataLoss(std::string_view msg = "")
  {
    return Status(StatusCode::DataLoss, std::string(msg));
  }
  static Status Timeout(std::string_view msg = "")
  {
    return Status(StatusCode::Timeout, std::string(msg));
  }
  static Status IoError(std::string_view msg = "")
  {
    return Status(StatusCode::IoError, std::string(msg));
  }
  static Status Corrupted(std::string_view msg = "")
  {
    return Status(StatusCode::Corrupted, std::string(msg));
  }

  friend bool operator==(const Status &lhs, const Status &rhs) noexcept
  {
    return lhs.m_code == rhs.m_code && lhs.m_message == rhs.m_message;
  }

  friend bool operator==(const Status &lhs, StatusCode code) noexcept
  {
    return lhs.m_code == static_cast<int32_t>(code);
  }

private:
  int32_t m_code{0};
  std::string m_message;
};

/**
 * @brief Result 泛型結果型別別名 (基於 ISO C++23 std::expected)
 * 預設錯誤型別為 ork::base::Status，成功型別為 T。
 */
template <typename T, typename E = Status>
using Result = std::expected<T, E>;

/**
 * @brief StatusOr 別名 (類似 Google Abseil / Chromium 慣例)
 */
template <typename T>
using StatusOr = std::expected<T, Status>;

/**
 * @brief 輔助建構 std::unexpected 的標準工廠函式
 * 避免與舊標準 <exception> 中的 void unexpected() 符號發生衝突。
 */
template <typename E = Status>
[[nodiscard]] constexpr auto unexpected(E &&err)
{
  return std::unexpected<std::decay_t<E>>(std::forward<E>(err));
}

/**
 * @brief 輔助建構成功 Result 的便捷函式
 */
template <typename T>
[[nodiscard]] inline auto Ok(T &&val)
{
  return Result<std::decay_t<T>>(std::forward<T>(val));
}

[[nodiscard]] inline auto Ok()
{
  return Result<void>();
}

/**
 * @brief 輔助建構錯誤 Result 的便捷函式
 */
[[nodiscard]] inline auto Err(Status s)
{
  return std::unexpected<Status>(std::move(s));
}

[[nodiscard]] inline auto Err(StatusCode code, std::string_view msg = "")
{
  return std::unexpected<Status>(Status(code, std::string(msg)));
}

[[nodiscard]] inline auto Err(int32_t raw_code, std::string_view msg = "")
{
  return std::unexpected<Status>(Status(raw_code, std::string(msg)));
}

}  // namespace ork::base

namespace ork
{
using base::Err;
using base::Ok;
using base::Result;
using base::Status;
using base::StatusCode;
using base::StatusOr;
using base::unexpected;
}  // namespace ork
