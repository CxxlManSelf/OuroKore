# OuroKore Base 基礎工具庫規劃與實作完成度報告

本文件詳細記錄 OuroKore 基礎模組（`ourokore_base`）自 2026 年 9 月規劃以來的原始設計目標、六大子系統之工具清單、當前實作完成狀況，以及尚未實作之項目表列。

---

## 🧭 一、 背景與原計畫起源

在 2026 年 9 月 2 日的專案架構規劃中，確立了為 OuroKore 建立通用基礎依賴層（`ourokore_base`）的方針：
* **核心定位**：作為獨立於 OuroKore 核心業務之外的底層基礎設施庫，**零依賴 `ourokore_core`**，不僅供核心內部調度，亦可直接給外部使用端（Host 與 Plugin）使用。
* **使用者最初指定之三大核心需求**：
  1. **執行緒管理**：自動管理可開啟的執行緒數量，提供「固定數量」與「隨負載自動增減」兩種執行緒池。
  2. **Semaphore**：計數訊號量與跨平台事件原語。
  3. **DLL Loader**：跨平台動態模組載入器。
* **架構劃分**：依據功能領域，於目錄與程式碼中劃分為 6 大子系統模組：
  * `async/`：並發與非同步排程
  * `sync/`：同步原語
  * `plugin/`：外掛與動態庫載入
  * `memory/`：記憶體管理與安全防禦
  * `io/`：系統與 I/O 抽象
  * `utils/`：通用診斷與基礎工具

---

## 📊 二、 總覽統計與進度摘要

* **使用者最初指定之 3 大需求**：**100% 已全數完成**（`FixedThreadPool`、`DynamicThreadPool`、`Semaphore`/`Event`、`DynamicLibrary`）。
* **語言標準全面升級**：專案已升級至 **ISO C++23**（`CMAKE_CXX_STANDARD 23`）。
* **現代值語意錯誤處理原語全面落地**：實作 [`Result.hpp`](include/ourokore/base/Result.hpp)，直接基於原生 C++23 `std::expected` 與 `Status`，支援 Monadic 鏈式調用，並已無縫整合至 `ThreadSafeQueue` 與 `DynamicLibrary`。
* **後續演進超額完成之重大基礎設施**：
  * **現代樹狀物件節點與文字 DSL 串流**（[`Tree.hpp`](include/ourokore/base/Tree.hpp) 與 [`TreeIO.hpp`](include/ourokore/base/TreeIO.hpp)，支援異質物件、CRTP、RFC-ORKT-1.0 規範、防爆棧迭代）。
  * **外掛 Heap 追蹤與清空檢驗系統**（[`HeapTracker.hpp`](include/ourokore/base/HeapTracker.hpp)、[`PluginHeap.hpp`](include/ourokore/base/PluginHeap.hpp) 與 [`heap_api.h`](include/ourokore/base/heap_api.h)，支援編譯期雙策略 A/B 切換）。
  * **C++20/C++23 現代編譯期雜湊模組**（[`Hash.hpp`](include/ourokore/base/Hash.hpp)，全面支援 `constexpr`、FNV-1a、CRC32、MurmurHash3 等）。
  * **全域 UTF-8 零拷貝轉換工具**（[`utf8.hpp`](include/ourokore/base/utf8.hpp)）。
* **尚未實作之項目**：主要集中於進階記憶體池（ObjectPool / Arena）、系統 I/O（MmapFile / 定時器輪 Scheduler）以及輔助工具（非同步 Logger、UUID、Channel）。
* 📌 **架構排程決策**：
  * **「外掛註冊中心（Plugin Registry）、語意化版本管理（SemVer）與依賴解析」**：經確認留待下一個階段的 OuroKore 實作再來處理。
  * **「記憶體映射檔案（MmapFile）」**：因涉及下一個階段物件再生與儲存驅動二進位協議，先行擱下。

---

## 📋 三、 六大子系統工具清單與完成狀態表列

### 1. `async/` 並發與非同步排程

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **固定數量執行緒池** | `FixedThreadPool`，任務排程，回傳 `std::future` 與 Fire-and-Forget | ✅ **已完成** | [ThreadPool.hpp](include/ourokore/base/ThreadPool.hpp) |
| **彈性動態伸縮執行緒池** | `DynamicThreadPool`，依負載擴容，閒置逾時自動縮容，防 Worker 自死鎖 | ✅ **已完成** | [ThreadPool.hpp](include/ourokore/base/ThreadPool.hpp) |
| **執行緒安全阻塞佇列** | `ThreadSafeQueue<T>`，MPMC 多生產者多消費者佇列，逾時取出，鎖外析構保證 | ✅ **已完成** | [ThreadSafeQueue.hpp](include/ourokore/base/ThreadSafeQueue.hpp) (已擴充 `pop_expected` 等 C++23 介面) |
| **Channel 通道** | Go-style CSP 通道，帶緩衝/無緩衝之線程資料管道 | ❌ **未完成** | 原計畫建議，尚未實作 |
| **無鎖佇列 / 環形緩衝區** | SPSC / MPMC Lock-free Queue & RingBuffer | ❌ **未完成** | 目前以互斥鎖保護之阻塞式 `ThreadSafeQueue` 滿足需求 |
| **工作竊取 / 優先級佇列** | Work-Stealing 或 Priority Queue 任務調度 | ❌ **未完成** | 目前為先進先出（FIFO）佇列 |

---

### 2. `sync/` 同步原語

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **計數信號量** | `Semaphore`，資源配額控制，逾時獲取與釋放 | ✅ **已完成** | [Semaphore.hpp](include/ourokore/base/Semaphore.hpp) |
| **事件通知原語** | `Event`，跨平台條件通知，支援 `AutoReset` 與 `ManualReset`（廣播） | ✅ **已完成** | [Semaphore.hpp](include/ourokore/base/Semaphore.hpp) |
| **跨進程具名訊號量** | Named Semaphore，用於多進程 IPC 共享資源配額 | ❌ **未完成** | 原計畫建議，尚未實作 |
| **自旋鎖** | 輕量級用戶態 `SpinLock` | ❌ **未完成** | 專案現行以 `std::mutex` 與 `std::shared_mutex` 為主 |

---

### 3. `plugin/` 外掛與動態載入

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **動態庫載入器** | `DynamicLibrary`，跨平台動態加載、型別安全符號獲取、生命週期反向錨定、非同步善後握手、離棧延遲卸載防護、常駐模式（Resident Mode） | ✅ **已完成** | [DynamicLibrary.hpp](include/ourokore/base/DynamicLibrary.hpp) (已擴充 `try_load` / `try_get_symbol` 等 C++23 介面) |
| **弱引用動態庫** | `WeakDynamicLibrary`，無所有權觀察與原子安全晉升重獲（`lock()`） | ✅ **已完成** | [DynamicLibrary.hpp](include/ourokore/base/DynamicLibrary.hpp) |
| **純生命週期權杖** | `create_lifetime_token`，支援樹節點群與非同步任務共享存活保證 | ✅ **已完成** | [DynamicLibrary.hpp](include/ourokore/base/DynamicLibrary.hpp) |
| **跨平台符號匯出巨集** | `export.h`，提供 `ORK_BASE_API`、`ORK_CALL` 等呼叫慣例 | ✅ **已完成** | [export.h](include/ourokore/base/export.h) |
| **外掛註冊中心與版本管理** | Plugin Registry，集中管理外掛實例、版本比對與依賴驗證 | ⏸️ **階段排程** | **排定於下階段 OuroKore 實作**。<br>現行 `DynamicLibrary` 具備路徑弱引用快取共享、首度載入識別（`is_first_loaded`）與單次初始化（`initialize_once`），已完全滿足當前載入需求。 |

---

### 4. `memory/` 記憶體管理與安全防禦

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **外掛 Heap 追蹤與清空檢驗** | `HeapTracker` / `PluginHeap`，支援編譯期方案 A/B 切換，外掛卸載前零洩漏清空驗證 | 🌟 **超額完成** | [HeapTracker.hpp](include/ourokore/base/HeapTracker.hpp)、[PluginHeap.hpp](include/ourokore/base/PluginHeap.hpp) |
| **顯式與全域記憶體運算子** | `TrackedNewDelete`，`ORK_NEW`、`ORK_DELETE`、`TrackedAllocator` 與全域 `operator new` 透明重載 | 🌟 **超額完成** | [TrackedNewDelete.hpp](include/ourokore/base/TrackedNewDelete.hpp) |
| **跨語言 Heap C ABI** | `heap_api.h`，導出 `ork_heap_allocate`、`ork_heap_is_clean`、`ork_heap_assert_clean` 等純 C 介面 | 🌟 **超額完成** | [heap_api.h](include/ourokore/base/heap_api.h) |
| **物件池 (Object Pool)** | 高頻物件（Handle、Task 等）避免頻繁 `malloc`/`free` 的池化分配器 | ❌ **未完成** | 原計畫建議，尚未實作 |
| **區域/線性記憶體分配器** | Arena / Slab Allocator，批次快速配置與整體釋放 | ❌ **未完成** | 原計畫建議，尚未實作 |
| **共享引用位元組切片** | `ByteSpan` / `SharedBuffer`，帶引用計數的零拷貝二進位緩衝區 | ❌ **未完成** | 原計畫建議，尚未實作 |

---

### 5. `io/` 系統、檔案與 I/O 抽象

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **現代樹狀物件節點與階層拓撲** | `TreeNodeBase<D>`、`StringTreeNode`，連續向量保序與雜湊雙模態，C++20/C++23 `std::derived_from` 異質物件直出，防爆棧迭代析構 | 🌟 **超額完成** | [Tree.hpp](include/ourokore/base/Tree.hpp) |
| **文字 DSL 狀態機串流** | `TreeIO`，正交界定符、Pretty/Compact 雙模式、寬容狀態機反序列化、RFC-ORKT-1.0 規範、外部工廠優先架構 | 🌟 **超額完成** | [TreeIO.hpp](include/ourokore/base/TreeIO.hpp) |
| **記憶體映射檔案 (MmapFile)** | 跨平台封裝 Windows `CreateFileMapping` / POSIX `mmap` | ⏸️ **階段擱置** | 涉及下階段物件再生與儲存驅動二進位格式，先行擱置 |
| **高精度計時器與排程器** | TimerScheduler，時間輪（Time-wheel）或最小堆，支援延遲單次或週期性心跳 | ❌ **未完成** | 原計畫建議，尚未實作 |

---

### 6. `utils/` 通用診斷與基礎工具

| 工具項目 | 原計畫規格描述 | 當前狀態 | 實作檔案 / 現況說明 |
| :--- | :--- | :---: | :--- |
| **現代值語意錯誤處理原語** | `Result<T, E>` / `StatusOr<T>`，基於 ISO C++23 `std::expected` 與 `Status`，支援 Monadic 鏈式調用 | 🌟 **全面加入 (已完成)** | [Result.hpp](include/ourokore/base/Result.hpp) |
| **全域 UTF-8 文字輔助工具** | `utf8.hpp`，提供 `as_view`、`to_string`、`to_u8string`、`is_string_like_v`，Windows W 版 API 邊界轉換隔離 | ✅ **已完成** | [utf8.hpp](include/ourokore/base/utf8.hpp) |
| **現代編譯期雜湊模組** | `Hash.hpp`，`constexpr` FNV-1a (32/64)、CRC32、MurmurHash3、`HashCombine`、字面量 `_fnv64` / `_crc32` | ✅ **已完成** | [Hash.hpp](include/ourokore/base/Hash.hpp) |
| **輕量非同步結構化日誌** | Async Logger，基於佇列與背景執行緒的非阻塞日誌器 | ❌ **未完成** | 原計畫建議，尚未實作 |
| **UUID / GUID 生成與解析** | `Uuid`，符合 RFC 4122 / RFC 9562 規範，跨平台密碼學安全隨機 UUIDv4、時間排序單調 UUIDv7，C++23 constexpr 解析、`_uuid` 字面量、`Result<Uuid>`、三路比較 `<=>`、`std::format` 特化與純 C ABI `uuid_api.h` | 🌟 **超額完成** | [Uuid.hpp](include/ourokore/base/Uuid.hpp)、[uuid_api.h](include/ourokore/base/uuid_api.h) |
| **事件匯流排 / 委派** | EventBus / Signal-Slot，模組解耦廣播機制 | ❌ **未完成** | 原計畫建議，尚未實作 |

---

## 🎯 四、 總結與後續推進建議

當前 `ourokore_base` 模組中**直接支撐 OuroKore 核心運作、執行緒排程、外掛熱卸載、安全防護與錯誤處理**的關鍵基礎設施已高度成熟且具備完整技術規格與單元測試。

依據目前的架構分期規劃：
1. **外掛生態與版本管理（Plugin Registry & Version Management）**：留待下階段 OuroKore 核心層開發時一併統籌處理。
2. **記憶體映射檔案（MmapFile）**：留待下階段儲存驅動與物件再生協議敲定後實作。
3. **其餘 Base 待實作工具之建議優先順序**：
   - **非同步日誌**：`AsyncLogger`（提供非阻塞式系統診斷日誌）。
   - **高精度定時器**：`TimerScheduler`（通用定時排程引擎）。
   - **進階池化按需擴充**：`ObjectPool` / `ArenaAllocator` 與 `Channel`（UUID 生成器已完成）。