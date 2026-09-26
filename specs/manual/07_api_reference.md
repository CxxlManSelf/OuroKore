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

---

## 📦 2. 領域物件基底：`ork::OuroObject`
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **方法**：
  * `HandleID GetObjectID() const`：取得物件之全域唯一識別碼。
  * `StorageState GetStorageState() const`：取得物件當前儲存狀態（Clean/Dirty/Dehydrated/UnsavedNew）。
  * `virtual void SerializePayload(OuroStream &stream) const`：純資料屬性序列化介面。
  * `virtual void DeserializePayload(OuroStream &stream)`：純資料屬性反序列化介面。

---

## 🔗 3. 智慧 Handle 系統
* **標頭檔**：`ourokore/component/Handles.hpp`
* **類別**：
  * `OwningHandle<T>`：強持有槽位，宣告為物件成員。方法：`Set()`, `Get()`, `Release()`, `GetTargetID()`。
  * `OwningContainerHandle`：動態強持有容器，方法：`AddTarget()`, `RemoveTarget()`, `GetTargetIDs()`。
  * `UnboundHandle<T>`：無繫結非擁有型引用，方法：`LockAndAcquire()`, `GetTargetID()`, `IsAlive()`, `Release()`。
  * `OuroPtr<T>`：棧上活躍根指標守衛，支援 `operator->`, `operator*`, `GetTargetID()`, `Release()`。

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
* **設計哲學**：無手動卸載（No Manual Unload）、生命週期反向錨定（Life-Bound Retention）、使用端自訂工廠函式。
* **方法**：
  * `static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = Default)`：自 UTF-8 路徑載入動態庫（Windows 內部使用 Unicode `LoadLibraryW`，杜絕本地 ANSI/CP950 亂碼）。
  * `static DynamicLibrary load(const std::filesystem::path &path, ...)`：自檔案路徑載入動態庫。
  * `bool is_loaded() const noexcept`：查詢動態庫是否載入成功。
  * `const std::string &get_last_error() const noexcept`：取得 UTF-8 格式的系統錯誤訊息。
  * `std::string get_path_utf8() const noexcept`：取得載入函式庫之 UTF-8 規範路徑。
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
