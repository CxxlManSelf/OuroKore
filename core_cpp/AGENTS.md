# OuroKore 專案架構規範與開發守則

本文件為 OuroKore 專案之核心記憶與開發規範。未來所有在專案中新增、修改的功能與 API，均必須嚴格依據以下三項核心準則進行審查與設計：

---

## 核心設計總綱：三層邊界隔離與跨語言 FFI 友善架構

任何新增或修改的功能，必須遵循「**C ABI 為底，各語言 Wrapper 為糖**」之設計哲學，並於設計階段完成以下三重檢核：

```
[ 新功能 / 修改提案 ]
        │
        ├── 檢核 1：這項功能是否屬於主程式（Host）獨佔特權？
        ├── 檢核 2：這項功能第三方插件（Plugin/Component）是否絕對不可碰觸？
        └── 檢核 3：這項功能是否需要且已提供支援其他語言（C#/Rust/Python）的純 C ABI？
```

---

## 語言標準與編譯要求規範 (C++20 Standard Invariant)

> **核心原則：OuroKore 核心程式庫全體均以 ISO C++20 標準建立，使用端強烈建議以 C++20 以上標準進行開發。**

1. **核心程式庫建置標準**：
   - 專案根目錄 [CMakeLists.txt](file:///c:/MySrc/OuroKore/core_cpp/CMakeLists.txt) 強制指定 `CMAKE_CXX_STANDARD 20`、`CMAKE_CXX_STANDARD_REQUIRED ON`、`CMAKE_CXX_EXTENSIONS OFF`。所有核心庫（`ourokore_base`、`ourokore_core`）與單元測試均以標準 ISO C++20 進行編譯。
2. **使用端（Client / Host / Plugin）開發規範**：
   - **強烈建議使用端一律使用 C++20 或更高版本之標準（C++20+）開發**。
   - 程式庫標頭檔使用了 C++20 語法特性、STL 特性與現代記憶體模型，使用端若低於 C++20 可能遭遇編譯期型別或標頭檔無法解析之錯誤，使用 C++20 以上能確保 ABI 佈局與模板相容性最佳化。

---

## 檢核準則一：主程式（Host）特權專用判定

> **核心原則：凡涉及進程級全域控制、資源調度與破壞性操作，必須且只能由主程式掌控。**

若功能符合以下任一條件，**必須收斂至 Host 層**（C++ [`HostContext`](file:///c:/MySrc/OuroKore/core_cpp/include/ourokore/host/HostContext.hpp) 與純 C [`host_api.h`](file:///c:/MySrc/OuroKore/core_cpp/include/ourokore/c_api/host_api.h)），並受特權校驗保護：
1. **全進程生命週期**：核心初始化宣告、優雅關閉（`Shutdown`）、核心狀態重置（`Reset`）。
2. **基礎設施注入**：持久化儲存驅動（`IStorageDriver`）、全域自動脫水策略（`IAutoDehydrator`）、核心背景執行緒池。
3. **全域資源排程與監控**：循環參照收集判定（`CollectCycles`）、延遲銷毀模式與排空（`FlushDeferredDeletions`）、排隊任務數量監控。
4. **全域記憶體緊急調度**：全域記憶體緊急脫水救援（`TriggerDehydrationRescue`，防範插件惡意導致全進程卡頓）。
5. **全域監聽器註冊**：進程級物件銷毀監聽回呼（`SetObjectDestroyedCallback`，防範動態載入的插件卸載後留下懸空回呼指標引發 Crash）。
6. **白盒測試與故障模擬**：強制清空記憶體 Payload、手動修改 StorageState（此類操作具破壞性，僅限 Host/單元測試模擬使用）。

---

## 檢核準則二：第三方插件（Plugin/Component）嚴格隔離防護

> **核心原則：第三方插件僅能使用受管物件與安全查詢，絕不可具備干預系統運作或存取內部細節的能力。**

1. **功能邊界界定**：
   - 插件**可以使用**：受管物件生命週期句柄（`CreateObject`, `OuroPtr`, `UnboundHandle`，其中 `OuroPtr` 透過 `operator()` 轉發呼叫，嚴禁解引用裸指標）、物件脫水/復水（`Dehydrate`, `Rehydrate`）、**純唯讀無副作用的狀態查詢**（`IsAlive`, `GetStorageState`, `GetRootEdgeCount`, `Is<T>`, `GetTypeID`）。
   - 插件**絕對不可以碰觸**：上述準則一的所有 Host 特權、以及核心內部實作細節。
2. **標頭檔與目錄防洩漏規則**：
   - 所有公開發布之 `include/` 目錄，**絕不可出現**任何內部私有標頭檔（如 `internal_api.h`）。
   - 核心底層內部私有功能（兩階段構造 ID 預留、底層 ControlBlock 記憶體操作等）**必須嚴格保留於 `core/src/`**，不得打包進 SDK。
   - `host_api.h` 絕不 re-export 任何核心私有標頭檔。
3. **內部自救權杖與情境防禦（Passkey & Reservation Context Guard）**：
   - 核心允許 `CreateObject` 與 `Rehydrate` 在分配記憶體遭遇 OOM 時被動觸發緊急脫水換頁自救，但底層 `TriggerRuntimeRescue` **必須受 `OuroCreationToken` 權杖（私有建構子 Passkey 模式）與核心內部 HandleID 預留/脫水狀態（`IsValidRescueContext`）雙重保護**。
   - 嚴格禁止第三方插件在業務代碼中主動實例化 Token 或主動調用脫水救援 API。
   - 內部底層回呼（如 `RehydrateCallback`）必須嚴格收斂至 `ork::detail` 命名空間，禁止外洩至公開 `ork::` 命名空間以防插件繞過安全句柄直接取得原始裸指標。
4. **動態插件 CMake 建置規範（MODULE Target Invariant）**：
   - 所有動態擴充外掛（Plugin / Component）與測試動態庫，在 CMake 中**一律強制宣告為 `add_library(<name> MODULE ...)`**，嚴格禁止宣告為 `SHARED`。
   - `MODULE` 從 CMake 建置系統維度強制禁止其他 Target 在編譯期進行靜態鏈結（Link），確保外掛僅能於執行期透過 `DynamicLibrary`（`LoadLibrary` / `dlopen`）動態加載，杜絕誤鏈結導致外掛喪失獨立卸載能力。

---

## 檢核準則三：多語言使用介面（Cross-Language FFI）支援

> **核心原則：任何功能絕不可「只」實現於 C++ 類別中，底層必須先有完備的純 C ABI。**

未來本專案將用於開發 C#、Rust、Python、Go 等不同語言的使用介面，所有新增與修改的功能必須滿足：
1. **底層純 C ABI 完備**：
   - 所有核心功能、Host 特權或唯讀查詢，必須於 `c_api/` 目錄下的 C 標頭檔（`core.h`、`component_api.h`、`host_api.h`）中提供對應的純 C 函式（`extern "C"`、`ORK_API`）。
   - 使用純整數狀態碼（`int32_t`）、`HandleID`（`uint64_t`）、基礎型別指標或 C-style 函式指標進行跨語言溝通。
2. **嚴格禁止 C++ 例外跨越 DLL 邊界**：
   - 所有純 C ABI 函式內部必須以 `try/catch` 攔截所有 C++ 例外，轉換為相應的 `ORK_STATUS_ERROR_*` 錯誤碼傳回。
3. **C++ 高階封裝作為 Wrapper**：
   - C++ 高階物件（如 `HostContext`、`OuroCore.hpp` 輔助函式）僅作為基於純 C ABI 之上的現代安全包裝層（RAII、模板、異常安全）。
   - 如此可確保未來為 C#（P/Invoke）或 Rust（`extern "C"`）開發綁定時，能 100% 完整接入相同能力，不會出現跨語言功能斷層。

---

## 全域文字與訊息編碼標準規範 (UTF-8 Standard Invariant)

> **核心原則：OuroKore 框架全體涉及到任何文字訊息、字串、識別碼與路徑，一律以 UTF-8 為唯一強制標準。**

1. **全域唯一字串編碼**：
   - 所有的屬性鍵名（Property Keys）、插槽名稱（Slot Names）、錯誤描述字串（Error Messages）、日誌輸出（Logging）、二進位資料串流（Binary Streams）以及跨語言 FFI 傳遞的字元指標，**內部 100% 強制使用 UTF-8 編碼**。
   - 嚴禁在核心內部或公開介面中混用 ANSI 本地編碼（如 CP950/Big5、CP936/GBK、Windows-1252 等）。
2. **作業系統 API 邊界轉換隔離**：
   - 在 Windows 平台上呼叫系統原生 API 時（如檔案 I/O、動態庫載入、系統錯誤代碼格式化），底層實作必須在邊界內部將 UTF-8 顯式轉換為 UTF-16 寬字元（`std::wstring`）並調用 Unicode `W` 版 API（如 `LoadLibraryW`、`FormatMessageW`）。
   - 系統回傳的字串訊息必須立即轉換為 UTF-8 儲存與傳播，杜絕平台特有編碼汙染核心。
3. **二進位協議規範**：
   - 藍圖打包、脫水落盤與網路傳輸中的字串，統一採用「4 位元組長度前綴（Little-Endian `uint32_t len`）+ `len` 個位元組之 UTF-8 內容（無 null 結尾字元）」標準，確保多語言資料 100% 互通。

---

## 基礎工具層與型別唯一性規範 (Base Utilities & TypeID Invariant)

> **核心原則：基礎模組為底，通用工具統一收斂，型別識別碼全域唯一且長存。**

1. **Base 模組職責邊界**：
   - `ourokore_base` 僅提供零依賴、高效能且相容 C++20 `constexpr` 之現代系統基礎設施（執行緒池、同步原語、動態庫載入器、標準雜湊演算法 `Hash.hpp`）。
   - 基礎層嚴格禁止逆向依賴 `ourokore_core`，確保通用工具庫可獨立被任何宿主或外掛共用。
2. **全域唯一 TypeID 雜湊標準**：
   - 核心所有型別識別碼（`ork_type_id_t`）無論於編譯期 `ork::Subclass` 樣板基底或執行期字串註冊，**一律統一採用 `ork::base::Fnv1a64` 進行計算**，嚴禁各模組自定義重複邏輯。
3. **脫水墓碑型別查詢零 I/O 保證 (Zero-I/O Dehydration Invariant)**：
   - 控制區塊（ControlBlock）必須長存 `TypeID`，即使記憶體實體脫水落盤釋放，呼叫 `IsAlive()`、`Is<T>()`、`GetTypeID()` 或 C ABI `ork_is_instance_of` 必須保證純記憶體命中，絕不觸發復水與磁碟 I/O。

