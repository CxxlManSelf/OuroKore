#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "ourokore/base/export.h"

namespace ork
{

/**
 * @brief 動態庫載入行為旗標
 */
enum class LibraryLoadFlags : uint32_t
{
  Default = 0,
  ResolveNow = 1 << 0,   // 立即解析所有符號（POSIX: RTLD_NOW，Windows 預設）
  ResolveLazy = 1 << 1,  // 延遲按需解析符號（POSIX: RTLD_LAZY）
  ScopeLocal = 1 << 2,   // 符號私有隔離，不外洩給其他模組（POSIX: RTLD_LOCAL，預設）
  ScopeGlobal = 1 << 3,  // 符號全域可見（POSIX: RTLD_GLOBAL）
  SearchDllDir = 1 << 4  // Windows: 優先搜尋 DLL 所在目錄與相依項
};

inline LibraryLoadFlags operator|(LibraryLoadFlags a, LibraryLoadFlags b)
{
  return static_cast<LibraryLoadFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(LibraryLoadFlags a, LibraryLoadFlags b)
{
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// 前置宣告內部控制區塊（用於管理作業系統層級的原生句柄與真正卸載）
class LibraryControlBlock;

/**
 * @brief 跨平台通用動態函式庫載入器 (Dynamic Library Loader)
 *
 * 【設計哲學與安全承諾】
 * 1. 零 OuroKore 概念依賴：純通用現代 C++20 基礎元件，任何外部專案均可獨立使用。
 * 2. 跨平台支援：
 *    - Windows: LoadLibraryW / GetProcAddress / FreeLibrary（全面支援 UTF-8 / Unicode 路徑）。
 *    - POSIX (Linux, macOS): dlopen / dlsym / dlclose。
 * 3. 禁絕手動卸載 (No Manual Unload)：
 *    傳統允許手動 unload() 的載入器常導致使用端手上物件的虛擬函式表 (vtable) 與成員函式代碼段
 *    被提前解除對映，引發致命的 Access Violation / SIGSEGV。
 *    本類別刻意「不提供任何手動 unload 方法」，改由內部引用計數全權託管。
 * 4. 物件生命週期反向錨定 (Life-Bound Retention)：
 *    使用端建立物件時，可透過 bind_lifecycle() 或在自訂 Deleter 閉包中捕捉 DynamicLibrary 實例，
 *    使產生物件與動態庫建立存活錨定。核心保證：只要該 DLL 產生的任何物件仍在記憶體中存活，
 *    該 DLL 的代碼段就絕對不會被卸載；直至所有關聯物件與 DynamicLibrary 變數全數銷毀歸零，
 *    才會安全執行底層作業系統的 FreeLibrary / dlclose。
 * 5. 職責分離：載入器僅專注於動態庫載入與符號解析，不干預物件具體構造模式（由使用端自行定義工廠函式）。
 */
class ORK_BASE_API DynamicLibrary
{
public:
  DynamicLibrary() noexcept = default;
  ~DynamicLibrary() noexcept = default;

  // 支援安全共享複製與移動（內部透過 shared 引用計數管理）
  DynamicLibrary(const DynamicLibrary &) noexcept = default;
  DynamicLibrary &operator=(const DynamicLibrary &) noexcept = default;
  DynamicLibrary(DynamicLibrary &&) noexcept = default;
  DynamicLibrary &operator=(DynamicLibrary &&) noexcept = default;

  /**
   * @brief 靜態工廠：從 UTF-8 編碼字串載入動態函式庫
   *
   * 嚴格遵循 OuroKore 全域 UTF-8 規範。Windows 下內部會顯式將 UTF-8 轉換為 Unicode UTF-16
   * 並調用 LoadLibraryW，杜絕繁體中文/多語系路徑被 Windows STL 誤解為本地 ANSI 代碼頁引發的亂碼失敗。
   *
   * @param utf8_path UTF-8 編碼之動態庫路徑
   * @param flags 載入模式旗標
   * @return 若成功載入傳回有效之 DynamicLibrary；失敗則傳回無效實例
   */
  static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = LibraryLoadFlags::Default);

  /**
   * @brief 靜態工廠：自 C-style 字串（UTF-8 編碼）載入動態庫
   * 消解字串字面量傳入時 std::string_view 與 std::filesystem::path 的重載二義性
   */
  static DynamicLibrary load(const char *utf8_path, LibraryLoadFlags flags = LibraryLoadFlags::Default)
  {
    return load(std::string_view(utf8_path ? utf8_path : ""), flags);
  }

  /**
   * @brief 靜態工廠：載入指定的動態函式庫
   * @param path 動態庫路徑
   * @param flags 載入模式旗標
   * @return 若成功載入傳回有效之 DynamicLibrary；失敗則傳回無效實例，可透過 get_last_error() 查詢原因
   */
  static DynamicLibrary load(const std::filesystem::path &path, LibraryLoadFlags flags = LibraryLoadFlags::Default);

  /**
   * @brief 查詢動態庫是否已成功載入且有效
   */
  bool is_loaded() const noexcept;
  explicit operator bool() const noexcept { return is_loaded(); }

  /**
   * @brief 取得載入之檔案路徑 (std::filesystem::path)
   */
  const std::filesystem::path &get_path() const noexcept;

  /**
   * @brief 取得載入之檔案路徑（保證 100% 為 UTF-8 字串）
   */
  std::string get_path_utf8() const noexcept;

  /**
   * @brief 取得最後一次錯誤訊息（保證 100% 為 UTF-8 字串，若無錯誤則為空）
   */
  const std::string &get_last_error() const noexcept;

  /**
   * @brief 取得當前動態庫的存活引用計數（包含 DynamicLibrary 變數與所有受管活體物件）
   */
  size_t use_count() const noexcept;

  /**
   * @brief 型別安全符號獲取
   * @tparam FuncT 函式簽章或指標，支援 void()、void(*)()、int(int, int) 等
   * @param symbol_name 匯出符號名稱
   * @return 函式指標；若找不到符號則安全傳回 nullptr
   */
  template <typename FuncT>
  auto get_symbol(std::string_view symbol_name) const noexcept
  {
    if constexpr (std::is_function_v<FuncT>)
    {
      return reinterpret_cast<FuncT *>(get_raw_symbol(symbol_name));
    }
    else if constexpr (std::is_pointer_v<FuncT>)
    {
      return reinterpret_cast<FuncT>(get_raw_symbol(symbol_name));
    }
    else
    {
      return reinterpret_cast<FuncT *>(get_raw_symbol(symbol_name));
    }
  }

  /**
   * @brief 取得原始裸符號指標
   */
  void *get_raw_symbol(std::string_view symbol_name) const noexcept;

  /**
   * @brief 將物件裸指標錨定至本 DLL 生命週期 (Life-Bound Anchor)
   *
   * 回傳之 std::shared_ptr 在其 Deleter 內部持有一份動態庫存活權杖。
   * 銷毀順序嚴格保證：
   * 1. 呼叫傳入的 deleter_fn(p) 釋放物件（此時 DLL 代碼段 100% 有效）。
   * 2. 扣減動態庫引用計數；若歸零則底層自動安全執行 FreeLibrary / dlclose。
   *
   * @tparam InterfaceT 介面型別
   * @tparam DeleterFnT 銷毀回呼函式型別，簽章需接受 InterfaceT*
   * @param raw_ptr 模組建立的物件指標
   * @param deleter_fn 釋放回呼函式
   */
  template <typename InterfaceT, typename DeleterFnT>
  std::shared_ptr<InterfaceT> bind_lifecycle(InterfaceT *raw_ptr, DeleterFnT deleter_fn) const
  {
    if (!raw_ptr || !m_control_block)
    {
      return nullptr;
    }

    auto keep_alive = m_control_block;
    return std::shared_ptr<InterfaceT>(raw_ptr, [keep_alive, deleter_fn](InterfaceT *p) {
      if (p)
      {
        deleter_fn(p);
      }
    });
  }

  /**
   * @brief 依據當前平台自動格式化動態庫檔名
   * @param base_name 基礎模組名稱（如 "my_plugin"）
   * @return Windows: "my_plugin.dll", Linux: "libmy_plugin.so", macOS: "libmy_plugin.dylib"
   */
  static std::filesystem::path format_filename(std::string_view base_name);

private:
  explicit DynamicLibrary(std::shared_ptr<LibraryControlBlock> control_block, std::string last_error = "") noexcept
      : m_control_block(std::move(control_block)), m_last_error(std::move(last_error))
  {
  }

  std::shared_ptr<LibraryControlBlock> m_control_block;
  mutable std::string m_last_error;
};

}  // namespace ork
