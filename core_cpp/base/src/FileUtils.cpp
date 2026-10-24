#include "ourokore/base/FileUtils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ork::base::fs
{

namespace
{

#if defined(_WIN32)
std::wstring utf8_to_wide(std::string_view utf8_str)
{
  if (utf8_str.empty())
  {
    return std::wstring();
  }
  int size_needed = MultiByteToWideChar(
      CP_UTF8, 0, utf8_str.data(), static_cast<int>(utf8_str.size()), nullptr, 0);
  if (size_needed <= 0)
  {
    return std::wstring();
  }
  std::wstring wstr(size_needed, 0);
  MultiByteToWideChar(
      CP_UTF8, 0, utf8_str.data(), static_cast<int>(utf8_str.size()), wstr.data(), size_needed);
  return wstr;
}
#endif

std::string generate_temp_path(std::string_view target_path)
{
  static std::mt19937_64 rng(static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  uint64_t rand_val = rng();

#if defined(_WIN32)
  uint32_t pid = GetCurrentProcessId();
#else
  uint32_t pid = static_cast<uint32_t>(getpid());
#endif

  return std::string(target_path) + ".tmp." + std::to_string(pid) + "." + std::to_string(rand_val);
}

}  // namespace

Result<void> ensure_directory(std::string_view dir_path)
{
  if (dir_path.empty())
  {
    return {};
  }
  std::error_code ec;
#if defined(_WIN32)
  std::wstring wdir = utf8_to_wide(dir_path);
  std::filesystem::create_directories(std::filesystem::path(wdir), ec);
#else
  std::filesystem::create_directories(std::filesystem::u8path(dir_path), ec);
#endif
  if (ec)
  {
    return unexpected(Status::Internal("建立目錄失敗: " + ec.message()));
  }
  return {};
}

bool exists(std::string_view path) noexcept
{
  std::error_code ec;
#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);
  return std::filesystem::exists(std::filesystem::path(wpath), ec);
#else
  return std::filesystem::exists(std::filesystem::u8path(path), ec);
#endif
}

Result<void> remove_file(std::string_view path)
{
  std::error_code ec;
#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);
  std::filesystem::remove(std::filesystem::path(wpath), ec);
#else
  std::filesystem::remove(std::filesystem::u8path(path), ec);
#endif
  if (ec)
  {
    return unexpected(Status::Internal("刪除檔案失敗: " + ec.message()));
  }
  return {};
}

Result<uint64_t> get_file_size(std::string_view path)
{
  std::error_code ec;
#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);
  auto sz = std::filesystem::file_size(std::filesystem::path(wpath), ec);
#else
  auto sz = std::filesystem::file_size(std::filesystem::u8path(path), ec);
#endif
  if (ec)
  {
    return unexpected(Status::NotFound("無法取得檔案大小: " + ec.message()));
  }
  return static_cast<uint64_t>(sz);
}

Result<void> atomic_write(
    std::string_view target_path,
    std::span<const uint8_t> data,
    WriteMode mode)
{
  if (target_path.empty())
  {
    return unexpected(Status::InvalidArgument("檔案路徑不得為空"));
  }

  if (mode == WriteMode::FailIfExists && exists(target_path))
  {
    return unexpected(Status::AlreadyExists("目標檔案已存在且設定為不可覆寫: " + std::string(target_path)));
  }

  // 1. 自動確保父目錄存在
  std::filesystem::path p =
#if defined(_WIN32)
      std::filesystem::path(utf8_to_wide(target_path));
#else
      std::filesystem::u8path(target_path);
#endif

  if (p.has_parent_path())
  {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
    {
      return unexpected(Status::Internal("建立父目錄失敗: " + ec.message()));
    }
  }

  // 2. 建立同目錄臨時檔
  std::string temp_path = generate_temp_path(target_path);

#if defined(_WIN32)
  std::wstring wtemp = utf8_to_wide(temp_path);
  std::wstring wtarget = utf8_to_wide(target_path);

  // 建立暫存檔並寫入
  HANDLE hFile = CreateFileW(
      wtemp.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (hFile == INVALID_HANDLE_VALUE)
  {
    return unexpected(Status::Internal("無法建立臨時檔案: " + std::to_string(GetLastError())));
  }

  DWORD bytes_written = 0;
  if (!data.empty())
  {
    if (!WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &bytes_written, nullptr) ||
        bytes_written != data.size())
    {
      CloseHandle(hFile);
      DeleteFileW(wtemp.c_str());
      return unexpected(Status::Internal("臨時檔案寫入不完整"));
    }
  }

  // 磁碟物理刷盤 (FlushFileBuffers)
  FlushFileBuffers(hFile);
  CloseHandle(hFile);

  // 3. 原生原子替換 (MoveFileExW)
  DWORD flags = MOVEFILE_WRITE_THROUGH;
  if (mode == WriteMode::ReplaceExisting)
  {
    flags |= MOVEFILE_REPLACE_EXISTING;
  }

  if (!MoveFileExW(wtemp.c_str(), wtarget.c_str(), flags))
  {
    DWORD err = GetLastError();
    DeleteFileW(wtemp.c_str());
    return unexpected(Status::Internal("原子替換檔案失敗: Win32 錯誤碼 " + std::to_string(err)));
  }

#else
  // POSIX 實作
  int fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    return unexpected(Status::Internal("無法建立臨時檔案"));
  }

  size_t written = 0;
  while (written < data.size())
  {
    ssize_t n = write(fd, data.data() + written, data.size() - written);
    if (n < 0)
    {
      close(fd);
      unlink(temp_path.c_str());
      return unexpected(Status::Internal("臨時檔案寫入錯誤"));
    }
    written += static_cast<size_t>(n);
  }

  // 物理刷盤
  fsync(fd);
  close(fd);

  // 原生原子替換 (rename)
  if (mode == WriteMode::FailIfExists && exists(target_path))
  {
    unlink(temp_path.c_str());
    return unexpected(Status::AlreadyExists("目標檔案已存在"));
  }

  if (rename(temp_path.c_str(), std::string(target_path).c_str()) != 0)
  {
    unlink(temp_path.c_str());
    return unexpected(Status::Internal("原子 rename 替換檔案失敗"));
  }
#endif

  return {};
}

Result<void> atomic_write(
    std::string_view target_path,
    std::string_view text,
    WriteMode mode)
{
  auto span_data = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(text.data()), text.size());
  return atomic_write(target_path, span_data, mode);
}

Result<std::vector<uint8_t>> read_all_bytes(std::string_view path)
{
#if defined(_WIN32)
  std::wstring wpath = utf8_to_wide(path);
  std::ifstream file(std::filesystem::path(wpath), std::ios::binary | std::ios::ate);
#else
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
#endif

  if (!file.is_open())
  {
    return unexpected(Status::NotFound("無法開啟檔案: " + std::string(path)));
  }

  auto size = file.tellg();
  if (size < 0)
  {
    return unexpected(Status::Internal("無法取得檔案大小"));
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  if (size > 0)
  {
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    if (!file)
    {
      return unexpected(Status::Internal("讀取檔案資料失敗"));
    }
  }

  return buffer;
}

Result<std::string> read_all_text(std::string_view path)
{
  auto bytes_res = read_all_bytes(path);
  if (!bytes_res)
  {
    return unexpected(bytes_res.error());
  }
  const auto &buf = *bytes_res;
  return std::string(reinterpret_cast<const char *>(buf.data()), buf.size());
}

}  // namespace ork::base::fs
