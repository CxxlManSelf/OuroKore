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

在您的應用程式或插件 CMake 專案中，請確保至少啟用 C++20：

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKoreApp LANGUAGES CXX)

# 強烈建議：使用 C++20 或更高版本標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 引入 OuroKore 標頭檔與程式庫
target_include_directories(MyOuroKoreApp PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyOuroKoreApp PRIVATE ourokore_core ourokore_base)
```

---

## 🧭 核心架構特色

1. **控制區塊與 Handle 代數系統 (ControlBlock & Handle System)**：
   - 透過全域唯一 64-bit `HandleID` 與控制區塊管理物件生命週期。
   - `OwningHandle<T>`：持有圖拓撲的強引用，支援循環參照並由背景 `CycleCollector` 非同步安全回收。
   - `UnboundHandle<T>`：無繫結弱引用（In-degree = 0），專為動態插件模組非同步卸載防釘死與旁路觀察設計。
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
