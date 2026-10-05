#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
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

// 前置宣告內部控制區塊與弱引用觀察者
class LibraryControlBlock;
class WeakDynamicLibrary;

/**
 * @brief 跨平台通用動態函式庫載入器 (Dynamic Library Loader)
 *
 * 【設計哲學與生命週期管理原則】
 * 1. 零 OuroKore 概念依賴：純通用現代 C++20 基礎元件，任何外部專案均可獨立使用。
 * 2. 跨平台支援：
 *    - Windows: LoadLibraryW / GetProcAddress / FreeLibrary（全面支援 UTF-8 / Unicode 路徑）。
 *    - POSIX (Linux, macOS): dlopen / dlsym / dlclose。
 * 3. 禁絕手動卸載 (No Manual Unload)：
 *    載入器刻意「不提供任何手動卸載 (unload)」API。傳統手動 unload 常因其他執行緒或物件仍在存取，
 *    導致虛擬函式表 (vtable) 與成員函式代碼段被提前解除映射，引發致命的 Access Violation / SIGSEGV。
 * 4. 物件生命週期反向錨定與自動卸載 (Life-Bound Retention & Automatic Unload)：
 *    本設計的核心理念，是希望應用端將動態庫「所產生的物件」與動態庫本身進行生命週期綁定
 *    （例如透過 bind_lifecycle() 或在自訂工廠 Deleter 閉包中捕捉 DynamicLibrary 存活實例）。
 *    當該動態庫產生的所有物件全部解構完畢後，底層動態庫才會在引用計數歸零時自動且安全地執行卸載（FreeLibrary / dlclose）。
 * 5. ⚠️ 關鍵約束：load() 回傳值的持有與放棄 (Crucial Invariant on load() Return Handle)：
 *    呼叫端必須明白，ork::DynamicLibrary::load() 的回傳值本身「就已經將動態庫綁定（持有一份引用計數）」。
 *    如果不放棄該回傳值（例如將其長存於成員變數、全域變數，或未離開作用域/未重設），動態庫的引用計數就永遠不會歸零，
 *    該 DLL 就絕對不會被卸載！
 *    因此，若要達成「產生物件全部解構後自動卸載」，應用端必須在完成物件構造與綁定後，
 *    主動放棄/釋放 load() 回傳的 DynamicLibrary 變數（例如讓其自然隨作用域解構，或呼叫 reset() 放棄持有），
 *    將存活權杖全權交給產生的物件託管。
 * 6. 職責分離：載入器僅專注於動態庫載入、符號解析與生命週期反向錨定，不干預物件具體構造模式（由使用端自行定義工廠函式）。
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
   * ⚠️ 核心約束注意：
   * 本函式傳回之 DynamicLibrary 實例本身已經持有一份動態庫引用（use_count >= 1）。
   * 載入器不提供手動卸載，而是設計為由產生的物件綁定生命週期並在全部解構後自動卸載。
   * 若呼叫端不放棄本函式傳回之變數（例如未離開作用域、未 reset() 或長存於成員/全域變數），
   * 該 DLL 就絕對不會被卸載！
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
   * 
   * ⚠️ 核心約束注意：
   * 本函式傳回之 DynamicLibrary 實例本身已經持有一份動態庫引用（use_count >= 1）。
   * 若要達成「由其產生的物件全部解構後 DLL 自動卸載」，呼叫端在完成物件綁定後必須放棄此變數（或呼叫 reset()）。
   *
   * @param path 動態庫路徑
   * @param flags 載入模式旗標
   * @return 若成功載入傳回有效之 DynamicLibrary；失敗則傳回無效實例，可透過 get_last_error() 查詢原因
   */
  static DynamicLibrary load(const std::filesystem::path &path, LibraryLoadFlags flags = LibraryLoadFlags::Default);

  /**
   * @brief 放棄當前持有的動態庫句柄（扣減引用計數）
   *
   * 使當前 DynamicLibrary 實例變為未載入狀態。若動態庫已透過 bind_lifecycle() 
   * 或工廠 Deleter 閉包與產生的物件綁定，呼叫端可透過 reset() 放棄 load() 回傳的初始句柄，
   * 使得動態庫唯一的存活權杖全權移交給物件持有，當所有物件全數解構後便會自動觸發底層動態庫卸載。
   */
  void reset() noexcept;

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
   * 2. 扣減動態庫引用計數；若歸零且應用端已放棄 load() 句柄，底層自動安全執行 FreeLibrary / dlclose。
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
   * @brief 產生一個純生命週期存活權杖 (Pure Lifetime Token)
   *
   * 回傳之 std::shared_ptr 在其控制區塊內部持有一份動態函式庫存活引用。
   * 與 bind_lifecycle() 相比，本函式無須綁定任何特定的物件裸指標，而是產生一個型別擦除的純權杖。
   *
   * 適用情境：
   * 1. 樹狀結構（如 TreeNodeBase）或容器群：整棵樹的所有節點均可共同持有此 Token，
   *    只要尚有任一節點存活於記憶體中，底層動態庫便絕不會被物理卸載。
   * 2. 背景工作任務（Worker Tasks）或會話物件（Sessions）：任何非同步閉包均可捕獲此 Token，
   *    確保背景邏輯執行期間代碼段始終安全有效。
   *
   * @return 若動態庫已有效載入傳回有效權杖；若動態庫未載入則傳回 nullptr
   */
  [[nodiscard]] std::shared_ptr<const void> create_lifetime_token() const noexcept;

  /**
   * @brief 查詢本次 load() 取得的實例是否為動態庫於進程中的首次載入 (0 -> 1)
   *
   * 若傳回 true，代表動態庫為首次載入進程，呼叫端應在此時執行模組級全域啟始工作（Init）；
   * 若傳回 false，代表此動態庫在當前進程先前已由其他模組載入並存活中（共用相同控制區塊），
   * 應避免重複執行全域初始化以防止狀態衝突。
   */
  [[nodiscard]] bool is_first_loaded() const noexcept;

  /**
   * @brief 註冊在動態函式庫卸載（FreeLibrary / dlclose）前一刻執行的收尾回呼 (Pre-Unload Hook)
   *
   * 所有透過此函式註冊的收尾回呼，將嚴格保證在動態庫引用計數徹底歸零（1 -> 0）、
   * 且在動態庫代碼段解除映射之前，依反向順序 (LIFO) 執行。
   * 此時動態庫的實體程式碼段與 vtable 依然完整有效。
   *
   * @param hook 收尾回呼閉包
   */
  void add_cleanup_hook(std::function<void()> hook);

  /**
   * @brief 註冊在動態函式庫卸載（FreeLibrary / dlclose）完成後執行的通知回呼 (Post-Unload Hook)
   *
   * 與 add_cleanup_hook()（於 FreeLibrary 之前執行）不同，此回呼嚴格保證在動態庫引用計數徹底歸零、
   * 且底層作業系統實體動態庫已經完全物理卸載出記憶體之後執行。
   *
   * ⚠️ 注意事項：
   * 回呼執行時，動態庫代碼段已被作業系統解除映射（Unmap），因此傳入的 hook 閉包內部
   * 絕不可呼叫已卸載動態庫的任何函式或存取其虛擬函式表，通常用於通知宿主「插件資源已完全釋放/可更新狀態」。
   *
   * @param hook 卸載完成通知回呼閉包
   */
  void add_post_unload_hook(std::function<void()> hook);

  /**
   * @brief 啟用或停用非同步離棧延遲卸載模式 (Deferred Stack-Decoupled Unload)
   *
   * 當由受管物件或生命週期權杖（Lifetime Token）的解構觸發動態庫最後一次引用歸零時，
   * 呼叫棧頂層可能仍殘留有動態庫內部的解構子代碼（例如外掛節點自己的虛擬解構函式）。
   * 若直接在當前執行緒同步調用 FreeLibrary，會引發在自身呼叫棧中解除映射代碼段的崩潰 (Self-Unload Stack Trap)。
   *
   * 啟用非同步離棧卸載後，當最後一個引用歸零時，動態庫卸載動作將自動移交至獨立的背景執行緒執行，
   * 確保當前物件的解構呼叫棧完全退出後才執行物理卸載，達成 100% 絕對安全的自毀與卸載。
   *
   * @param enable 是否啟用非同步離棧卸載（預設為 true）
   */
  void enable_deferred_unload(bool enable = true) noexcept;

  /**
   * @brief 查詢當前是否啟用了非同步離棧延遲卸載模式
   */
  [[nodiscard]] bool is_deferred_unload_enabled() const noexcept;

  /**
   * @brief 依據符號名稱自動註冊無參數收尾函式（void()）為卸載前回呼
   *
   * @param symbol_name 收尾函式符號名稱（例如 "ork_plugin_shutdown"）
   * @return 若成功找到符號並註冊傳回 true；若找不到符號或庫未載入傳回 false
   */
  bool register_shutdown_symbol(std::string_view symbol_name);

  /**
   * @brief 僅在首次載入（0 -> 1）時執行指定的符號初始化函式
   *
   * 若動態庫先前已被其他呼叫者載入（is_first_loaded() == false），則本函式會自動安全略過。
   *
   * @tparam FuncT 函式指標型別或函式簽章
   * @tparam Args 傳入初始化函式之參數型別
   * @param symbol_name 初始化符號名稱（例如 "ork_plugin_init"）
   * @param args 轉發給初始化函式的引數
   * @return 若成功觸發初始化傳回 true；若非首次載入或找不到符號傳回 false
   */
  template <typename FuncT, typename... Args>
  bool initialize_once(std::string_view symbol_name, Args &&...args)
  {
    if (!is_first_loaded())
    {
      return false;
    }

    auto fn = get_symbol<FuncT>(symbol_name);
    if (!fn)
    {
      return false;
    }

    fn(std::forward<Args>(args)...);
    return true;
  }

  /**
   * @brief 依據當前平台自動格式化動態庫檔名
   * @param base_name 基礎模組名稱（如 "my_plugin"）
   * @return Windows: "my_plugin.dll", Linux: "libmy_plugin.so", macOS: "libmy_plugin.dylib"
   */
  static std::filesystem::path format_filename(std::string_view base_name);

  using Weak = WeakDynamicLibrary;
  using weak_type = WeakDynamicLibrary;

  /**
   * @brief 取得對應於本動態庫的弱引用觀察者 (Weak Dynamic Library)
   *
   * 類似 std::weak_ptr，不計入強引用計數，不會阻止動態庫在其產生的物件全數銷毀後自動卸載。
   * 當主程式或外掛管理器為了達成自動卸載而透過 reset() 放棄 load() 回傳的初始強引用句柄後，
   * 仍可持有此 WeakDynamicLibrary 觀察者。只要先前產生的物件仍有存活，隨時可透過 lock()
   * 重新安全提升為有效的 DynamicLibrary 強引用（無需再次調用 OS LoadLibrary）以創建物件或解析符號。
   */
  [[nodiscard]] WeakDynamicLibrary to_weak() const noexcept;

private:
  friend class WeakDynamicLibrary;

  explicit DynamicLibrary(std::shared_ptr<LibraryControlBlock> control_block, std::string last_error = "", bool is_first_loaded = false) noexcept
      : m_control_block(std::move(control_block)), m_last_error(std::move(last_error)), m_is_first_loaded(is_first_loaded)
  {
  }

  std::shared_ptr<LibraryControlBlock> m_control_block;
  mutable std::string m_last_error;
  bool m_is_first_loaded{false};
};

/**
 * @brief 動態庫弱引用觀察者 (Weak Dynamic Library Handle)
 *
 * 【設計目的】
 * 提供類似 std::weak_ptr 對應 std::shared_ptr 的無所有權觀察與晉升機制。
 *
 * 核心解決情境：
 * 依照 OuroKore 生命週期反向錨定規範，主程式或工廠在建立物件並綁定動態庫後，
 * 必須放棄 load() 回傳的初始強引用（如呼叫 DynamicLibrary::reset() 或讓變數離開局部作用域），
 * 才能達成「當產生的所有物件全部解構時，底層動態庫安全自動卸載」。
 *
 * 然而一旦主程式呼叫 reset()，原本的 DynamicLibrary 變數即刻歸零失去關聯，日後若需再次使用便無法直接獲取。
 * 透過事先建立 WeakDynamicLibrary，主程式可保有不干涉卸載的旁觀與晉升能力：
 * 1. 狀態查詢：透過 expired() 查詢動態庫是否已被底層卸載，或透過 use_count() 查詢活體物件引用計數。
 * 2. 安全重獲 (Lock & Acquire)：若先前產生的物件仍有存活（DLL 尚未卸載），呼叫 lock() 可再次
 *    成功提升為有效的 DynamicLibrary 強引用，無須重新調用作業系統 LoadLibrary 即可再次取得符號或產生物件；
 *    若所有物件已銷毀且 DLL 已在底層卸載，lock() 則安全傳回無效實例（expired() == true）。
 */
class ORK_BASE_API WeakDynamicLibrary
{
public:
  WeakDynamicLibrary() noexcept;
  ~WeakDynamicLibrary() noexcept;

  WeakDynamicLibrary(const WeakDynamicLibrary &) noexcept;
  WeakDynamicLibrary &operator=(const WeakDynamicLibrary &) noexcept;
  WeakDynamicLibrary(WeakDynamicLibrary &&) noexcept;
  WeakDynamicLibrary &operator=(WeakDynamicLibrary &&) noexcept;

  /**
   * @brief 從強引用 DynamicLibrary 構造弱引用觀察者
   */
  WeakDynamicLibrary(const DynamicLibrary &lib) noexcept;
  WeakDynamicLibrary &operator=(const DynamicLibrary &lib) noexcept;

  /**
   * @brief 嘗試將弱引用提升為強引用 DynamicLibrary
   * @return 若動態庫仍然存活且未卸載，傳回有效的 DynamicLibrary；若已卸載或未載入則傳回無效實例
   */
  [[nodiscard]] DynamicLibrary lock() const noexcept;

  /**
   * @brief 檢查動態庫是否已經卸載或過期
   * @return 若動態庫已卸載或從未載入，傳回 true；若仍有活體物件或強引用持有中，傳回 false
   */
  [[nodiscard]] bool expired() const noexcept;

  /**
   * @brief 查詢當前動態庫的存活引用計數（所有持有該 DLL 之物件與 DynamicLibrary 強引用總數）
   */
  [[nodiscard]] size_t use_count() const noexcept;

  /**
   * @brief 產生一個對應於本動態庫的弱引用權杖 (Weak Lifetime Token)
   *
   * @return 若底層控制區塊仍存活傳回有效弱引用；若已過期則傳回空弱引用
   */
  [[nodiscard]] std::weak_ptr<const void> create_weak_lifetime_token() const noexcept;

  /**
   * @brief 重設弱引用為空狀態
   */
  void reset() noexcept;

  /**
   * @brief 布林運算子重載，等同於 !expired()
   */
  explicit operator bool() const noexcept { return !expired(); }

private:
  friend class DynamicLibrary;
  std::weak_ptr<LibraryControlBlock> m_control_block;
};

}  // namespace ork
