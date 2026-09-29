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
  * `void SetObjectModuleLoader(HandleID id, const ork::DynamicLibrary &loader)`：為特定受管物件綁定動態外掛載入器，錨定動態庫生命週期於 ControlBlock 墓碑中，杜絕脫水時動態庫提前卸載引發復水崩潰。
  * `ork::DynamicLibrary GetObjectModuleLoader(HandleID id) const`：取得目標受管物件當前綁定之動態外掛載入器。
  * `std::shared_ptr<IObjectModuleBinder> GetModuleBinder() const`：取得專用模組綁定介面，實現最小特權原則委派。
  * `void Reset()`：復位核心初始化狀態（支援軟重啟與測試套件切換）。
  * `void SetObjectDestroyedCallback(void (*callback)(HandleID id))`：設定全域物件銷毀監聽回呼。
  * `uint64_t GetDeferredDeletePendingCount() const`：取得當前排隊等待物理銷毀之任務數量。

---

## 🧩 1.1 專用模組綁定介面：`ork::IObjectModuleBinder`
* **標頭檔**：`ourokore/host/IObjectModuleBinder.hpp`
* **說明**：專為專職外掛管理單元（如 PluginManager）設計之輕量權限介面，遵循介面隔離原則（ISP）與最小特權原則。
* **方法**：
  * `virtual void SetObjectModuleLoader(HandleID id, const ork::DynamicLibrary &loader) = 0`：為目標物件錨定動態庫生命週期。
  * `virtual ork::DynamicLibrary GetObjectModuleLoader(HandleID id) const = 0`：取得目標物件當前綁定之動態庫載入器。

---

## 📦 2. 領域物件基底與樣板：`ork::OuroObject` 與 `ork::Subclass`
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **`ork::OuroObject` 基底方法**：
  * `HandleID GetObjectID() const`：取得物件之全域唯一識別碼。
  * `StorageState GetStorageState() const`：取得物件當前儲存狀態（Clean/Dirty/Dehydrated/UnsavedNew）。
  * `ork_type_id_t GetTypeID() const`：取得物件之靜態型別 64 位元 TypeID（支援多型與繼承查詢）。
  * `virtual void SerializePayload(OuroStream &stream) const`：純資料屬性序列化介面。
  * `virtual void DeserializePayload(OuroStream &stream)`：純資料屬性反序列化介面。
* **`ork::Subclass<Derived, Base = ork::OuroObject>` 樣板基底**：
  * **強制約束**：所有交由 `CreateObject<T>()` 建立的受管領域物件**一律必須繼承此樣板**，嚴格禁止直接裸繼承 `OuroObject`。
  * **編譯期功能**：自動萃取短類別名稱（支援 MSVC/Clang/GCC）、自動計算與註冊 TypeID、覆寫 `GetTypeID()` 多型虛擬函式、提供完美轉發建構子。

---

## 🔗 3. 智慧 Handle 系統
* **標頭檔**：`ourokore/component/Handles.hpp`
* **類別**：
  * `OwningHandle<T>`：強持有槽位，宣告為物件成員。方法：`Set()`, `Get()`, `Release()`, `GetTargetID()`。
  * `OwningContainerHandle`：動態強持有容器，方法：`AddTarget()`, `RemoveTarget()`, `GetTargetIDs()`。
  * `UnboundHandle<T>`：無繫結非擁有型引用，方法：`LockAndAcquire()`, `GetTargetID()`, `IsAlive()`, `Release()`。
  * OuroPtr<T>：棧上活躍根指標守衛，支援 operator()(Fn&&, Args&&...)、Invoke(...)（受 C++20 std::is_member_pointer_v 約束，僅接受成員函式或欄位指標）、WithObject(Fn&&, Args&&...)（受信任進階閉包通道）、operator bool()、IsAlive()、GetTargetID()、Release()。
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
  * `Dehydrate(id)` / `Rehydrate<T>(id)`：手動脫水與復水。
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
  * `static std::filesystem::path format_filename(std::string_view base_name)`：依作業系統格式化動態庫檔名（Windows `.dll`、Linux `.so`、macOS `.dylib`）。

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
