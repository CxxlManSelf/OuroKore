# OuroKore 核心開發規範：三層邊界與多語言 FFI 準則

本專案往後所有新增、修改之功能與 API，均必須嚴格遵循以下三重審查原則：

## 1. 主程式（Host）特權專用審查
- **判定要點**：是否只有主程式可以使用？
- **規範**：
  - 凡涉及全進程生命週期（`Init`、`Shutdown`、`Reset`）、基礎設施注入（儲存驅動、脫水器、執行緒池）、全域 GC/延遲銷毀排程、全域記憶體緊急救援（`TriggerDehydrationRescue`）、全域銷毀回呼（`SetObjectDestroyedCallback`），以及白盒測試故障模擬（`SetStorageState`、`ClearObjectPayload`），**必須嚴格歸入 Host 層**（C++ `HostContext` 與純 C `host_api.h`），受特權校驗保護。

## 2. 第三方插件（Plugin/Component）隔離審查
- **判定要點**：第三方插件是否絕對不可使用？
- **規範**：
  - 插件僅能使用受管物件指針（`CreateObject`、`OuroPtr`、`OuroWeakPtr`）、物件脫水落盤，以及**安全唯讀狀態查詢**（`IsAlive`、`GetStorageState`、`GetRootEdgeCount`）。
  - 插件絕不可碰觸任何進程級特權或內部實作細節。
  - 公開之 `include/` 目錄絕不可含有內部私有標頭檔（如 `internal_api.h`，必須置於 `core/src/`）。

## 3. 多語言使用介面（Cross-Language FFI）審查
- **判定要點**：是否需要且已提供支援其他語言（C#、Rust、Python、Go 等）的使用介面？
- **規範**：
  - 遵循「**C ABI 為底，各語言 Wrapper 為糖**」之核心哲學。
  - 功能絕不可只實現於 C++ 類別中，底層必須先在 `c_api/` 提供對應的純 C ABI（`extern "C"`、整數狀態碼、HandleID、C 函式指標）。
  - 純 C 函式內部必須以 `try/catch` 嚴密攔截所有 C++ 例外，防止例外飛出 DLL 導致崩潰。
  - C++ `HostContext` 與 `OuroCore.hpp` 作為現代語義包裝層，確保未來跨語言綁定庫享有 100% 相同控制權。

## 4. 語言標準規範（C++20 Invariant）
- **核心程式庫**：全專案嚴格使用 **ISO C++20** 標準建立（`CMAKE_CXX_STANDARD 20`、`CMAKE_CXX_STANDARD_REQUIRED ON`、`CMAKE_CXX_EXTENSIONS OFF`）。
- **使用端規範**：強烈建議使用端（Host 主程式、Plugin 外掛模組）亦採用 **C++20 或更高版本之標準** 進行開發，以確保 C++ 模板展開、STL 記憶體模型與公開標頭檔（Headers）之 100% 相容。
