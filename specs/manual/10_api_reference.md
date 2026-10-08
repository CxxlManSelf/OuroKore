# 10. 公開 C++ API 參照手冊 (API Reference)

本手冊彙整 OuroKore 面向應用開發者、外掛實作者與宿主主程式之所有公開核心類別、工具與介面，作為全套手冊之終端速查字典附錄。

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
* **設計哲學與卸載原則**：
  * **禁絕手動卸載 (No Manual Unload)**：動態庫不提供手動 `unload()`，杜絕野指針與 vtable 懸空崩溃。
  * **生命週期反向錨定 (Life-Bound Retention)**：物件全數銷毀後自動安全卸載。呼叫端若長存 `load()` 回傳之初始句柄則永遠不卸載，完成綁定後應主動 `reset()` 或移交權杖。
* **方法**：
  * `static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = Default)`：自 UTF-8 路徑載入動態庫。
  * `void reset() noexcept`：放棄句柄持有（扣減引用計數）。
  * `bool is_loaded() const noexcept`：查詢是否載入成功。
  * `const std::string &get_last_error() const noexcept`：取得 UTF-8 系統錯誤訊息。
  * `std::string get_path_utf8() const noexcept`：取得規範路徑。
  * `size_t use_count() const noexcept`：取得存活引用計數。
  * `template <typename FuncT> auto get_symbol(std::string_view name) const noexcept`：解析導出符號。
  * `bool is_first_loaded() const noexcept`：查詢是否為進程內首次載入（0 -> 1）。
  * `template <typename FuncT, typename... Args> bool initialize_once(std::string_view symbol, Args&&... args)`：首次載入單次初始化。
  * `std::shared_ptr<const void> create_lifetime_token() const noexcept`：建立純生命週期權杖（輕量保活）。
  * `void add_cleanup_hook(std::function<void()> hook)`：註冊同步收尾回呼（LIFO 順序執行）。
  * `bool register_shutdown_symbol(std::string_view symbol)`：註冊符號為同步收尾回呼。
  * `bool register_terminal_shutdown_symbol(std::string_view symbol)`：註冊符號為外掛終端收尾回呼（最後執行；若外掛回傳非零則拒絕卸載轉為常駐模式）。
  * `void add_async_cleanup_hook(AsyncCleanupHook hook)`：註冊非同步握手收尾回呼（主程式 0ms 立即返回，外掛善後完畢主動調用 `on_ready()` 喚醒物理卸載）。
  * `void add_post_unload_hook(std::function<void(std::string_view)> hook)`：註冊 DLL 物理卸載後全域通知。
  * `void enable_deferred_unload(bool enable = true)`：啟用非同步離棧延遲卸載防護（杜絕呼叫棧內自毀引發崩潰）。

* **類別 `ork::WeakDynamicLibrary`**：
  * `DynamicLibrary lock() const noexcept`：嘗試晉升為強引用。
  * `bool expired() const noexcept`：查詢動態庫是否已卸載。
  * `size_t use_count() const noexcept`：查詢存活強引用總數。

---

## 🌐 7. 全域 UTF-8 零拷貝輔助工具：`ork::utf8`
* **標頭檔**：`ourokore/base/utf8.hpp`
* **函式**：
  * `as_view(str)`：將各類字串零拷貝轉換為 `std::string_view`。
  * `to_string(str)`：統一轉換為 `std::string`。
  * `to_u8string(view)`：轉換為 C++20 原生 `std::u8string`。
  * `is_string_like_v<T>`：編譯期型別特徵萃取，判斷是否為字串型別。

---

## ⚙️ 8. 現代高效能雜湊工具模組：`ork::base::Hash`
* **標頭檔**：`ourokore/base/Hash.hpp`
* **演算法與運算子**：
  * `Fnv1a64(data)`：FNV-1a 64-bit 雜湊（全域 TypeID 與字串 ID 唯一標準）。
  * `Fnv1a32(data)`：FNV-1a 32-bit 雜湊。
  * `Crc32(data)`：CRC32 (IEEE 802.3) 校驗碼。
  * `MurmurHash3(data, seed)`：MurmurHash3 32-bit 高品質雜湊。
  * `HashCombine(seed, v1, v2, ...)`：變參組合雜湊。
  * 使用者自訂字面量（`using namespace ork::base::literals;`）：
    * `""_fnv64`：編譯期計算 64 位元常數。
    * `""_fnv32`：編譯期計算 32 位元常數。
    * `""_crc32`：編譯期計算 CRC32 校驗碼。

---

## 🌳 9. 樹狀物件節點與異質物件階層：`ork::base::TreeNode<T>` / `ork::base::TreeIO`
* **標頭檔**：`ourokore/base/Tree.hpp`、`ourokore/base/TreeIO.hpp`
* **樣板基底 `TreeNodeBase<Derived>` 方法**：
  * `explicit TreeNodeBase(name)`：受保護建構子（`protected`），僅供衍生類別構造自身時調用；外部禁止直接實例化未封裝之基底。
  * `CreateRoot<SubT = D>(name, args...)`：建立樹之根物件節點（唯一合法的樹入口，支援 C++20 `std::derived_from<SubT, D>` 約束與轉發建構參數，直出強型別 `std::shared_ptr<SubT>`）。
  * `Size()` / `ChildCount()`：子節點/元素數量查詢（$O(1)$）。
  * `GetElementAt(index)` / `operator[](size_t index)`：隨機下標存取（$O(1)$）。
  * `FindChildByName(name)` / `operator[](const std::u8string &name)`：名稱尋址（$O(1)$）。
  * `AddChild<SubT = D>(name, args...)` / `PrependChild<SubT = D>(name, args...)`：在尾端追加或在最前端插入具名或匿名子物件節點（直出強型別 `std::shared_ptr<SubT>`，零手動轉型，支援異質物件）。
  * `PushElement<SubT = D>(args...)`：原地構造並推入匿名異質元素節點。
  * `InsertBefore<SubT = D>(child, name, args...)` / `InsertAfter<SubT = D>(child, name, args...)`：指定位置精準插入異質衍生節點。
  * `MoveChildBefore(child, target)` / `MoveChildAfter(child, target)` / `MoveChildToIndex(child, index)`：同層子節點順序原地搬移與重排（採用 `std::rotate` 原地調度，零記憶體重配置，具名字典 $O(1)$ 尋址長存有效）。
  * `MoveBefore(target)` / `MoveAfter(target)` / `MoveToIndex(index)`：子節點自身發起之同層重排捷徑語法糖。
  * `RemoveElementAt()` / `RemoveChild()` / `ClearChildren()`：子節點移除（斷開關聯一律由父節點呼叫 `RemoveChild`）。
  * `bool DetachFromParent()`：斷開與父節點之關聯並自立為新樹（自動從父節點之容器完全移除、解開父弱引用，並分配傳播新專屬樹級讀寫鎖）。
  * `GetCleanupTracker()`：取得整棵樹之清理狀態追蹤器（`TreeCleanupTracker`），以 $O(1)$ 弱引用樹級共享鎖判定所有節點是否全數清除。
  * `Reversed()`：零拷貝反向走訪視圖糖衣。
  * `GetTreeMutex()`：取得樹級讀寫鎖（整棵樹共享同一個鎖）。
  * ⚠️ **高壓線禁忌**：走訪期間只能進行純資料讀取，**絕對禁止調用任何結構異動介面**（如 `AddChild`/`PrependChild`/`PushElement`/`RemoveChild`/`MoveChildBefore` 等），否則引發不可重入讀寫鎖重複加鎖死鎖！
  * 🔒 **拓撲特權隔離**：`AttachChild` 與 `MakeNode` 為私有內部特權（`private`，僅對 `friend class TreeIO;` 開放），外部使用者一律嚴格由父節點原地延伸構造，禁止隨意掛載外部獨立節點。
* **樹清理追蹤器 `ork::base::TreeCleanupTracker` 類別**：
  * `bool AreAllNodesCleanedUp() const noexcept`：檢查整棵樹所有節點是否已全數解構清除（鎖已銷毀即代表所有節點均已析構）。
  * `bool IsCleanedUp() const noexcept`：`AreAllNodesCleanedUp()` 之別名捷徑。
  * `bool IsAlive() const noexcept`：檢查樹是否仍有節點存活。
  * `long UseCount() const noexcept`：取得目前持有該鎖之節點引用計數預估。
  * `const std::weak_ptr<std::shared_mutex>& GetRawWeakPtr() const noexcept`：取得底層原生弱引用。
* **具體節點 `TreeNode<T>`（`StringTreeNode`）方法**：
  * `T GetData()` / `void SetData(const T &)` / `void SetData(T &&)`：資料鎖保護之存取。
* **文字 DSL 串流 `TreeIO`**：
  * `Serialize(ostream, root, ...)` / `SerializeCompact(...)` / `SerializeToString(...)`
  * `Deserialize(istream, ...)` / `DeserializeFromString(...)`：支援外部工廠模式 `factory(name, data) -> NodePtr`，反轉職責不預先製造 node；工廠造不出物件或同層具名同名衝突時，視為資料毀損立即中止並回傳 `nullptr`。
  * 模式列舉：`CompactMode::Pretty`（Allman 風格排版）/ `CompactMode::Compact`（緊湊模式，保留關鍵字等號 `=`）。

---

## ⚡ 10. 並行排程與同步設施：`ork::base::concurrency`
* **標頭檔**：
  * `<ourokore/base/FixedThreadPool.hpp>`
  * `<ourokore/base/DynamicThreadPool.hpp>`
  * `<ourokore/base/ThreadSafeQueue.hpp>`
  * `<ourokore/base/Semaphore.hpp>`
  * `<ourokore/base/Event.hpp>`
* **固定執行緒池 `ork::base::FixedThreadPool`**：
  * `explicit FixedThreadPool(size_t thread_count = hardware_concurrency())`
  * `template <typename F, typename... Args> auto Submit(F&&, Args&&...) -> std::future<...>`：非同步提交任務並取得 Future。
  * `void WaitForAll()`：同步阻塞等待當前佇列與執行中之任務全數完成。
  * `void Shutdown()`：優雅等待排隊任務完成後關閉執行緒池。
  * `size_t GetWorkerCount() const` / `size_t GetActiveCount() const` / `size_t GetPendingCount() const`
* **動態彈性伸縮池 `ork::base::DynamicThreadPool`**：
  * `DynamicThreadPool(min_threads, max_threads, idle_timeout)`
  * `Submit(F&&, Args&&...)`：自適應工作量自動擴充執行緒，閒置逾時自動縮容銷毀。
* **執行緒安全佇列 `ork::base::ThreadSafeQueue<T>`**：
  * `void Push(T item)` / `void Push(T&& item)`：執行緒安全寫入元素並喚醒等待者。
  * `bool TryPop(T &item)`：非阻塞嘗試取出元素（佇列為空時立即返回 false）。
  * `T WaitAndPop()` / `bool WaitAndPop(T &item, duration timeout)`：阻塞或限時等待取出元素。
  * `bool Empty() const` / `size_t Size() const` / `void Clear()`
* **計數信號量 `ork::base::Semaphore`**：
  * `explicit Semaphore(ptrdiff_t initial_count)`
  * `void Acquire()` / `void Release(ptrdiff_t update = 1)` / `bool TryAcquire()` / `bool TryAcquireFor(timeout)`
* **同步事件 `ork::base::Event`**：
  * `explicit Event(bool manual_reset = false, bool initially_signaled = false)`
  * `void Signal()` / `void Reset()` / `void Wait()` / `bool WaitFor(timeout)` / `bool IsSignaled() const`

---

## 🛡️ 11. 外掛 Heap 追蹤與記憶體防禦：`ork::base::HeapTracker` / `ork::PluginHeap`
* **標頭檔**：
  * `<ourokore/base/HeapTracker.hpp>`
  * `<ourokore/base/PluginHeap.hpp>`
  * `<ourokore/base/TrackedNewDelete.hpp>`
  * `<ourokore/base/heap_api.h>`
* **編譯期雙策略**：
  * `ORK_HEAP_POLICY_A`：Debug 詳細診斷 / Release 零開銷關閉（原生速度）。
  * `ORK_HEAP_POLICY_B`：Debug 詳細診斷 / Release 輕量無鎖原子計數（發布版仍可清空驗收）。
* **全域透明重載巨集**：
  * `ORK_ENABLE_PLUGIN_HEAP_TRACKING()`：一行透明攔截外掛模組內所有 `new`/`delete` 及 STL 容器配置。
* **顯式巨集與 STL 配置器**：
  * `ORK_NEW(Type, args...)` / `ORK_DELETE(ptr)`：編譯期自動捕捉 `__FILE__` 與 `__LINE__`。
  * `ORK_NEW_ARRAY(Type, count)` / `ORK_DELETE_ARRAY(ptr, count)`
  * `TrackedAllocator<T>`：相容 STL 容器之受管分配器。
* **清空判定與洩漏診斷**：
  * `bool PluginHeap::is_clean()`：查詢當前模組是否 100% 清空。
  * `std::string PluginHeap::dump_leaks_to_string(context_name)`：輸出格式化 UTF-8 洩漏診斷清單。
  * `void PluginHeap::assert_clean(context_name)`：未清空立即印出報告並拋出例外。
  * `PluginHeapGuard`：RAII 作用域洩漏檢測守衛。
* **純 C ABI 介面**：
  * `ork_heap_allocate(size, file, line)` / `ork_heap_deallocate(ptr)`
  * `ork_heap_is_clean()` / `ork_heap_dump_leaks(buf, len)` / `ork_heap_assert_clean(name)`

---

## 🌐 12. 底層純 C ABI（Cross-Language FFI）分類索引
所有底層操作保證跨 DLL 邊界零例外逃逸，完整清單與參數規格請參見《[03. 純 C ABI 規格與記憶體佈局規範](../technical/03_c_abi_and_memory.md)》：
* **類別 A：物件生命週期與工廠**（`ork_create_object`、`ork_retain_object`、`ork_release_object`、`ork_acquire_object_pointer`、`ork_release_object_pointer`）
* **類別 B：狀態查詢與圖拓撲**（`ork_is_alive`、`ork_get_storage_state`、`ork_get_type_id`、`ork_is_instance_of`、`ork_read_lock`、`ork_write_lock` 等）
* **類別 C：宿主全域特權**（`ork_host_initialize`、`ork_host_shutdown`、`ork_host_set_storage_driver`、`ork_host_trigger_dehydration_rescue` 等）
* **類別 D：外掛 Heap 記憶體檢查**（`ork_heap_allocate`、`ork_heap_deallocate`、`ork_heap_is_clean`、`ork_heap_dump_leaks`、`ork_heap_assert_clean`）
