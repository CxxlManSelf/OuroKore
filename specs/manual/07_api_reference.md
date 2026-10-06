# 07. 公開 C++ API 參照手冊 (API Reference)

本手冊彙整 OuroKore 面向應用開發者與宿主主程式之所有公開核心類別與全域介面。

---

## 🏛️ 1. 宿主專屬類別：`ork::HostContext`
* **標頭檔**：`ourokore/host/HostContext.hpp`
* **方法**：
  * `bool IsValid() const noexcept`：檢查是否具備合法宿主主控權。
  * `void Shutdown()`：優雅終止核心背景任務與執行緒池。
  * `void FlushStorage()`：同步排空並等待所有藍圖磁碟寫入與銷毀落盤。
  * `void FlushDeferredDeletions()`：同步排空延遲物理銷毀隊列。
  * `void CollectCycles()`：同步觸發一輪循環參照檢測與孤島解開。
  * `void SetDeferredDeleteMode(bool sync)`：設定非同步延遲銷毀或即時同步模式。
  * `void SetAutoDehydrator(std::shared_ptr<IAutoDehydrator>)`：設定全域自動脫水模組。
  * `std::shared_ptr<IAutoDehydrator> GetAutoDehydrator() const`：取得當前自動脫水模組。
  * `void SetStorageDriver(std::shared_ptr<IStorageDriver>)`：設定儲存驅動。
  * `std::shared_ptr<IStorageDriver> GetStorageDriver() const`：取得當前儲存驅動。
  * `size_t TriggerDehydrationRescue(size_t bytes_needed)`：緊急脫水指定位元組數。
  * `void SetObjectModuleLoader(HandleID id, const DynamicLibrary &loader)`：綁定動態庫載入器至受管物件控制區塊（脫水長存，Payload 銷毀即刻解錨）。
  * `DynamicLibrary GetObjectModuleLoader(HandleID id) const`：取得物件綁定之動態庫載入器。
  * `std::shared_ptr<IObjectModuleBinder> GetModuleBinder() const`：取得專職模組綁定介面（最小特權原則，委派給外掛工廠）。

---

## 📦 2. 領域物件基底與 CRTP 樣板：`ork::OuroObject` / `ork::Subclass`
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **類別基底 `ork::OuroObject`**：
  * 所有託管物件之抽象基類，嚴禁外部 `new` 或值拷貝。
  * `HandleID GetObjectID() const`：取得物件之全域唯一識別碼。
  * `StorageState GetStorageState() const`：取得物件當前儲存狀態（Clean/Dirty/Dehydrated/UnsavedNew）。
  * `ork_type_id_t GetTypeID() const`：取得物件當前之 64 位元 TypeID（支援多型與繼承階層查詢）。
  * `virtual void SerializePayload(OuroStream &stream) const`：純資料屬性序列化介面。
  * `virtual void DeserializePayload(OuroStream &stream)`：純資料屬性反序列化介面。
* **樣板基底 `ork::Subclass<Derived, Base = ork::OuroObject>`**：
  * **所有領域物件強制繼承之 CRTP 基底**（免巨集自動型別系統）。
  * `static constexpr const char* StaticTypeName()`：自動在編譯期萃取類別名稱。
  * `static TypeID StaticTypeID()`：自動以 FNV-1a 計算並向核心註冊繼承關係樹。
  * 支援帶參數建構子完美轉發：`Subclass(args...)` 直接初始化父類別。

---

## 🔗 3. 智慧 Handle 系統
* **標頭檔**：`ourokore/component/Handles.hpp`
* **類別**：
  * `OwningHandle<T>`：強持有槽位，宣告為物件成員。方法：`Set()`, `Get()`, `Release()`, `GetTargetID()`。
  * `OwningContainerHandle`：動態強持有容器，方法：`AddTarget()`, `RemoveTarget()`, `GetTargetIDs()`。
  * `UnboundHandle<T>`：無繫結非擁有型引用，方法：`LockAndAcquire()`, `GetTargetID()`, `IsAlive()`, `Release()`。
  * `OuroPtr<T>`：棧上活躍根指標守衛，支援 `operator()(Fn&&, Args&&...)`, `Invoke(...)`, `operator bool()`, `IsAlive()`, `GetTargetID()`, `Release()`。
    * `template <typename U> bool Is() const`：判定物件是否屬於或繼承自型別 `U`（純記憶體查詢，脫水狀態零 I/O 保證）。
    * `template <typename U> OuroPtr<U> As() const &`：向下/向上安全轉型（左值增持根引用）。
    * `template <typename U> OuroPtr<U> As() &&`：右值移動轉型（**零引用計數開銷**轉移所有權）。
    * `ork_type_id_t GetTypeID() const`：取得目標物件 TypeID。
    * `dynamic_pointer_cast<U>(ptr)` / `static_pointer_cast<U>(ptr)`：STL 風格轉型支援。

---

## 🔒 4. 併發同步守衛
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **類別**：
  * `OuroReadLock`：共享讀鎖 RAII 守衛。
  * `OuroWriteLock`：獨占寫鎖 RAII 守衛，**解構時自動原子標記 Dirty**。

---

## 🏭 5. 物件工廠與持久化介面
* **標頭檔**：`ourokore/component/OuroCore.hpp`
* **函式**：
  * `CreateObject<T>(args...)`：建立受管領域物件（自動通報脫水模組登記）。
  * `CreatePermanentObject<T>(args...)`：建立永久常駐物件（不參與脫水換頁）。
  * `Save(OuroPtr<T>)` / `Load(OuroPtr<T>)`：同步存檔與自磁碟載入刷新。
  * `SaveAsync(OuroPtr<T>)` / `LoadAsync(OuroPtr<T>)`：非同步背景存檔與載入。
  * `Dehydrate(HandleID id)`：依 ID 脫水（若 root_count > 0 則安全略過傳回 false）。
  * `Dehydrate(OuroPtr<T> &&ptr)`：右值移動消耗脫水（清空原指標，防止懸空）。
  * `DehydrateAsync(HandleID id)` / `DehydrateAsync(OuroPtr<T> &&ptr)`：非同步背景脫水。
  * `Rehydrate<T>(HandleID id)` / `Rehydrate<T>(const OuroPtr<T> &ptr)`：顯式手動復水，回傳全新 `OuroPtr<T>`。
  * `RehydrateAsync<T>(HandleID id)` / `RehydrateAsync<T>(const OuroPtr<T> &ptr)`：非同步背景顯式復水。
  * `IsAlive(HandleID id)`：查詢物件是否存活（純 ControlBlock 查詢，零 I/O 保證）。
  * `GetStorageState(HandleID id)`：查詢物件當前 StorageState（純 ControlBlock 查詢，零 I/O 保證）。
  * `GetRootEdgeCount(HandleID id)`：查詢目標當前活躍根邊緣數量。
  * `SaveBatch(...)` / `LoadBatch(...)`：多核心平行批次操作。

---

## 🧩 6. 跨平台動態庫與插件載入器：`ork::DynamicLibrary`
* **標頭檔**：`ourokore/base/DynamicLibrary.hpp`
* **設計哲學與卸載核心原則**：
  * **禁絕手動卸載 (No Manual Unload)**：載入器不提供手動 `unload()` 介面，杜絕因提前手動卸載導致正在執行的物件虛擬函式表 (vtable) 與代碼段失效崩潰。
  * **生命週期反向錨定與自動卸載 (Life-Bound Retention & Auto Unload)**：設計期望應用端將動態庫「所產生的物件」與動態庫建立生命週期綁定（透過 `bind_lifecycle()` 或在工廠 Deleter 閉包中捕捉 `DynamicLibrary` 實例）。當該動態庫產生的所有物件全部解構銷毀後，底層動態庫才會在引用計數歸零時自動且安全地卸載（`FreeLibrary` / `dlclose`）。
  * ⚠️ **關鍵約束注意（load 回傳值之生命週期綁定）**：`ork::DynamicLibrary::load()` 的回傳值本身「已經將其綁定（持有一份引用計數）」。如果不放棄該回傳值（例如長存於全域或成員變數、或外層未離開作用域/未重設），動態庫是絕對不會被卸載的！因此，若希望依賴產生物件全部解構後自動卸載 DLL，呼叫端在完成物件構造與綁定後，必須主動放棄/釋放 `load()` 傳回的初始句柄（例如讓其隨工廠作用域自然解構，或呼叫 `reset()` 放棄持有）。
* **方法**：
  * `static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = Default)`：自 UTF-8 路徑載入動態庫（Windows 內部使用 Unicode `LoadLibraryW`，杜絕本地 ANSI/CP950 亂碼）。回傳之句柄已持有動態庫引用。
  * `static DynamicLibrary load(const std::filesystem::path &path, ...)`：自檔案路徑載入動態庫。
  * `void reset() noexcept`：放棄當前持有的動態庫句柄（扣減引用計數），使存活權杖全權移交給綁定物件。
  * `bool is_loaded() const noexcept`：查詢動態庫是否載入成功。
  * `const std::string &get_last_error() const noexcept`：取得 UTF-8 格式的系統錯誤訊息。
  * `std::string get_path_utf8() const noexcept`：取得載入函式庫之 UTF-8 規範路徑。
  * `size_t use_count() const noexcept`：取得當前動態庫的存活引用計數（含句柄變數與綁定物件）。
  * `template <typename FuncT> auto get_symbol(std::string_view name) const noexcept`：解析動態庫導出符號並智慧推導函式指標型別。
  * `template <typename T, typename DeleterT> std::shared_ptr<T> bind_lifecycle(T *raw_ptr, DeleterT deleter)`：將自訂裸指標與動態庫存活權杖綁定，確保指標銷毀前動態庫永不卸載。
  * `std::shared_ptr<const void> create_lifetime_token() const noexcept`：產生純生命週期存活權杖（Pure Lifetime Token），無須綁定單一裸指標，任何容器、樹狀結構（如整棵樹的所有節點）或非同步任務均可共享持有，只要任一節點存活即保證 DLL 絕對不被物理卸載。
  * `static std::filesystem::path format_filename(std::string_view base_name)`：依作業系統格式化動態庫檔名（Windows `.dll`、Linux `.so`、macOS `.dylib`）。
  * `bool is_first_loaded() const noexcept`：查詢本次 `load()` 取得的實例是否為動態庫於進程中的首次載入（0 -> 1）。若為 false 代表先前已由其他模組載入並存活中。
  * `void add_cleanup_hook(std::function<void()> hook)`：註冊在動態函式庫卸載前執行的通用前置收尾回呼（Pre-Unload Hook）。在代碼段解除映射前依反向順序 (LIFO) 執行。
  * `void add_shutdown_hook(std::function<void()> hook)`：註冊模組最終同步收尾回呼。享有【模組收尾最後執行保證 (Terminal Shutdown Guarantee)】，不論註冊時機為何，必定在所有通用清理回呼執行完畢後才最後被觸發。
  * `void add_async_shutdown_hook(AsyncCleanupHook hook)`：註冊模組最終非同步善後收尾回呼。享有最後執行保證，於所有通用清理後觸發並等待握手完成。
  * `void add_post_unload_hook(std::function<void()> hook)`：註冊在動態函式庫物理卸載（FreeLibrary / dlclose）完成後執行的通知回呼（Post-Unload Hook）。嚴格保證在代碼段已完全解除映射後觸發，通知宿主資源已全數釋放完畢。
  * `void enable_deferred_unload(bool enable = true) noexcept`：啟用非同步離棧延遲卸載模式。當由受管物件或生命週期權杖解構觸發最後引用歸零時，卸載動作自動移交獨立背景執行緒執行，確保當前物件解構棧幀安全退出後再卸載代碼段，徹底杜絕呼叫棧自毀崩潰 (Self-Unload Stack Trap)。
  * `bool is_deferred_unload_enabled() const noexcept`：查詢當前是否啟用了非同步離棧延遲卸載模式。
  * `bool register_shutdown_symbol(std::string_view symbol_name)`：依據符號名稱自動解析無參數收尾函式（`void()`）並註冊為模組終端收尾回呼（享有 Terminal Shutdown Guarantee）。
  * `bool register_async_shutdown_symbol(std::string_view symbol_name)`：依據符號名稱自動解析純 C 簽章之非同步收尾函式並註冊（享有 Terminal Shutdown Guarantee）。
  * `template <typename FuncT, typename... Args> bool initialize_once(std::string_view symbol_name, Args &&...args)`：僅在首次載入（0 -> 1）時執行指定的符號初始化函式，重複載入時自動安全略過。
  * `WeakDynamicLibrary to_weak() const noexcept`：建立並取得該動態庫之弱引用觀察者（不增加強引用計數，不阻止自動卸載）。

---

## 👁️ 6.1 動態庫弱引用觀察者：`ork::WeakDynamicLibrary`
* **標頭檔**：`ourokore/base/DynamicLibrary.hpp`
* **設計目的**：提供類似 `std::weak_ptr` 的無所有權觀察與晉升機制。當主程式為配合自動卸載而呼叫 `DynamicLibrary::reset()` 放棄初始強引用後，若日後需要再次建立物件或監控模組狀態，可透過本類別之 `lock()` 安全晉升重獲強引用（無須重新 LoadLibrary）；若所有受管物件均已解構且 DLL 已卸載，`lock()` 則安全傳回無效實例。
* **方法**：
  * `WeakDynamicLibrary(const DynamicLibrary &lib) noexcept`：從強引用 DynamicLibrary 構造弱引用觀察者。
  * `DynamicLibrary lock() const noexcept`：嘗試將弱引用晉升為強引用。若動態庫仍存活傳回有效實例；若已卸載則傳回無效實例。
  * `bool expired() const noexcept`：查詢動態庫是否已經卸載或過期。
  * `size_t use_count() const noexcept`：查詢當前存活之強引用計數（所有綁定活體物件與強引用總數）。
  * `std::weak_ptr<const void> create_weak_lifetime_token() const noexcept`：產生對應於本動態庫的弱引用權杖。
  * `void reset() noexcept`：重設弱引用為空狀態。
  * `explicit operator bool() const noexcept`：等同於 `!expired()`。

---

## 🌐 7. 全域 UTF-8 零拷貝輔助工具：`ork::utf8`
* **標頭檔**：`ourokore/base/utf8.hpp`
* **函式與工具**：
  * `ork::utf8::as_view(str)`：將 `std::string`、`std::u8string`、`std::string_view`、`std::u8string_view`、`const char*`、`const char8_t*` 零拷貝轉為 `std::string_view`。
  * `ork::utf8::to_string(str)`：將各類字串統一轉為 `std::string`。
  * `ork::utf8::to_u8string(view)`：將字串視圖轉為 C++20 原生 `std::u8string`。
  * `ork::utf8::is_string_like_v<T>`：編譯期型別特徵萃取，判斷是否為字串相關型別。

---

## ⚙️ 8. 現代高效能雜湊工具模組：`ork::base::Hash`
* **標頭檔**：`ourokore/base/Hash.hpp`
* **設計哲學**：相容 C++20 `constexpr` 編譯期常數計算、現代雜湊演算法、字面量運算子支援。
* **演算法與函式**：
  * `ork::base::Fnv1a64(data)`：FNV-1a 64-bit 雜湊演算法（全域 TypeID 與字串 ID 唯一標準）。
  * `ork::base::Fnv1a32(data)`：FNV-1a 32-bit 雜湊演算法。
  * `ork::base::Crc32(data)`：CRC32 (IEEE 802.3) 校驗碼（資料完整性與防竄改驗證）。
  * `ork::base::MurmurHash3(data, seed)`：MurmurHash3 32-bit 高品質雜湊演算法。
  * `ork::base::HashCombine(seed, v1, v2, ...)`：Boost / Container 標準變參組合雜湊。
  * 使用者自訂字面量（`using namespace ork::base::literals;`）：
    * `""_fnv64`：編譯期直接計算為 64 位元常數整數。
    * `""_fnv32`：編譯期直接計算為 32 位元常數整數。
    * `""_crc32`：編譯期直接計算為 CRC32 常數校驗碼。

---

## 🌳 9. 樹狀結構節點與文字 DSL 串流：`ork::base::TreeNode<T>` / `ork::base::TreeIO`
* **標頭檔**：`ourokore/base/Tree.hpp`、`ourokore/base/TreeIO.hpp`
* **設計哲學**：
  * **單一容器雙模態統合（Unified Dual-Mode）**：全體子節點統一由連續記憶體 `std::vector` 儲存（享有 CPU 快取極速預讀），具名字節點由 `std::unordered_map` 提供 $O(1)$ 雜湊尋址。**下標與名稱存取 100% 互通**，存取到的為同一節點實體。
  * **形態由資料自動推導（Data-Driven Morphism）**：依據子節點結構純度自動判定——全具名者自動判定為物件模式（大括號 `{}`），混入匿名元素者自動判定為陣列模式（小括號 `()`）。
  * **CRTP 自定義衍生節點擴充（Extensible CRTP Hierarchy）**：支援繼承 `TreeNodeBase<Derived>` 定義強型別領域節點，序列化與反序列化自適應萃取衍生型別，零成本零強制轉型。
  * **極致執行緒安全**：結構拓撲鎖（`m_mutex`）與資料 Payload 鎖（`m_dataMutex`）獨立讀寫分離，高頻資料更新不阻礙樹結構遍歷。
  * **防遞迴析構爆棧**：內建顯式堆疊迭代析構，巨型深樹解構時由堆積迴圈安全釋放，徹底杜絕遞迴析構引發呼叫堆疊溢位（Stack Overflow）與行程退出 UAF。
  * **非遞迴顯式堆疊反序列化**：反序列化全程採用 Heap 顯式堆疊非遞迴狀態機，Call Stack 深度恆為 $O(1)$，巨深巢狀文字 DSL 免疫 Stack Overflow。
  * **正交界定符與無等號緊湊支援**：四大正交界定符 `[名稱]`、`"資料"`、`{物件}`、`(陣列)`，等號 `=` 為純無視裝飾符號。支援極致緊湊無等號模式（`WithoutEqual`），連續具名空節點、匿名空元素、物件陣列 `( { [a]="1" } )` 100% 精確對稱還原，單元素容器拓撲絕不脫殼降級。
* **核心類別與方法**：
  * **樣板基底 `TreeNodeBase<Derived>`**：
    * `CreateRoot(name)` / `CreateArray(name)`：建立樹之根節點。
    * `bool IsObject() / bool IsArray()`：純資料內容驅動判定（`m_elements.size() > m_nameMap.size()` 為陣列）。
    * `NodePtr PushElement()` / `bool PushElement(element)`：向尾端追加匿名元素。
    * `size_t ElementCount()` / `size_t ChildCount()` / `size_t Size()`：取得子節點總數（$O(1)$）。
    * `NodePtr GetElementAt(index)` / `operator[](size_t index)`：隨機下標存取元素（$O(1)$）。
    * `NodePtr FindChildByName(name)` / `operator[](const std::u8string &name)`：按名稱尋找子節點（$O(1)$）。
    * `bool HasChild(name)`：查詢子節點存在性。
    * `NodePtr AddChild(name)`（相容別名 `AddBackChild`）：新增具名或匿名子節點（$O(1)$）。
    * `NodePtr InsertBefore(child, name)` / `NodePtr InsertAfter(child, name)`：指定位置插入子節點。
    * `bool RemoveElementAt(index)` / `bool RemoveChild(child)` / `bool RemoveChildByName(name)`：移除子節點。
    * `void ClearChildren()` / `ClearElements()`：清空所有子項目。
    * `auto begin() / end()` / `rbegin() / rend()` / `Reversed()` / `GetTreeMutex()`：支援配合樹級讀寫鎖進行標準 STL 迭代器與 range-for 安全走訪（支援 `for (auto &c : node->Reversed())` 零成本反向視圖）。⚠️ **關鍵防禦鐵律**：走訪期間僅供純資料使用（`GetData` / `GetName`），**絕對禁止在此期間執行節點拓撲修改（如 `AddChild` / `RemoveChild`）**，否則會因非遞迴讀寫鎖引發重複加鎖死鎖（Deadlock）！
    * `DetachFromParent()`：安全斷開與父節點之雙向弱關聯並自立為新樹（配發專屬獨立鎖）。
  * **具體節點 `TreeNode<T>`（`StringTreeNode` 預設 `T = std::string`）**：
    * `T GetData()` / `void SetData(const T &)` / `void SetData(T &&)`：安全存取節點資料（受資料讀寫鎖保護）。
  * **文字 DSL 串流工具 `TreeIO`**：
    * `CompactMode` 列舉：`None`（標準美化縮排換行）、`WithEqual`（保留等號緊湊 `="`）、`WithoutEqual`（不保留等號極致緊湊 `"`）。
    * `static void Serialize<Node = StringTreeNode>(ostream, root, data_to_string, indent_width, mode)`：輸出文字 DSL 至串流，支援應用端自訂 CRTP 衍生節點與 3 種緊湊模式。
    * `static void SerializeCompact<Node = StringTreeNode>(ostream, root, mode)`：緊湊序列化便捷函式。
    * `static std::string SerializeToString<Node = StringTreeNode>(root, ...)`：直接輸出文字 DSL 字串（支援 CompactMode 列舉、布林緊湊旗標或自訂縮排與 data_to_string 轉發）。
    * `static std::shared_ptr<NodeType> Deserialize<NodeType = StringTreeNode>(istream, data_handler)`：寬容型狀態機自輸入串流反序列化（支援 CRTP 節點替換與資料型別自適應，精準回傳應用端節點智慧指針；handler 支援值轉換或 `(node, str) -> void` 節點現地賦值）。
    * `static std::shared_ptr<NodeType> DeserializeFromString<NodeType = StringTreeNode>(string_view, data_handler)`：自文字字串反序列化（支援 CRTP 節點替換與資料型別自適應，精準回傳應用端節點智慧指針）。
