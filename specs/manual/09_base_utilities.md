# 09. 基礎工具庫指南 (Base Foundation & Utilities)

本章節介紹 OuroKore 基礎模組（`ourokore_base`）中提供的通用現代基礎設施與工具庫。這些工具零依賴上層核心邏輯（`ourokore_core`），遵循現代 ISO C++20 標準，具備高效能、跨平台與極致執行緒安全特性。

---

## 🧭 基礎工具庫總覽

`ourokore_base` 模組包含以下關鍵子系統：

```
+-----------------------------------------------------------------------------------+
|                           ourokore_base 基礎工具庫                                |
+-----------------------------------------+-----------------------------------------+
| 1. 動態模組載入器 (DynamicLibrary)       | 2. 現代編譯期雜湊模組 (Hash)            |
|    - 生命週期反向錨定 (Life-Bound)      |    - C++20 constexpr 編譯期計算         |
|    - 弱引用晉升 (WeakDynamicLibrary)    |    - FNV-1a (TypeID 唯一標準)           |
|    - 純存活權杖 (LifetimeToken)         |    - CRC32 / MurmurHash3 / HashCombine  |
|    - 後置卸載通知與非同步離棧延遲卸載   |    - 使用者自訂字面量 (_fnv64, _crc32)  |
+-----------------------------------------+-----------------------------------------+
| 3. 並行與多執行緒排程 (Concurrency)     | 4. 全域文字與字串標準 (UTF-8)            |
|    - 固定執行緒池 (FixedThreadPool)     |    - 跨平台零拷貝視圖轉換 (as_view)     |
|    - 動態彈性伸縮池 (DynamicThreadPool) |    - to_string / to_u8string            |
|    - 執行緒安全佇列 (ThreadSafeQueue)   |    - Windows Unicode W 邊界隔離         |
|    - 計數信號量與事件 (Semaphore/Event) |                                         |
+-----------------------------------------+-----------------------------------------+
| 5. 樹狀結構容器與文字 DSL (Tree & TreeIO) ── 詳見《08. 樹狀結構容器與文字 DSL 指南》|
+-----------------------------------------------------------------------------------+
```

---

## 🧩 1. 動態模組載入器 (DynamicLibrary & WeakDynamicLibrary)

* **標頭檔**：`<ourokore/base/DynamicLibrary.hpp>`
* **命名空間**：`ork`（相容於 `ork::base::DynamicLibrary` 別名）
* **目標情境**：動態擴充外掛（Plugin / Component）、熱載入邏輯模組、跨平台符號解析、跨模組生命週期自動安全管理。

---

### 1.1 核心設計哲學：生命週期反向錨定與禁絕手動卸載

傳統動態庫載入器通常提供顯式的 `unload()` 函式，但在多執行緒或複雜物件圖中，提前手動卸載動態庫是導致致命崩潰（Access Violation / SIGSEGV）的首要元兇——當外部執行緒或背景佇列仍在執行物件的虛擬函式時，其虛擬函式表（vtable）與程式碼段已被作業系統解除映射（Unmap），立即引發 UAF。

OuroKore 徹底顛覆手動卸載思維，確立以下三大鐵律：
1. **禁絕手動卸載 (No Manual Unload)**：`DynamicLibrary` 刻意不提供任何手動 `unload()` 介面。
2. **生命週期反向錨定 (Life-Bound Retention)**：應用端將動態庫「所產生的物件」與動態庫建立生命週期綁定（透過 `bind_lifecycle()`、工廠 Deleter 閉包或存活權杖）。**只有當由該動態庫產生的所有活體物件全部解構銷毀後，底層動態庫才會在引用計數歸零時自動且安全地由底層卸載（`FreeLibrary` / `dlclose`）**。
3. ⚠️ **關鍵約束：`load()` 回傳值之生命週期持有與放棄**：
   `DynamicLibrary::load()` 的回傳值本身「持有一份動態庫引用（use_count >= 1）」。
   **若呼叫端長存此回傳值變數（例如存為全域變數、類別長存成員，或未離開作用域/未呼叫 `reset()`），動態庫就永遠不會被卸載！**
   應用端若希望實現「物件全數銷毀後 DLL 自動卸載」，必須在完成物件建立與綁定後，主動呼叫 `lib.reset()` 或讓局部句柄離開作用域，將存活權杖全權移交給受管物件持有。

---

### 1.2 多重載入快取與首度載入單次初始化 (Cache & Initialization)

當進程內多個子系統或模組在不同時機請求載入同一個動態庫時，`DynamicLibrary` 內部會以標準化絕對路徑進行弱引用快取共享。

* **`is_first_loaded()`**：查詢當前實例是否為動態庫於進程中的**首次載入（引用計數 0 -> 1）**。
  * 若傳回 `true`：代表該庫剛被載入進程，呼叫端應執行模組級全域初始化。
  * 若傳回 `false`：代表此庫先前已由其他模組載入且仍在記憶體中存活（引用計數 1 -> 2），呼叫端應避免重複初始化以防止狀態衝突。
* **`initialize_once<FuncT>(symbol_name, args...)`**：便捷的單次初始化樣板函式。**僅在 `is_first_loaded() == true` 時呼叫指定符號函式**；若為重複載入則自動安全略過並回傳 `false`。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/physics_engine.dll");
if (lib.is_loaded()) {
    // 僅在首次載入進程時執行一次 PhysicsInit(gravity=9.8f)；若已被其他模組載入過則自動略過
    lib.initialize_once<void(float)>("PhysicsInit", 9.8f);
}
```

---

### 1.3 兩階段卸載回呼系統 (Two-Stage Unload Hooks)

為了在動態庫生命週期走向終結時進行安全清理與狀態通知，`DynamicLibrary` 提供了兩階段、不同時機的卸載掛鉤：

```
主程式執行緒 (Main Thread)        DynamicLibrary 背景等待執行緒         外掛 DLL (Plugin)
        │                                  │                                   │
  1. 釋放最後引用 (如 lib.reset())         │                                   │
        │ ── 觸發卸載 (非同步交棒) ───────> │                                   │
  2. 立即返回繼續主程式工作！              │ ── 調用非同步善後函式 ──────────> │ 3. 執行冗長善後...
     (主程式 0ms 延遲、完全零卡頓)          │    (附帶 on_ready_to_unload 回呼) │    - 快取與資料落盤
        │                                  │                                   │    - 釋放 GPU/緩衝區
        │                                  │ ── 背景阻塞等待握手通知 ────      │    - 關閉連線或背景執行緒
        │                                  │                            │      │
        │                                  │ <── 呼叫 on_ready_to_unload() ────│ 4. 善後徹底完畢！
        │                                  │     (握手喚醒背景線程)            │    (外掛不再執行任何代碼)
        │                                  │                                  
        │                                  │ 5. 收到確認，呼叫 FreeLibrary() 物理卸載 DLL
        │                                  │ 6. 觸發 post_unload_hooks 通知主程式
```

#### 1. 第一階段：卸載前收尾與非同步握手協定 (Pre-Unload & Async Handshake)
* **同步收尾：`add_cleanup_hook(std::function<void()> hook)`**：註冊在動態庫卸載前執行的同步收尾回呼（保證代碼段與 vtable 依然完整有效，LIFO 順序執行）。
* **同步符號：`register_shutdown_symbol(std::string_view symbol_name)`**：依據符號名稱自動註冊無參 `void()` 函式為收尾回呼。
* 🌟 **非同步握手收尾：`add_async_cleanup_hook(AsyncCleanupHook hook)`**：
  * **設計目的**：解決外掛 shutdown 冗長善後導致主程式卡頓問題。
  * **握手運作**：主程式觸發卸載後**立即返回繼續運行（0ms 延遲）**；`DynamicLibrary` 在背景等待執行緒中調用 hook，外掛在完成所有耗時工作後主動呼叫傳入的 `on_ready_to_unload()`。背景執行緒收到通知被喚醒後，才執行 `FreeLibrary` 物理卸載 DLL！
* 🌟 **純 C 非同步符號：`register_async_shutdown_symbol(std::string_view symbol_name)`**：
  * 支援跨語言 C ABI：外掛導出 `void PluginAsyncShutdown(void (*on_ready)(void*), void* user_data)`。
* **逾時保護：`set_async_shutdown_timeout(std::chrono::milliseconds timeout)`**：設定非同步善後最大等待逾時（預設 30 秒），防範外掛死鎖。

#### 2. 第二階段：卸載完成通知 (Post-Unload Hook)
* **`add_post_unload_hook(std::function<void()> hook)`**：註冊在動態庫完成作業系統物理卸載後執行的通知回呼。
  * **目的**：宿主被動接收「外掛已完全死透、資源已全數釋放」事件，無需輪詢。
  * **高壓警戒**：此時動態庫程式碼段已解除映射，回呼閉包內部**絕對嚴禁**存取動態庫中的任何指標或呼叫其函式！

```cpp
auto lib = ork::DynamicLibrary::load("plugins/render_system.dll");

// 1. 【同步模式】註冊卸載前收尾：在 FreeLibrary 前同步清理
lib.add_cleanup_hook([]() {
    std::cout << "[Pre-Unload] 正在清理外掛內部 GPU 緩衝區..." << std::endl;
});
lib.register_shutdown_symbol("RenderShutdown");

// 2. 🌟【非同步握手模式】註冊非同步善後（主程式 0ms 立即返回，外掛背景耗時善後完畢後握手卸載）
lib.add_async_cleanup_hook([](ork::DynamicLibrary::ReadyToUnloadCallback on_ready) {
    std::thread([on_ready = std::move(on_ready)]() {
        std::cout << "[外掛背景] 正在非同步落盤大型存檔與中斷網絡...
";
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 耗時善後
        std::cout << "[外掛背景] 善後全數完畢！通知 DynamicLibrary 可以 FreeLibrary 了。
";
        
        // 握手確認：外掛保證絕不再執行任何代碼，喚醒卸載等待線程
        on_ready();
    }).detach();
});

// 3. 註冊卸載後通知：僅更新宿主狀態，絕不碰觸外掛代碼
lib.add_post_unload_hook([]() {
    std::cout << "[Post-Unload] 渲染插件已完全從記憶體卸載！宿主切換為軟體渲染模式。" << std::endl;
});
```

---

### 1.4 非同步離棧延遲卸載防護 (Deferred Stack-Decoupled Unload)

#### 崩潰陷阱：自解構呼叫棧陷阱 (Self-Unload Stack Trap)
考慮以下極端但常見的場景：
外掛定義了一個類別 `class PluginNode`，其虛擬解構式 `virtual ~PluginNode()` 編譯在外掛 DLL 內部代碼段中。當應用端銷毀最後一個 `PluginNode` 實例時：
1. 呼叫 `PluginNode` 的虛擬解構函式（此時當前執行緒的 Call Stack 頂層正處於 DLL 內部代碼段）。
2. 在該解構函式內部或 Deleter 中，最後一個動態庫引用歸零，觸發同步呼叫 `FreeLibrary(hDll)`。
3. **作業系統立即將 DLL 代碼段從記憶體中抹除！**
4. 呼叫棧嘗試從虛擬解構函式返回至呼叫端——但返回位址所在的代碼段已經消失，瞬間引發不可挽回的 `0xC0000005: Access Violation` 崩潰！

#### 解決方案：`enable_deferred_unload(true)`
* **用法**：呼叫 `lib.enable_deferred_unload(true);` 啟用離棧延遲卸載保護。
* **機制**：當最後一個引用歸零時，動態庫卸載動作會自動移交給**獨立的背景執行緒**執行，確保當前物件的解構呼叫棧完全退出後才卸載代碼段，達成 100% 絕對安全的自毀與卸載。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/node_system.dll");

// 啟用非同步離棧卸載防護
lib.enable_deferred_unload(true);

// 即使最後一個節點在外掛自身的代碼段中觸發解構，呼叫棧也能全身而退！
```

---

### 1.5 純生命週期存活權杖 (Pure Lifetime Token)

在傳統模式中，我們透過 `bind_lifecycle(raw_ptr, deleter)` 綁定單一裸指標。但對於複雜的樹狀結構（如 `TreeNodeBase` 百萬節點群）、非同步工作任務（Worker Tasks）或會話物件（Sessions），沒有單一裸指標適合承擔整個 DLL 的生命週期。

* **`create_lifetime_token()`**：產生一個型別擦除的純權杖（`std::shared_ptr<const void>`）。
* **特性**：
  * 該 Token 內部持有一份動態庫存活引用。
  * 樹狀結構的所有節點或多個非同步閉包均可複製並持有此 Token。
  * 只要宇宙中尚有任一節點存活，DLL 代碼段便長存有效；最後一個節點解構使 Token 計數歸零時，自動觸發底層動態庫安全卸載。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/tree_module.dll");

// 產生純存活權杖
std::shared_ptr<const void> token = lib.create_lifetime_token();

// 宿主主動放棄強引用
lib.reset();

// 建立樹節點，所有節點共享持有此 Token
struct MyNode {
    std::string name;
    std::shared_ptr<const void> dll_token;
};

auto root = std::make_shared<MyNode>("Root", token);
auto child1 = std::make_shared<MyNode>("Child1", token);

// 即使 root 被釋放，只要 child1 仍存活，DLL 就絕不會被卸載！
root.reset();
assert(child1 != nullptr); // DLL 依然存活

// 當最後一個節點釋放，DLL 安全自動卸載
child1.reset();
```

---

### 1.6 弱引用觀察與晉升重獲 (WeakDynamicLibrary)

當主程式為了實現自動卸載而呼叫 `lib.reset()` 或讓局部變數離開作用域時，主程式原本的 `DynamicLibrary` 變數已歸零。如果日後主程式又需要使用該動態庫（例如再次解析符號、創建物件或檢查外掛存活狀態），該怎麼辦？

`WeakDynamicLibrary` 提供了類似 `std::weak_ptr` 的無所有權觀察與安全重獲機制：

```cpp
auto lib = ork::DynamicLibrary::load("plugins/ai_module.dll");

// 1. 取得弱引用觀察者（不增加強引用計數，不阻止自動卸載）
ork::WeakDynamicLibrary weak_lib = lib.to_weak();

// 2. 建立業務物件並綁定生命週期
auto entity = lib.bind_lifecycle(CreateRawAI(), &DestroyRawAI);

// 3. 宿主主動放棄強引用句柄
lib.reset();
assert(!lib.is_loaded()); // 宿主句柄為空

// 4. 此時 entity 依然存活，DLL 尚未卸載
assert(!weak_lib.expired());
assert(weak_lib.use_count() == 1); // entity 仍持有 1 份

// 5. 核心：日後主程式再次需要使用時，透過 lock() 零開銷安全晉升重獲強引用！
if (auto locked = weak_lib.lock()) {
    // 成功重獲有效 DynamicLibrary（無需調用作業系統 LoadLibrary，零 I/O）
    auto fn = locked.get_symbol<void(*)()>("GlobalAIStep");
    if (fn) fn();
}

// 6. 業務物件全數解構
entity.reset();

// 7. DLL 已物理卸載，弱引用安全過期
assert(weak_lib.expired());
auto failed_lock = weak_lib.lock();
assert(!failed_lock.is_loaded()); // 安全傳回無效實例，絕不崩潰
```

---

### 1.7 跨平台檔名格式化與載入旗標

* **`format_filename(base_name)`**：依據當前作業系統規範自動產生動態庫檔名：
  * Windows：`"my_plugin.dll"`
  * Linux：`"libmy_plugin.so"`
  * macOS：`"libmy_plugin.dylib"`
* **`LibraryLoadFlags` 載入旗標**：
  * `LibraryLoadFlags::ResolveNow`：立即解析所有符號（POSIX: `RTLD_NOW`，Windows 預設）。
  * `LibraryLoadFlags::ResolveLazy`：延遲按需解析符號（POSIX: `RTLD_LAZY`）。
  * `LibraryLoadFlags::ScopeLocal`：符號私有隔離，不外洩給其他模組（POSIX: `RTLD_LOCAL`，預設）。
  * `LibraryLoadFlags::ScopeGlobal`：符號全域可見（POSIX: `RTLD_GLOBAL`）。
  * `LibraryLoadFlags::SearchDllDir`：Windows 專用，優先搜尋 DLL 所在目錄與其相依項。
* **全域 UTF-8 路徑支援**：
  * `load("路徑/插件.dll")` 內部一律轉換為 Unicode UTF-16 呼叫 `LoadLibraryW`，徹底解決多語系與繁體中文路徑亂碼失敗問題。
  * `get_path_utf8()` 保證傳回 100% 規範化的 UTF-8 絕對路徑。


## ⚙️ 2. 現代編譯期雜湊模組 (Hash.hpp)

* **標頭檔**：`<ourokore/base/Hash.hpp>`
* **命名空間**：`ork::base`，字面量命名空間：`ork::base::literals`
* **目標情境**：型別識別碼（TypeID）、字串鍵雜湊比對、封包校驗、資料完整性驗證。

### 特性與演算法

1. **C++20 constexpr 編譯期零開銷**：
   - 演算法全面支援編譯期求值，零執行期開銷。
2. **演算法矩陣**：
   * **FNV-1a (64-bit / 32-bit)**：OuroKore 系統中 `TypeID` 與全域識別碼的唯一標準演算法，計算速度極快、分佈優良。
   * **CRC32 (IEEE 802.3)**：標準循環冗餘校驗，廣泛用於二進位串流、藍圖存檔與通訊封包防竄改檢查。
   * **MurmurHash3 (32-bit)**：高品質通用雜湊演算法，具備極強的雪崩效應（Avalanche Effect），適合哈希表尋址。
   * **HashCombine**：變參雜湊組合函式，適用於多欄位複合鍵雜湊。

### 範例程式碼

```cpp
#include <ourokore/base/Hash.hpp>
#include <iostream>
#include <cassert>

using namespace ork::base::literals;

void HashDemo() {
    // 1. 編譯期常數計算（使用字面量運算子）
    constexpr uint64_t type_id = "PlayerCharacter"_fnv64;
    constexpr uint32_t type_id32 = "PlayerCharacter"_fnv32;
    constexpr uint32_t crc = "BLUEPRINT_HEADER"_crc32;

    // 2. 執行期字串與記憶體區塊雜湊
    std::string player_name = "Arthur";
    uint64_t name_hash = ork::base::Fnv1a64(player_name);
    assert(name_hash == "Arthur"_fnv64);

    // 3. CRC32 資料完整性校驗
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t checksum = ork::base::Crc32(payload);

    // 4. MurmurHash3 (32-bit) 帶種子雜湊
    uint32_t seed = 0x9747b28c;
    uint32_t murmur = ork::base::MurmurHash3("SampleKey", seed);

    // 5. 複合屬性鍵組合 (HashCombine)
    size_t combined = 0;
    ork::base::HashCombine(combined, type_id, name_hash, checksum);
    std::cout << "複合雜湊值: " << combined << std::endl;
}
```

---

## ⚡ 3. 並行與多執行緒排程 (ThreadPool, Queue, Semaphore, Event)

* **標頭檔**：
  * `<ourokore/base/ThreadPool.hpp>`
  * `<ourokore/base/ThreadSafeQueue.hpp>`
  * `<ourokore/base/Semaphore.hpp>`
* **命名空間**：`ork::base`

### 3.1 固定數量執行緒池：`FixedThreadPool`

建立恆定數量的 Worker 執行緒，適用於 CPU 密集型運算或任務數量穩定之場景。

* **核心特性**：
  * 支援 `submit`：提交任務並回傳 `std::future<ReturnType>`，支援任意函式與參數完美轉發。
  * 支援 `submit_detached`：Fire-and-Forget 任務排程，避免 `packaged_task` 內部堆積配置。
  * `wait_idle()`：阻塞等待直到所有排隊任務與執行中任務全數完成。
  * **Worker 防自我死鎖**：Worker 執行緒內部調用 `wait_idle()` 時自動安全略過，防止自我等待死鎖。
  * RAII 優雅關閉：解構時自動呼叫 `stop()` 並等待所有已排隊任務處理完畢。

```cpp
#include <ourokore/base/ThreadPool.hpp>
#include <iostream>

void TestFixedPool() {
    // 建立 4 個 Worker 的固定執行緒池（傳入 0 則預設為 CPU 核心數）
    ork::base::FixedThreadPool pool(4);

    // 1. 提交有回傳值的任務 (Future)
    std::future<int> result = pool.submit([](int a, int b) {
        return a + b;
    }, 10, 20);

    std::cout << "計算結果: " << result.get() << std::endl; // 輸出 30

    // 2. 提交 Fire-and-Forget 輕量任務
    pool.submit_detached([]() {
        std::cout << "背景日誌處理完成" << std::endl;
    });

    // 3. 等待所有任務執行完畢
    pool.wait_idle();
}
```

### 3.2 彈性動態伸縮執行緒池：`DynamicThreadPool`

依據即時任務負載量自動增減 Worker 執行緒，兼顧尖峰並發能力與離峰資源節能。

* **動態擴展**：當排隊任務數超過目前空閒 Worker 且未達 `max_threads` 時，即刻動態生成新 Worker。
* **空閒縮容回收**：Worker 空閒等待超過指定逾時時間（`idle_timeout`，預設 3000ms）時，自動終止並回收執行緒，直至保留核心常駐數量（`min_threads`）。

```cpp
#include <ourokore/base/ThreadPool.hpp>

void TestDynamicPool() {
    // min_threads=2, max_threads=8, idle_timeout=2000ms
    ork::base::DynamicThreadPool dynamic_pool(2, 8, std::chrono::milliseconds(2000));

    for (int i = 0; i < 20; ++i) {
        dynamic_pool.submit_detached([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }

    // 負載突增時，Worker 數量動態擴增至最高 8 個
    std::cout << "當前 Worker 數: " << dynamic_pool.get_current_worker_count() << std::endl;

    dynamic_pool.wait_idle();
    // 待任務清空且逾時 2 秒後，Worker 數量自動縮容回核心 2 個
}
```

### 3.3 執行緒安全阻塞佇列：`ThreadSafeQueue<T>`

多生產者-多消費者（MPMC）阻塞佇列，具備逾時取出與優雅關閉喚醒。

* `bool push(T item)` / `template <typename... Args> bool emplace(Args&&... args)`：推入元素，成功回傳 true；若佇列已停止則回傳 false。
* `bool pop(T &out_val)`：阻塞等待取出隊首元素。若佇列已停止且為空則傳回 false。
* `bool pop_for(T &out_val, rel_time)`：在指定逾時內等待取出隊首元素。
* `bool try_pop(T &out_val)`：非阻塞立即取出。
* `void stop()`：停止佇列並喚醒所有等待中的執行緒。
* `void clear()`：清空佇列，**於鎖外安全析構殘留元素**，避免持有鎖時解構複雜物件引發死鎖。

```cpp
#include <ourokore/base/ThreadSafeQueue.hpp>

ork::base::ThreadSafeQueue<std::string> message_queue;

// 生產者執行緒
message_queue.push("Task #1");

// 消費者執行緒
std::string msg;
if (message_queue.pop_for(msg, std::chrono::milliseconds(500))) {
    std::cout << "收到訊息: " << msg << std::endl;
}
```

### 3.4 計數信號量：`Semaphore`

跨平台計數信號量，支援阻塞獲取、逾時獲取與批次釋放。

* `acquire()`：阻塞等待可用資源計數大於 0 並遞減。
* `try_acquire()`：非阻塞嘗試獲取。
* `try_acquire_for(rel_time)`：逾時等待獲取。
* `release(ptrdiff_t update = 1)`：釋放並增加資源計數。
* `available()`：查詢目前可用資源快照。

```cpp
#include <ourokore/base/Semaphore.hpp>

ork::base::Semaphore sem(0); // 初始可用計數為 0

std::thread worker([&sem]() {
    sem.acquire(); // 阻塞等待信號
    std::cout << "Worker 開始執行" << std::endl;
});

// 主執行緒通知 Worker
sem.release();
worker.join();
```

### 3.5 事件通知同步原語：`Event`

跨平台事件通知原語，支援自動重設（`AutoReset`）與手動廣播（`ManualReset`）。

* **`EventResetMode::AutoReset`**：單一等待執行緒被喚醒後，自動重設為未觸發狀態（類似 Windows Auto-Reset Event）。
* **`EventResetMode::ManualReset`**：所有等待執行緒均被喚醒，需顯式呼叫 `reset()` 才會回到未觸發狀態（廣播模式）。
* 方法：`set()`, `reset()`, `wait()`, `wait_for(rel_time)`, `is_set()`。

```cpp
#include <ourokore/base/Semaphore.hpp>

// 建立手動重設廣播事件
ork::base::Event ready_event(ork::base::EventResetMode::ManualReset, false);

// 多個 Worker 執行緒等待初始化完成
// worker: ready_event.wait();

// 主執行緒廣播完成通知
ready_event.set();
```

---

## 🌐 4. 全域 UTF-8 零拷貝文字轉換輔助 (utf8.hpp)

* **標頭檔**：`<ourokore/base/utf8.hpp>`
* **命名空間**：`ork::utf8`

依據 OuroKore 全域字串規範（UTF-8 Standard Invariant），系統內部一律使用 UTF-8 編碼。`utf8.hpp` 提供方便的零拷貝轉換與概念萃取：

* `ork::utf8::as_view(str)`：將 `std::string`、`std::u8string`、`std::string_view`、`std::u8string_view`、`const char*`、`const char8_t*` 零拷貝轉為 `std::string_view`。
* `ork::utf8::to_string(str)`：統一轉換為標準 `std::string`。
* `ork::utf8::to_u8string(view)`：轉換為 C++20 原生 `std::u8string`。
* `ork::utf8::is_string_like_v<T>`：編譯期萃取，判斷是否為類字串型別。

---

## 🌳 5. 現代樹狀結構容器與文字 DSL 串流 (Tree & TreeIO)

適用於階層資料、設定檔、屬性樹、文字 DSL 串流存取與遊戲腳本配置。
詳細深入指南請參閱專章：
👉 **[08. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)](08_tree_and_dsl.md)**
