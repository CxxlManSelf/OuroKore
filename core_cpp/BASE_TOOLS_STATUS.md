# OuroKore Base 基礎工具庫規劃與實作完成度報告 (次世代架構修訂版)

本文件依據 **2026 年 10 月最新架構定調**（「次世代零件化、CID 內容定址與 `OrkGatewayDriver` 網關驅動」），全面重新審查並修訂 OuroKore 基礎模組（`ourokore_base`）之工具需求清單，果斷剔除過度設計與衝突項目，並明確列出即將實作之核心工具。

---

## 🧭 一、 架構演進方針與「軟體減法」原則

### 1. 核心定位
`ourokore_base` 嚴格維持作為獨立於核心業務之外的底層通用基礎設施庫，**100% 零依賴 `ourokore_core`**，不僅供核心調度，亦供外部宿主（Host）與外掛插件（Plugin）共用。

### 2. 次世代架構升維後的「軟體減法」
在引入次世代零件化（Component Materialization）、不可變規格藍圖、內容定址（CIDv1）與階層語意尋址後，我們遵循**「避免第二系統症候群（Second-System Syndrome）」與「防範過度工程（Over-Engineering）」**原則，對過去原計畫中未動工的項目進行了嚴格的篩選與剔除：
* **凡是破壞跨 DLL Heap 隔離者，剔除！**
* **凡是與不可變 CAS 原理相左者，剔除！**
* **凡是徒增並發除錯負擔而無實質效能增益者，剔除！**

---

## ❌ 二、 架構降維與確定剔除之項目清單 (Pruned & Deprecated)

以下 6 大項為 2026 年 9 月原計畫中提出、但在最新次世代架構審查中**確認不再需要、正式否決並予以剔除**的工具：

| 否決剔除之項目 | 原所屬模組 | 原計畫設想 | 最新架構審查否決理由（為什麼不需要） |
| :--- | :---: | :--- | :--- |
| **記憶體映射檔案 (`MmapFile`)** | `io/` | 封裝 `mmap` / `CreateFileMapping` 用於大檔案隨機讀寫 | ❌ **違反 CAS 原子性原則**：次世代零件藍圖屬於數 KB 至數百 KB 的不可變二進位快照。內容定址（CAS）最核心的要求是**「寫入原子防損毀（WriteAllBytesAtomic，防斷電寫半殘）」**與**「一次性讀入（ReadAllBytes）」**。`mmap` 跨平台行為分裂嚴重且無法提供原子替換保障，純屬多餘且危險。 |
| **全域物件池與區域分配器 (`ObjectPool`, `Arena`)** | `memory/` | 自刻池化分配器以減少 `malloc`/`free` 次數 | ❌ **破壞外掛 Heap 隔離鐵律**：零件實體由各外掛 DLL 工廠獨立配置，受 `HeapTracker` 稽核。若在 Base 硬塞全域物件池，會讓多個動態庫共享底層記憶體，**打破「跨 DLL Heap 邊界隔離」，引發跨 CRT 釋放崩潰**。現代 OS Heap（如 Segment Heap）效能已極佳，Base 絕不硬造分配器。 |
| **無鎖佇列與環形緩衝區 (`LockFreeQueue`, `RingBuffer`)** | `async/` | 追求極限並發吞吐量的無鎖資料結構 | ❌ **無實質增益且引發除錯災難**：現有 `ThreadSafeQueue` 微秒級延遲已極為充裕。次世代背景瓶頸在 SHA-256 計算與磁碟 I/O，微秒級鎖開銷微不足道；無鎖佇列的 ABA 問題、記憶體屏障與析構順序保證，在複雜生命週期系統中弊遠大於利。 |
| **Go-style 通道 (`Channel`)** | `async/` | 提供帶緩衝/無緩衝之線程通訊管道 | ❌ **語意多餘**：Base 現有之 `ThreadPool`、`ThreadSafeQueue`、`Semaphore`、`Event` 與 C++23 `Result<T>` 已完美涵蓋非同步排程需求；C++ 缺乏原生 `select` 語法支援，硬做 Channel 只會淪為語法玩具。 |
| **巨型外掛註冊中心與版本管理 (`Plugin Registry & SemVer`)** | `plugin/` | 仿 npm/Cargo 之複雜套件相依解析與 SemVer 區間比對 | ❌ **被次世代 CID 與階層標籤降維取代**：次世代架構明確定調採**「三段式階層標籤（`Vendor/Plugin/Version`）」**進行目錄尋址；精確版本與相依性未來直接由不可變「CID 內容雜湊」釘死，不再需要 Base 層去實現龐大的依賴求解器（Dependency Resolver）。 |
| **全域事件匯流排 (`EventBus`)** | `utils/` | 模組間解耦的字串廣播機制 | ❌ **破壞圖論顯式依賴**：OuroKore 強調「組裝即拓撲（組裝是顯式的有向邊）」。全域隱式 EventBus 會引入幽靈依賴，破壞 `CycleCollector` 的圖論可追蹤性，極易在外掛卸載時留下懸空回呼指標引發 Crash。 |
| **128 位元 UUID 工具 (`Uuid.hpp`, `uuid_api.h`)** | `utils/` | 原為 Phase 4 冷啟動真復活預備之隨機/時序 ID 生成器 | ❌ **【已徹底刪除】**：次世代架構規格唯一只認 **CID**，記憶體唯一只認 **HandleID**。UUID 在本體系內已無生態位，保留只會引發身分證認同混淆。現已徹底刪除相關源碼與測試，並成功解除 Windows `bcrypt.lib` 之系統鏈結依賴。 |

---

## 📊 三、 六大子系統最新完成狀態與需求重劃

### 1. `async/` 並發與非同步排程
* ✅ **固定數量執行緒池 (`FixedThreadPool`)**：支援 `submit_detached`、`wait_idle`，[ThreadPool.hpp](include/ourokore/base/ThreadPool.hpp)。
* ✅ **彈性動態伸縮執行緒池 (`DynamicThreadPool`)**：依負載擴容、閒置自動縮容，防 Worker 自死鎖。
* ✅ **執行緒安全阻塞佇列 (`ThreadSafeQueue<T>`)**：MPMC 佇列，逾時取出，支援 C++23 `pop_expected`，鎖外析構保證，[ThreadSafeQueue.hpp](include/ourokore/base/ThreadSafeQueue.hpp)。
* ❌ **Channel 通道 / 無鎖佇列**：**【已剔除】**（確認不再需要）。

---

### 2. `sync/` 同步原語
* ✅ **計數信號量 (`Semaphore`)**：資源配額控制，支援逾時獲取與釋放，[Semaphore.hpp](include/ourokore/base/Semaphore.hpp)。
* ✅ **事件通知原語 (`Event`)**：跨平台通知，支援 `AutoReset` 與廣播 `ManualReset`，[Semaphore.hpp](include/ourokore/base/Semaphore.hpp)。
* ❌ **跨進程具名訊號量 / 自旋鎖**：**【已剔除】**（專案以進程內標準同步原語為主）。

---

### 3. `plugin/` 外掛與動態載入
* ✅ **跨平台動態庫載入器 (`DynamicLibrary`)**：跨平台（Windows `LoadLibraryW` / POSIX `dlopen`）、型別安全符號獲取、生命週期反向錨定、延遲卸載防護、常駐模式（Resident Mode），[DynamicLibrary.hpp](include/ourokore/base/DynamicLibrary.hpp)。
* ✅ **弱引用動態庫 (`WeakDynamicLibrary`)**：無所有權觀察與原子安全晉升（`lock()`）。
* ✅ **生命週期權杖 (`create_lifetime_token`)**：支援樹節點群與非同步任務共享存活保證。
* ✅ **跨平台符號匯出巨集 (`export.h`)**：提供 `ORK_BASE_API`、`ORK_CALL` 等呼叫慣例。
* ❌ **龐大套件註冊中心與 SemVer 解析器**：**【已剔除】**（由階層式語意標籤與 CID 取代）。

---

### 4. `memory/` 記憶體管理與安全防禦
* 🌟 **外掛專屬 Heap 追蹤與清空檢驗 (`HeapTracker`, `PluginHeap`)**：支援編譯期雙策略 A/B 切換，外掛卸載前零洩漏清空驗證，[HeapTracker.hpp](include/ourokore/base/HeapTracker.hpp)、[PluginHeap.hpp](include/ourokore/base/PluginHeap.hpp)。
* 🌟 **顯式與全域追蹤記憶體運算子 (`TrackedNewDelete`)**：提供 `ORK_NEW`、`ORK_DELETE` 與透明重載，[TrackedNewDelete.hpp](include/ourokore/base/TrackedNewDelete.hpp)。
* 🌟 **跨語言 Heap C ABI (`heap_api.h`)**：導出純 C 堆疊監控介面，[heap_api.h](include/ourokore/base/heap_api.h)。
* ❌ **通用 ObjectPool / Arena 分配器**：**【已剔除】**（防止破壞外掛 Heap 隔離防線）。

---

### 5. `io/` 系統、檔案與 I/O 抽象
* 🌟 **現代樹狀物件節點與階層拓撲 (`Tree.hpp`)**：連續向量保序與雜湊雙模態，C++23 `std::derived_from` 異質物件直出，防爆棧迭代析構，[Tree.hpp](include/ourokore/base/Tree.hpp)。
* 🌟 **文字 DSL 狀態機串流 (`TreeIO.hpp`)**：RFC-ORKT-1.0 規範、正交界定符、寬容狀態機反序列化，[TreeIO.hpp](include/ourokore/base/TreeIO.hpp)。
* 🚀 **[新增需求] 二進位檔案工具與原子防損毀寫入 (`FileUtils.hpp`)**：
  * **快速讀寫**：`ReadAllBytes`、`WriteAllBytes`。
  * **原子寫入防毀損**：`WriteAllBytesAtomic`（先寫入同目錄 `.tmp` 暫存檔，再呼叫 OS 原生 API `MoveFileExW` / `rename` 執行原子替換，確保斷電或崩潰時零殘檔）。
  * **目錄遞迴建立**：`EnsureDirectory`（當 CAS 雜湊子目錄不存在時自動安全建立）。
* ❌ **記憶體映射檔案 (`MmapFile`)**：**【已剔除】**（確認不再需要）。

---

### 6. `utils/` 通用診斷與基礎工具
* 🌟 **現代值語意錯誤處理原語 (`Result.hpp`)**：基於 ISO C++23 `std::expected` 與 `Status`，支援 Monadic 鏈式調用，[Result.hpp](include/ourokore/base/Result.hpp)。
* ✅ **全域 UTF-8 文字輔助工具 (`utf8.hpp`)**：字串檢視、零拷貝轉換、Windows W 版 API 邊界隔離，[utf8.hpp](include/ourokore/base/utf8.hpp)。
* ✅ **現代編譯期雜湊模組 (`Hash.hpp`)**：`constexpr` FNV-1a (32/64)、CRC32、MurmurHash3，用於 TypeID 計算與 HashMap，[Hash.hpp](include/ourokore/base/Hash.hpp)。
* ❌ **128 位元 UUID 工具 (`Uuid.hpp`, `uuid_api.h`)**：**【已徹底刪除】**（移除相關原始碼、純 C ABI 與測試，並解除 `bcrypt.lib` 依賴）。
* 🚀 **[新增需求] 密碼學雜湊與標準 CID 編碼 (`SHA256.hpp`, `CID.hpp`)**：
  * **純 CPU SHA-256**：零外部依賴、純記憶體安全計算，提供 `Update` 與 `Final`。
  * **標準 Multihash (CIDv1)**：按照標準加上前綴（`0x12` 代表 SHA-256、`0x20` 代表 32 位元組長度），並提供標準 Base32 / Hex 字串編碼與解碼。
* 🚀 **[新增需求] 確定性跨平台端序轉換 (`Endian.hpp`)**：
  * 利用 C++20 `<bit>`（`std::endian`），強制在跨架構（x86_64 與 ARM64）打包二進位串流時統一為**小端序（Little-Endian）**，保證 CID 雜湊絕對確定性。
* 🚀 **[新增需求] 階層標籤解析與路徑正規化 (`PathUtils.hpp`)**：
  * **三段式拆解**：解析 `"Vendor/Plugin/Version"` 標籤。
  * **安全防路徑穿越**：阻絕包含 `..` 或非法字元的惡意路徑，防範目錄遍歷攻擊。
  * **路徑正規化**：統一正反斜線，安全映射為本機檔案系統路徑。
* ❌ **全域事件匯流排 (`EventBus`)**：**【已剔除】**（確認不再需要）。

---

## 🎯 四、 新版 Base 基礎庫待辦實裝路線圖 (Next Steps)

根據本次需求修訂，現有程式庫中已完成之核心基礎代碼（`ThreadPool`、`DynamicLibrary`、`HeapTracker`、`Tree`、`Hash`、`utf8`、`Result` 等）**皆屬極高質量之現役資產，100% 保留**；原先列為待辦但已否決的 7 大項目全數移出開發計畫。

當前 `ourokore_base` 邁向完全就緒，僅剩下以下**唯一的 4 項輕量通用工具建置工作**：

| 步驟 | 預計產出標頭檔 | 核心工作內容 | 預估複雜度 |
| :---: | :--- | :--- | :---: |
| **Step 1** | `base/include/ourokore/base/SHA256.hpp`<br>`base/include/ourokore/base/CID.hpp` | 純 C++ 零依賴 SHA-256 實裝與 Multihash (CIDv1) 格式化工具 | 🟢 輕量純演算法 |
| **Step 2** | `base/include/ourokore/base/FileUtils.hpp` | 二進位檔案全檔讀寫、原子防損毀替換（`WriteAllBytesAtomic`）、目錄遞迴建立 | 🟢 輕量 OS API 封裝 |
| **Step 3** | `base/include/ourokore/base/Endian.hpp` | 基於 `<bit>` 的強制小端序編解碼工具（確定性雜湊保證） | 🟢 極輕量 Header-only |
| **Step 4** | `base/include/ourokore/base/PathUtils.hpp` | 三段式標籤解析（`Vendor/Plugin/ver`）、防路徑穿越（Path Traversal 防禦）與路徑正規化 | 🟢 輕量字串/路徑工具 |

完成上述 4 項通用工具後，`ourokore_base` 即可宣告以最精煉、最純粹的姿態完全成熟，並無縫供未來的次世代零件網關外掛 `OrkGatewayDriver` 呼叫使用！