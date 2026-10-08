#include "ourokore/base/DynamicLibrary.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

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

class LibraryControlBlock;

/**
 * @brief 全域動態函式庫弱引用登錄表 (Library Registry)
 * 透過弱引用快取已載入之動態函式庫控制區塊，精準分辨 0->1 首次載入與 N->N+1 重複載入，
 * 且不干預產生物件全數銷毀後的 1->0 自動安全卸載。
 */
struct LibraryRegistry
{
  std::mutex mutex;
  std::unordered_map<std::filesystem::path, std::weak_ptr<LibraryControlBlock>> table;
  std::unordered_map<std::filesystem::path, void *> resident_table;
};

static LibraryRegistry &GetLibraryRegistry()
{
  static LibraryRegistry s_registry;
  return s_registry;
}

static void UnregisterControlBlock(const std::filesystem::path &path)
{
  auto &reg = GetLibraryRegistry();
  std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.table.find(path);
  if (it != reg.table.end())
  {
    if (it->second.expired())
    {
      reg.table.erase(it);
    }
  }
  reg.resident_table.erase(path);
}

static void MarkAsResident(const std::filesystem::path &path, void *handle)
{
  if (!handle) return;
  auto &reg = GetLibraryRegistry();
  std::lock_guard<std::mutex> lock(reg.mutex);
  reg.resident_table[path] = handle;
}

/**
 * @brief 內部控制區塊：管理作業系統層級之原生動態庫句柄與收尾掛鉤
 */
class LibraryControlBlock
{
public:
  void *m_native_handle{nullptr};
  std::filesystem::path m_path;
  std::function<bool()> m_shutdown_hook;
  std::vector<std::function<void()>> m_post_unload_hooks;
  std::mutex m_hooks_mutex;
  std::atomic<bool> m_deferred_unload{false};

  LibraryControlBlock(void *handle, std::filesystem::path path)
      : m_native_handle(handle), m_path(std::move(path))
  {
  }

  void set_shutdown_hook(std::function<bool()> hook)
  {
    std::lock_guard<std::mutex> lock(m_hooks_mutex);
    m_shutdown_hook = std::move(hook);
  }

  void add_post_unload_hook(std::function<void()> hook)
  {
    if (!hook) return;
    std::lock_guard<std::mutex> lock(m_hooks_mutex);
    m_post_unload_hooks.push_back(std::move(hook));
  }

  void enable_deferred_unload(bool enable) noexcept
  {
    m_deferred_unload.store(enable, std::memory_order_relaxed);
  }

  bool is_deferred_unload_enabled() const noexcept
  {
    return m_deferred_unload.load(std::memory_order_relaxed);
  }

  ~LibraryControlBlock()
  {
    std::function<bool()> shutdown_hook;
    std::vector<std::function<void()>> post_hooks;
    {
      std::lock_guard<std::mutex> lock(m_hooks_mutex);
      shutdown_hook = std::move(m_shutdown_hook);
      post_hooks = std::move(m_post_unload_hooks);
    }

    void *handle = m_native_handle;
    m_native_handle = nullptr;
    std::filesystem::path path = m_path;
    bool is_deferred = m_deferred_unload.load(std::memory_order_relaxed);

    auto execute_shutdown_and_unload = [
      handle,
      path = std::move(path),
      shutdown_hook = std::move(shutdown_hook),
      post_hooks = std::move(post_hooks)
    ]() mutable {
      // 1. 執行模組唯一的善後收尾函式（若有註冊）
      bool can_unload = true;
      if (shutdown_hook)
      {
        try
        {
          can_unload = shutdown_hook();
        }
        catch (...)
        {
          can_unload = true;
        }
      }

      // 2. 若外掛回覆拒絕結束（can_unload == false），轉為常駐！
      if (!can_unload)
      {
        // 絕對不呼叫 FreeLibrary！不觸發 post_unload_hooks！
        // 註冊路徑不提早除名，將原生句柄轉移至常駐表，供未來 load() 無縫重用！
        MarkAsResident(path, handle);
        return;
      }

      // 3. 外掛確認允許卸載：此時才自全域快取表中除名路徑！
      UnregisterControlBlock(path);

      // 4. 作業系統物理卸載
      if (handle)
      {
#if defined(_WIN32)
        ::FreeLibrary(static_cast<HMODULE>(handle));
#else
        ::dlclose(handle);
#endif
      }

      // 5. 嚴格在物理卸載之後依序觸發後置通知
      for (auto &hook : post_hooks)
      {
        if (hook)
        {
          try
          {
            hook();
          }
          catch (...)
          {
          }
        }
      }
    };

    if (is_deferred)
    {
      // 離棧延遲卸載模式：移交獨立背景執行緒執行
      std::thread(std::move(execute_shutdown_and_unload)).detach();
    }
    else
    {
      // 純同步就地卸載
      execute_shutdown_and_unload();
    }
  }

  LibraryControlBlock(const LibraryControlBlock &) = delete;
  LibraryControlBlock &operator=(const LibraryControlBlock &) = delete;
};

DynamicLibrary DynamicLibrary::load(const std::filesystem::path &path, LibraryLoadFlags flags)
{
  if (path.empty())
  {
    return DynamicLibrary(nullptr, "Empty library path provided.", false);
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
  else
  {
    ec.clear();
    auto abs_path = std::filesystem::absolute(path, ec);
    if (!ec)
    {
      resolved_path = std::move(abs_path);
    }
  }

  // 💡【關鍵分辨點】：檢查快取中是否已有存活的控制區塊（進程中已載入此模組）
  auto &reg = GetLibraryRegistry();
  {
    std::lock_guard<std::mutex> lock(reg.mutex);
    auto it = reg.table.find(resolved_path);
    if (it != reg.table.end())
    {
      if (auto existing_cb = it->second.lock())
      {
        // ⏩ 分辨結果：重複載入 (N -> N+1)
        // 共享既有控制區塊，is_first_loaded 標記為 false，避免重複觸發全域初始化
        return DynamicLibrary(std::move(existing_cb), "", false);
      }
    }

    // 檢查常駐模組表：若該模組先前拒絕卸載已轉常駐，重用常駐原生句柄
    auto res_it = reg.resident_table.find(resolved_path);
    if (res_it != reg.resident_table.end())
    {
      void *resident_handle = res_it->second;
      reg.resident_table.erase(res_it);
      auto control_block = std::make_shared<LibraryControlBlock>(resident_handle, resolved_path);
      reg.table[resolved_path] = control_block;
      return DynamicLibrary(std::move(control_block), "", false);
    }
  }

  // 🚀 分辨結果：首次載入 (0 -> 1)
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
    return DynamicLibrary(nullptr, std::move(error_msg), false);
  }

  auto control_block = std::make_shared<LibraryControlBlock>(native_handle, resolved_path);

  // 將新實例登記至全域快取表
  {
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.table[resolved_path] = control_block;
  }

  // 回傳首次載入實例 (is_first_loaded = true)
  return DynamicLibrary(std::move(control_block), "", true);
}

DynamicLibrary DynamicLibrary::load(std::string_view utf8_path, LibraryLoadFlags flags)
{
  return load(Utf8ToPath(utf8_path), flags);
}

void DynamicLibrary::reset() noexcept
{
  m_control_block.reset();
  m_last_error.clear();
  m_is_first_loaded = false;
}

bool DynamicLibrary::is_first_loaded() const noexcept
{
  return m_is_first_loaded;
}

std::shared_ptr<const void> DynamicLibrary::create_lifetime_token() const noexcept
{
  return m_control_block;
}

void DynamicLibrary::set_shutdown_hook(std::function<bool()> hook)
{
  if (m_control_block)
  {
    m_control_block->set_shutdown_hook(std::move(hook));
  }
}

bool DynamicLibrary::register_shutdown_symbol(std::string_view symbol_name)
{
  if (!is_loaded() || symbol_name.empty())
  {
    return false;
  }
  // 相容傳統無回傳值收尾（void()）
  using VoidShutdownFn = void (*)();
  auto fn = get_symbol<VoidShutdownFn>(symbol_name);
  if (!fn)
  {
    return false;
  }
  set_shutdown_hook([fn]() -> bool {
    fn();
    return true; // 預設允許卸載
  });
  return true;
}

void DynamicLibrary::add_post_unload_hook(std::function<void()> hook)
{
  if (m_control_block && hook)
  {
    m_control_block->add_post_unload_hook(std::move(hook));
  }
}

void DynamicLibrary::enable_deferred_unload(bool enable) noexcept
{
  if (m_control_block)
  {
    m_control_block->enable_deferred_unload(enable);
  }
}

bool DynamicLibrary::is_deferred_unload_enabled() const noexcept
{
  return m_control_block ? m_control_block->is_deferred_unload_enabled() : false;
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

WeakDynamicLibrary DynamicLibrary::to_weak() const noexcept
{
  return WeakDynamicLibrary(*this);
}

// ============================================================================
// WeakDynamicLibrary 實作
// ============================================================================

WeakDynamicLibrary::WeakDynamicLibrary() noexcept = default;
WeakDynamicLibrary::~WeakDynamicLibrary() noexcept = default;

WeakDynamicLibrary::WeakDynamicLibrary(const WeakDynamicLibrary &) noexcept = default;
WeakDynamicLibrary &WeakDynamicLibrary::operator=(const WeakDynamicLibrary &) noexcept = default;
WeakDynamicLibrary::WeakDynamicLibrary(WeakDynamicLibrary &&) noexcept = default;
WeakDynamicLibrary &WeakDynamicLibrary::operator=(WeakDynamicLibrary &&) noexcept = default;

WeakDynamicLibrary::WeakDynamicLibrary(const DynamicLibrary &lib) noexcept
    : m_control_block(lib.m_control_block)
{
}

WeakDynamicLibrary &WeakDynamicLibrary::operator=(const DynamicLibrary &lib) noexcept
{
  m_control_block = lib.m_control_block;
  return *this;
}

DynamicLibrary WeakDynamicLibrary::lock() const noexcept
{
  auto sp = m_control_block.lock();
  if (!sp)
  {
    return DynamicLibrary(nullptr, "Dynamic library has expired or is unloaded.");
  }
  return DynamicLibrary(std::move(sp), "");
}

bool WeakDynamicLibrary::expired() const noexcept
{
  return m_control_block.expired();
}

size_t WeakDynamicLibrary::use_count() const noexcept
{
  return static_cast<size_t>(m_control_block.use_count());
}

void WeakDynamicLibrary::reset() noexcept
{
  m_control_block.reset();
}

std::weak_ptr<const void> WeakDynamicLibrary::create_weak_lifetime_token() const noexcept
{
  return m_control_block;
}

}  // namespace ork
