# 01. OuroKore 系統概述與架構哲學

## 📖 什麼是 OuroKore？

**OuroKore** 是一個專為**超大規模物件圖（Large-Scale Object Graph）**、**極致執行緒安全**、**記憶體吃緊時自動換頁脫水（Dehydration/Rehydration）**以及**藍圖持久化打包（Blueprint Packaging）**所設計的高效能 C++ 領域物件託管系統。

它廣泛適用於：
* 3A 遊戲世界實體與階層拓撲管理（角色、裝備、技能樹、場景物件）。
* 大規模世界編輯器與 CAD 節點圖系統。
* 需要高併發讀寫、記憶體配額受限且須透明落盤的高效能後端架構。

---

## 🧭 核心心智模型與三大基石

```
+-----------------------------------------------------------------------+
|                             OuroKore 架構                             |
+------------------------------------+----------------------------------+
| 1. 控制區塊與 Handle 代數系統      | 2. 透明換頁脫水與復水系統        |
|    - ControlBlock 跨模組託管       |    - 四態生命週期 (StorageState)  |
|    - OwningHandle (強擁有權邊緣)   |    - 內建 LRU 自動淘汰換頁       |
|    - UnboundHandle (無繫結解耦引用)|    - Transparent Rehydration     |
|    - OuroPtr (棧上安全根守衛)      |    - OOM 緊急自救脫水救援        |
+------------------------------------+----------------------------------+
| 3. 純 Payload 與拓撲分離藍圖打包    | 4. 三層權限隔離與 HostContext    |
|    - Pure Payload 序列化           |    - HostContext 獨佔特權        |
|    - Edge Roster 拓撲槽位自動打包  |    - Plugin 物理隔離零洩漏       |
|    - 兩階段套用與例外安全防禦      |    - 純 C ABI 底座多語言支援     |
+------------------------------------+----------------------------------+
```

### 1. 控制區塊與 Handle 代數系統 (ControlBlock & Handle System)
* **禁止裸指標**：領域物件絕不直接由 `new`/`delete` 或裸指標管理，而是統一配發全域唯一的 64 位元 `HandleID`，由底層控制區塊（`ControlBlock`）託管。
* **強邊界與弱引用分工**：
  * `OwningHandle<T>`：宣告單一子物件擁有權插槽（邊緣拓撲），形成清晰的父子持有樹。業務圖內部雙向與互指關聯亦放膽使用，由背景 `CycleCollector` 自動偵測孤島並非同步消化。
  * `OwningContainerHandle`：宣告動態子物件容器（如背包道具清單、子節點陣列）。
  * `UnboundHandle<T>`：純旁觀者句柄（只記住對方號碼，絕不干涉生死）。專為「外掛隨時卸載防卡死」、「UI 視窗暫時看一眼」等旁路觀察設計。要使用時先確認對方是否還在（`LockAndAcquire()`），若對方已離職或被銷毀則自動傳回空值並清理記錄，絕不卡住對方的釋放流程。
  * `OuroPtr<T>`：棧上或全域根引用守衛（Root Edge），內部自動調用 `ork_acquire_object_pointer` 與讀寫鎖，保證在活躍存取期間物件絕不被脫水或物理銷毀。

### 2. 記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)
* **儲存狀態四態模型**：`UnsavedNew(0)`、`Clean(1)`、`Dirty(2)`、`Dehydrated(3)`。
* **無感自動脫水**：當記憶體達到上限時，由脫水器（如內建的 `OuroLRUAutoDehydrator`）選出最久未被存取的冷物件，將其 Payload 序列化落盤至持久化驅動（`IStorageDriver`），隨後釋放實體記憶體，保留控制區塊墓碑。
* **透明按需復水**：當程式再次存取已脫水物件時，ControlBlock 透過預先註冊的回呼透明地重新配置空殼實體、讀取藍圖還原狀態並重新綁定指標。呼叫端完全無須編寫任何載入代碼！

### 3. 三層架構與邊界隔離原則
OuroKore 嚴格劃分三大權限層級：
1. **主程式宿主層（Host Application）**：透過 `HostContext` 獨佔進程生命週期、執行緒池注入、自動脫水策略與特權維護功能。
2. **組件與插件層（Component / Plugin）**：僅能使用受管物件、安全指標、Handle 拓撲與讀寫鎖，物理隔絕所有破壞性特權。
3. **底層純 C ABI（Cross-Language FFI）**：所有底層操作以純整數狀態碼、HandleID 與 C 函式指標封裝，100% 杜絕 C++ 例外跨動態庫逃逸，為未來綁定 C#、Rust、Python 提供完備基礎。

### 4. 基礎模組與通用設施 (Base Foundation & Utilities)
除了核心物件圖託管外，`ourokore_base` 模組提供了一套現代、高效能且零依賴上層核心的基礎工具庫：
* **現代樹狀容器與文字 DSL (Tree & TreeIO)**：單一容器雙模態統合（保序 vector + 雜湊 map），資料純度推導形態（物件 `{}` 與陣列 `()`），百萬層顯式堆疊防爆棧。
* **動態模組載入器 (DynamicLibrary)**：禁絕手動卸載、生命週期反向錨定、弱引用晉升與非同步離棧延遲卸載。
* **現代編譯期雜湊 (Hash.hpp)**：C++20 `constexpr` FNV-1a（TypeID 唯一標準）、CRC32、MurmurHash3、HashCombine 與字面量。
* **並行排程與同步設施 (ThreadPool, Semaphore, Event, ThreadSafeQueue)**：固定與動態彈性伸縮執行緒池、MPMC 阻塞佇列、計數信號量與跨平台事件原語。
* **全域 UTF-8 標準輔助 (utf8.hpp)**：零拷貝字串視圖轉換與 Windows Unicode `W` 邊界隔離。

---

## 📚 使用手冊章節導引 (Manual Navigation)

本使用手冊由淺入深分為以下章節：
1. **[01. 系統概述與架構哲學 (Introduction)](01_introduction.md)**：系統願景、核心心智模型與架構原則。
2. **[02. 5 分鐘快速上手 (Quickstart)](02_quickstart.md)**：從零建立宿主、受管物件、存檔與文字 DSL。
3. **[03. 領域物件設計與型別系統 (Domain Object Design)](03_domain_object_design.md)**：`Subclass` 樣板、TypeID 墓碑長存、安全向上/向下轉型與 X-Macro 安全 Proxy。
4. **[04. 智慧 Handle 系統與拓撲關係 (Handles & Topology)](04_handles_and_topology.md)**：`OwningHandle`、`UnboundHandle`、`OuroPtr` 防逃逸成員指標呼叫與背景循環回收。
5. **[05. 脫水換頁與儲存驅動 (Dehydration & Storage)](05_dehydration_and_storage.md)**：四態生命週期、LRU 自動換頁、透明按需復水與儲存驅動實作。
6. **[06. 宿主生命週期與特權控制 (Host Lifecycle)](06_host_lifecycle.md)**：`HostContext` 特權獨佔、優雅關閉、背景執行緒排空與自救脫水調度。
7. **[07. API 規格參考手冊 (API Reference)](07_api_reference.md)**：全體公開類別、樣板、方法與函式介面速查。
8. **[08. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)](08_tree_and_dsl.md)**：階層容器、雙模態統合、非遞迴狀態機反序列化與緊湊格式。
9. **[09. 基礎工具庫指南 (Base Foundation & Utilities)](09_base_utilities.md)**：動態庫載入、編譯期雜湊、並行執行緒池、同步原語與全域 UTF-8 工具。
