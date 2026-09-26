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
   - 插件**可以使用**：受管物件生命週期指針（`CreateObject`, `OuroPtr`, `OuroWeakPtr`）、物件脫水/復水（`Dehydrate`, `Rehydrate`）、**純唯讀無副作用的狀態查詢**（`IsAlive`, `GetStorageState`, `GetRootEdgeCount`）。
   - 插件**絕對不可以碰觸**：上述準則一的所有 Host 特權、以及核心內部實作細節。
2. **標頭檔與目錄防洩漏規則**：
   - 所有公開發布之 `include/` 目錄，**絕不可出現**任何內部私有標頭檔（如 `internal_api.h`）。
   - 核心底層內部私有功能（兩階段構造 ID 預留、底層 ControlBlock 記憶體操作等）**必須嚴格保留於 `core/src/`**，不得打包進 SDK。
   - `host_api.h` 絕不 re-export 任何核心私有標頭檔。
3. **內部自救權杖與情境防禦（Passkey & Reservation Context Guard）**：
   - 核心允許 `CreateObject` 與 `Rehydrate` 在分配記憶體遭遇 OOM 時被動觸發緊急脫水換頁自救，但底層 `TriggerRuntimeRescue` **必須受 `OuroCreationToken` 權杖（私有建構子 Passkey 模式）與核心內部 HandleID 預留/脫水狀態（`IsValidRescueContext`）雙重保護**。
   - 嚴格禁止第三方插件在業務代碼中主動實例化 Token 或主動調用脫水救援 API。
   - 內部底層回呼（如 `RehydrateCallback`）必須嚴格收斂至 `ork::detail` 命名空間，禁止外洩至公開 `ork::` 命名空間以防插件繞過智慧指針直接取得原始裸指標。

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
