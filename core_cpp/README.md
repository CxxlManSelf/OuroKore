# OuroKore Core C++

**OuroKore** 是一個針對超大規模物件圖（Large-Scale Object Graph）託管、弱引用代數系統、透明自動脫水換頁（Dehydration/Rehydration）與三層權限邊界隔離所設計的高效能 C++ 系統核心框架。

---

## ⚡ 語言標準與編譯要求 (C++20 Standard Invariant)

> ⚠️ **重要環境規範**：
> **OuroKore 核心程式庫全體（包含 `ourokore_base` 與 `ourokore_core`）均嚴格採用 ISO C++20 標準建立。**
> 
> 為確保公開標頭檔中的現代 C++ 模板（例如 `OwningHandle<T>`、`OuroPtr<T>`）、STL 資料結構記憶體佈局、多執行緒同步原語與最佳化編譯相容性：
> **程式庫的使用端（無論是 Host 主程式或外掛 Component/Plugin）強烈建議一律採用 C++20 或更高標準（C++20+）進行開發與建置。**

### CMake 使用端配置建議

在您的應用程式或插件 CMake 專案中，請確保啟用 C++20。針對動態外掛（Plugin），**強制使用 `MODULE` 庫類型**：

#### 1. 宿主主程式 (Host Executable)
```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKoreApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MyOuroKoreApp main.cpp)
target_include_directories(MyOuroKoreApp PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyOuroKoreApp PRIVATE ourokore_core ourokore_base)
```

#### 2. 動態擴充外掛 (Plugin / Component)
> ⚠️ **鐵律：外掛在庫類型上必須使用 `MODULE`，嚴禁宣告為 `SHARED`**。`MODULE` 保證外掛庫無法被靜態鏈結，僅供執行期透過 `DynamicLibrary` 動態載入，維持純淨熱卸載隔離。

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKorePlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 宣告為 MODULE 插件庫（非 SHARED）
add_library(MyPlugin MODULE MyPlugin.cpp)
target_include_directories(MyPlugin PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyPlugin PRIVATE ourokore_core ourokore_base)
set_target_properties(MyPlugin PROPERTIES 
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
```

### 全域文字訊息標準 (UTF-8 Standard Invariant)

> ⚠️ **全域唯一字串編碼標準**：
> OuroKore 系統內部涉及到任何文字訊息（屬性鍵名、插槽名稱、錯誤描述、二進位字串序列化與跨語言 FFI 傳遞），**一律強制採用 UTF-8 為唯一標準**。
> Windows 下涉及檔案路徑與作業系統呼叫，底層自動轉換為 `std::wstring` 呼叫 Unicode `W` 版 API，對外介面與持久化儲存 100% 保持 UTF-8，嚴禁混用 ANSI 本地編碼。

---

## 🧭 核心架構特色

1. **控制區塊與 Handle 代數系統 (ControlBlock & Handle System)**：
   - 透過全域唯一 64-bit `HandleID` 與控制區塊管理物件生命週期。
   - `OwningHandle<T>`：持有圖拓撲的強引用，支援循環參照並由背景 `CycleCollector` 非同步安全回收。
   - `UnboundHandle<T>`：純旁觀者弱引用（只看不管生死），專為外掛模組隨時卸載防卡死、UI 介面暫時觀察與快取索引設計。
   - `OuroPtr<T>`：棧上與根參照守衛（Root Edge），內建讀寫鎖與安全保護。

2. **記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)**：
   - 物件生命週期支援 `UnsavedNew`、`Clean`、`Dirty`、`Dehydrated` 四種儲存狀態。
   - 在記憶體壓力或 LRU 策略觸發時將 Dirty 物件序列化落盤並安全釋放記憶體 Payload，保留 ControlBlock。存取時透明按需自 `IStorageDriver` 復原。

3. **三層邊界隔離與跨語言 FFI 友善架構**：
   - 遵循「**C ABI 為底，各語言 Wrapper 為糖**」之核心設計哲學。
   - **Host 層獨佔特權**：全進程生命週期（`Init`、`Shutdown`、`Reset`）、基礎設施注入（儲存驅動、脫水器）、全域 GC 調度。
   - **Plugin/Component 隔離**：僅限使用受管物件、讀寫鎖與安全唯讀查詢，嚴禁碰觸系統級特權。
   - **底層純 C ABI**：所有功能均有完備的純 C 函式（`c_api/`），禁止 C++ 例外跨越 DLL 邊界，便於未來接入 C#、Rust、Python 與 Go。

---

## 📚 相關規範與文件指引

- **架構規範與開發守則**：[AGENTS.md](file:///c:/MySrc/OuroKore/core_cpp/AGENTS.md)
- **API 三層邊界與 FFI 準則**：[.agents/rules/api_boundaries.md](file:///c:/MySrc/OuroKore/core_cpp/.agents/rules/api_boundaries.md)
- **AI 助理架構維護技能指引**：[.agents/skills/ourokore/SKILL.md](file:///c:/MySrc/OuroKore/core_cpp/.agents/skills/ourokore/SKILL.md)
