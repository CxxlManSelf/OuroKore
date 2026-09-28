#include "ourokore/base/DynamicLibrary.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// 阻止 MinGW winnt.h 引入 GCC 內部 x86/SIMD 標頭檔，徹底根除 clangd 下 _mm_getcsr、__rdtsc 等內建函式衝突
#ifndef _X86INTRIN_H_INCLUDED
#define _X86INTRIN_H_INCLUDED
#endif
#ifndef _X86GPRINTRIN_H_INCLUDED
#define _X86GPRINTRIN_H_INCLUDED
#endif
#ifndef _XMMINTRIN_H_INCLUDED
#define _XMMINTRIN_H_INCLUDED
#endif
#ifndef _EMMINTRIN_H_INCLUDED
#define _EMMINTRIN_H_INCLUDED
#endif
#ifndef _PMMINTRIN_H_INCLUDED
#define _PMMINTRIN_H_INCLUDED
#endif
#ifndef _TMMINTRIN_H_INCLUDED
#define _TMMINTRIN_H_INCLUDED
#endif
#ifndef _SMMINTRIN_H_INCLUDED
#define _SMMINTRIN_H_INCLUDED
#endif
#ifndef _IMMINTRIN_H_INCLUDED
#define _IMMINTRIN_H_INCLUDED
#endif
#ifndef _MMINTRIN_H_INCLUDED
#define _MMINTRIN_H_INCLUDED
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ork
{

#if defined(_WIN32)
static std::string GetLastWindowsError()
{
  DWORD err = ::GetLastError();
  if (err == 0) return "";

  LPWSTR buffer = nullptr;
  DWORD size = ::FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer),
      0,
      nullptr);

  if (size == 0 || !buffer)
  {
    return "Windows Error " + std::to_string(err);
  }

  std::wstring ws(buffer, size);
  ::LocalFree(buffer);

  while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' '))
  {
    ws.pop_back();
  }

  int utf8_len = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (utf8_len <= 0)
  {
    return "Windows Error " + std::to_string(err);
  }

  std::string utf8_msg(utf8_len, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), utf8_msg.data(), utf8_len, nullptr, nullptr);
  return utf8_msg;
}

static std::filesystem::path Utf8ToPath(std::string_view utf8_str)
{
  if (utf8_str.empty()) return std::filesystem::path();
  int wide_len = ::MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), static_cast<int>(utf8_str.size()), nullptr, 0);
  if (wide_len <= 0) return std::filesystem::path(utf8_str);
  std::wstring wide_str(wide_len, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), static_cast<int>(utf8_str.size()), wide_str.data(), wide_len);
  return std::filesystem::path(std::move(wide_str));
}

static std::string PathToUtf8(const std::filesystem::path &path)
{
  std::wstring ws = path.wstring();
  if (ws.empty()) return "";
  int utf8_len = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (utf8_len <= 0) return "";
  std::string utf8_str(utf8_len, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), utf8_str.data(), utf8_len, nullptr, nullptr);
  return utf8_str;
}
#else
static std::filesystem::path Utf8ToPath(std::string_view utf8_str)
{
  return std::filesystem::path(utf8_str);
}

static std::string PathToUtf8(const std::filesystem::path &path)
{
  return path.string();
}
#endif

/**
 * @brief 內部控制區塊：管理作業系統層級之原生動態庫句柄
 */
class LibraryControlBlock
{
public:
  void *m_native_handle{nullptr};
  std::filesystem::path m_path;

  LibraryControlBlock(void *handle, std::filesystem::path path)
      : m_native_handle(handle), m_path(std::move(path))
  {
  }

  ~LibraryControlBlock()
  {
    if (m_native_handle)
    {
#if defined(_WIN32)
      ::FreeLibrary(static_cast<HMODULE>(m_native_handle));
#else
      ::dlclose(m_native_handle);
#endif
      m_native_handle = nullptr;
    }
  }

  LibraryControlBlock(const LibraryControlBlock &) = delete;
  LibraryControlBlock &operator=(const LibraryControlBlock &) = delete;
};

DynamicLibrary DynamicLibrary::load(const std::filesystem::path &path, LibraryLoadFlags flags)
{
  if (path.empty())
  {
    return DynamicLibrary(nullptr, "Empty library path provided.");
  }

  // 跨平台安全路徑標準化：若目標檔案存在於檔案系統，自動解析為絕對規範化路徑（消除 .、.. 與符號連結）
  std::filesystem::path resolved_path = path;
  std::error_code ec;
  if (std::filesystem::exists(path, ec))
  {
    auto canon = std::filesystem::canonical(path, ec);
    if (!ec)
    {
      resolved_path = std::move(canon);
    }
    else
    {
      ec.clear();
      auto abs_path = std::filesystem::absolute(path, ec);
      if (!ec)
      {
        resolved_path = std::move(abs_path);
      }
    }
  }

  void *native_handle = nullptr;
  std::string error_msg;

#if defined(_WIN32)
  DWORD load_flags = 0;
  if (flags & LibraryLoadFlags::SearchDllDir)
  {
    load_flags |= LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
  }

  // Windows 下嚴格採用 Unicode LoadLibraryW，杜絕中文/特殊字符路徑失敗
  std::wstring wide_path = resolved_path.wstring();
  if (load_flags != 0)
  {
    native_handle = ::LoadLibraryExW(wide_path.c_str(), nullptr, load_flags);
  }
  else
  {
    native_handle = ::LoadLibraryW(wide_path.c_str());
  }

  if (!native_handle)
  {
    error_msg = GetLastWindowsError();
  }
#else
  int mode = 0;
  if (flags & LibraryLoadFlags::ResolveLazy)
  {
    mode |= RTLD_LAZY;
  }
  else
  {
    mode |= RTLD_NOW;
  }

  if (flags & LibraryLoadFlags::ScopeGlobal)
  {
    mode |= RTLD_GLOBAL;
  }
  else
  {
    mode |= RTLD_LOCAL;
  }

  native_handle = ::dlopen(resolved_path.string().c_str(), mode);
  if (!native_handle)
  {
    const char *err = ::dlerror();
    error_msg = err ? std::string(err) : "Unknown dlopen error";
  }
#endif

  if (!native_handle)
  {
    return DynamicLibrary(nullptr, std::move(error_msg));
  }

  auto control_block = std::make_shared<LibraryControlBlock>(native_handle, std::move(resolved_path));
  return DynamicLibrary(std::move(control_block), "");
}

DynamicLibrary DynamicLibrary::load(std::string_view utf8_path, LibraryLoadFlags flags)
{
  return load(Utf8ToPath(utf8_path), flags);
}

void DynamicLibrary::reset() noexcept
{
  m_control_block.reset();
  m_last_error.clear();
}

bool DynamicLibrary::is_loaded() const noexcept
{
  return m_control_block != nullptr && m_control_block->m_native_handle != nullptr;
}

const std::filesystem::path &DynamicLibrary::get_path() const noexcept
{
  static const std::filesystem::path kEmptyPath;
  return m_control_block ? m_control_block->m_path : kEmptyPath;
}

std::string DynamicLibrary::get_path_utf8() const noexcept
{
  return m_control_block ? PathToUtf8(m_control_block->m_path) : "";
}

const std::string &DynamicLibrary::get_last_error() const noexcept
{
  return m_last_error;
}

size_t DynamicLibrary::use_count() const noexcept
{
  return m_control_block ? static_cast<size_t>(m_control_block.use_count()) : 0;
}

void *DynamicLibrary::get_raw_symbol(std::string_view symbol_name) const noexcept
{
  if (!is_loaded() || symbol_name.empty())
  {
    return nullptr;
  }

  std::string name(symbol_name);

#if defined(_WIN32)
  return reinterpret_cast<void *>(::GetProcAddress(static_cast<HMODULE>(m_control_block->m_native_handle), name.c_str()));
#else
  return ::dlsym(m_control_block->m_native_handle, name.c_str());
#endif
}

std::filesystem::path DynamicLibrary::format_filename(std::string_view base_name)
{
#if defined(_WIN32)
  return Utf8ToPath(std::string(base_name) + ".dll");
#elif defined(__APPLE__)
  return Utf8ToPath("lib" + std::string(base_name) + ".dylib");
#else
  return Utf8ToPath("lib" + std::string(base_name) + ".so");
#endif
}

}  // namespace ork
