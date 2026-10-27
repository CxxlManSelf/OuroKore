# 01. OuroKore 系統架構設計規範 (System Architecture RFC)

本文件定義 OuroKore 核心系統的領域無關架構規範，任何語言（C++、Rust、C#、Go 等）在實作 OuroKore 相容核心時，必須嚴格遵守以下心智模型、狀態轉換與演算法語意。
手冊中所有流程與演算法均以**中性偽代碼（Language-Agnostic Pseudocode）**表達。

---

## 🏛️ 1. 三層邊界隔離架構 (Three-Tier Boundary Architecture)

OuroKore 系統嚴格劃分三大權限與職責邊界，貫徹「**C ABI 為底，各語言 Wrapper 為糖**」之核心哲學：

```
+----------------------------------------------------------------------+
|                     主程式層 (Host Application)                      |
|  - 進入點持有唯一特權上下文 (HostContext)                             |
|  - 進程級生命週期控制 (Init / Shutdown / Reset)                       |
|  - 基礎設施注入：持久化驅動 (StorageDriver)、自動脫水換頁器、執行緒池  |
|  - 全域 GC 排程、延遲銷毀排空 (FlushStorage / FlushDeferredDeletions)  |
+----------------------------------------------------------------------+
                                   │
                                   ▼
+----------------------------------------------------------------------+
|                  第三方插件層 (Plugin / Component)                   |
|  - 領域物件繼承受管基底 (OuroObject)，嚴禁存取底層控制區塊裸指標       |
|  - 拓撲邊緣透過 OwningHandle、UnboundHandle、OwningContainerHandle 表達 |
|  - 棧上受管指標 OuroPtr，執行緒安全讀寫鎖 OuroReadLock / OuroWriteLock |
|  - 零特權防線：物理隔絕所有進程級控制 API 與破壞性測試介面             |
+----------------------------------------------------------------------+
                                   │
                                   ▼
+----------------------------------------------------------------------+
|                   純 C ABI / 核心運行時層 (core.dll)                 |
|  - 純整數狀態碼 (Int32)、全域識別碼 HandleID (UInt64)、純 C 回呼        |
|  - 控制區塊 (ControlBlock) 託管物件元資料、引用代數與讀寫併發鎖       |
|  - 異常安全邊界：核心導出函式保證攔截所有例外，防範跨動態庫崩潰         |
+----------------------------------------------------------------------+
```

---

## 🔄 2. 控制區塊與生命週期狀態機 (ControlBlock & Lifecycle State Machine)

所有領域物件均由唯一的 64 位元識別碼 `HandleID` 與底層控制區塊（`ControlBlock`）託管。

### 2.1 儲存狀態四態模型 (StorageState)
物件在其生命週期內處於以下四種儲存狀態之一：

| 狀態列舉值 | 名稱 | 定義與行為特徵 |
| :--- | :--- | :--- |
| `0` | **UnsavedNew** | 物件新建構，記憶體中存在 Payload，尚未在持久化儲存介質中建檔。 |
| `1` | **Clean** | 物件記憶體 Payload 與持久化儲存資料完全一致，未發生任何屬性修改。 |
| `2` | **Dirty** | 物件記憶體 Payload 已被寫入修改，與持久化儲存不同步，換頁脫水時必須先序列化落盤。 |
| `3` | **Dehydrated** | 物件記憶體 Payload 已被釋放，僅保留 ControlBlock 墓碑於全域註冊表；存取時透明觸發復水。 |

```
               [建立 CreateObject]
                       │
                       ▼
              +------------------+
              | 0: UnsavedNew    |
              +------------------+
                 │            ▲
     [首次落盤]  │            │ [復水 Rehydrate]
                 ▼            │
         +--------------+     │
  ┌─────>|   1: Clean   |─────┼──────┐
  │      +--------------+     │      │
[落盤]      │            │     │   [脫水釋放 Payload]
  │   [寫入標髒]    │     │      │
  │         ▼            │     │      ▼
  └──────+--------------+     │  +------------------+
         |   2: Dirty   |─────┘  |  3: Dehydrated   |
         +--------------+        +------------------+
```

### 2.2 三維引用計數代數體系 (Reference Algebra)
每個 `ControlBlock` 維護三個獨立的計數器，精確定義物件的生命週期邊界：

1. **內部強入邊度 (`strong_in_count`)**：
   - 由領域圖內部其他物件的強引用插槽（`OwningHandle` 或 `OwningContainerHandle`）持有。
   - 代表物件圖內部的強擁有權邊緣。
2. **外部根節點計數 (`root_count`)**：
   - 由棧上守衛或全域活躍句柄（`OuroPtr`）持有。
   - 代表正處於活躍存取中，**在此計數大於 0 時，核心絕對禁止脫水或銷毀**。
3. **無繫結引用計數 (`unbound_count`)**：
   - 由跨模組弱引用句柄（`UnboundHandle`）持有，不計入圖拓撲入邊（In-degree = 0）。
   - 專為動態插件模組卸載防釘死與旁路觀察設計。

#### 物件銷毀與墓碑清理準則：
* **進入墓碑態 (Tombstone)**：當 `strong_in_count == 0` 且 `root_count == 0` 時，若 `unbound_count > 0`，物件 Payload 立即銷毀，但保留 ControlBlock 墓碑，阻斷提升並觸發惰性修剪。
* **徹底釋放 (Complete Free)**：當 `strong_in_count == 0`、`root_count == 0` 且 `unbound_count == 0` 時，ControlBlock 自全域註冊表徹底註銷並釋放記憶體。

---

## 🧬 3. 循環參照收集演算法規範 (Cycle Collection Algorithm)

> 🗺️ **互動概念圖導覽**：
> 強烈建議點擊開啟 **[🗺️ 循環參照收集器與逆向圖論走訪全景概念圖](../manual/conceptmaps/cycle_collector_map.html)**（支援平滑拖曳與滾輪縮放），直觀掌握「單一背景執行緒架構」、「Upstream BFS 逆向快照走訪」、「HandleID 升冪排序防哲學家死鎖」與「外科手術式靜音斷鏈 (Silent Unbind)」的四層防禦全景！

OuroKore 放棄了傳統複雜易錯的多階段試探性減計數染色法，採用了現代化、極高併發且零死鎖的**「非同步逆向溯源走訪 (Upstream BFS) + 四層防禦二階段確認 (Two-Phase Lock & Verify) + 外科手術式靜音斷鏈 (Silent Unbind)」**演算法，專職負責將無根循環參照孤島（Cyclic Garbage）進行純物理銷毀。

---

### 3.1 核心架構約束與設計定理

1. **嚴格單一背景執行緒架構 (Single Background Worker Thread)**：
   - **定理**：圖結構之反向走訪在多執行緒併發下必然遭遇不同執行緒以反向順序索取節點鎖，引發典型的「哲學家就餐死鎖（Dining Philosophers Problem）」，且伴隨嚴重的 CPU 快取顛簸。
   - **解法**：核心強制規定全進程僅由唯一背景執行緒（`WorkerLoop`）執行孤島分析與破壞，徹底消滅走訪死鎖與爭搶。
2. **純物理銷毀定理 (Physical Destruction Only)**：
   - 循環回收器處理的孤島在語意上已被全域拋棄，**嚴禁在收集過程中觸碰脫水（Dehydration）或藍圖打包（PackBlueprint）**，直接純記憶體釋放，零多餘 I/O。
3. **序號同步屏障 (Sequence Barrier: `CollectCyclesExplicit`)**：
   - 供單元測試或宿主顯式調度，外部調用端僅透過序號屏障委託單一背景執行緒消化排隊批次，調用端在條件變數上安全阻塞等待，徹底消滅跨執行緒 Data Race。

---

### 3.2 演算法處理管線 (Pipeline)

#### 階段一：嫌疑犯 O(1) 入隊與批次去重 (Suspect Ingestion & Deduplication)
- 當調用 `ork_unregister_edge` 斷開強擁有邊緣時，若目標物件 `StrongCount > 0`（代表仍有其他引用或內部互指殘留），透過 CAS 原語原子搶入 `m_in_suspect_queue` 標記。
- 搶入成功者推入 `m_suspect_queue` 並發送信號喚醒背景清潔工；若收集器已停機，復位標記並早退，連互斥鎖都不必爭搶。
- 收集器被喚醒後，以 `current_batch.swap(m_suspect_queue)` 在微秒級鎖內整批取出，並在批次內透過 `unordered_set` 去重，消除同環多節點引發的冗餘重複走訪。

#### 階段二：雙重存活審查 (Double-Check Guard)
- 提取嫌疑犯時，若物件已被標記為正在拆解（`m_is_destructing == true`）或其強計數已歸零（`StrongCount == 0`，代表已被業務執行緒或 `DeferredDeleteQueue` 接管），立即重置標記並安全略過。

#### 階段三：微秒級快照與逆向圖論走訪 (Upstream BFS & Micro-Snapshot)
- 從嫌疑犯出發，依據 `ControlBlock::m_owners` 逆向名冊逐級向上廣度優先回溯（BFS）：
  1. **微秒級快照 (Micro-Snapshot)**：僅在極短時間獲取節點之 `m_owners_mutex`，複製其快照後立即釋放鎖，絕不在持鎖狀態下遞迴走訪。
  2. **外部存活根早退 (Root Early Exit)**：
     - 若節點入度為 0（非閉環依賴）；
     - 或觸及 `ORK_ROOT_ID`（代表有棧上 `OuroPtr` 根引用活躍持有）；
     - 或上游 owner 為外部非受管節點（Registry 查無 ControlBlock）；
     - 或上游 owner 正處於兩階段構造預留態（`m_is_reserved == true`）；
     - **✨ 立即判定為外部存活！立刻中斷 BFS 早退，絕不破壞該物件圖！**
  3. **visited 集合單次走訪防環**：基於即時快照嚴格審查，走訪結束即銷毀集合，無跨次過期快取毒化風險。

#### 階段四：四層防禦協議之孤島二階段確認 (DestructIsland Four-Tier Protocol)
若 Upstream BFS 回溯完成且未觸及任何外部存活根，該連通分量初步判定為死結孤島，隨後啟動嚴格的四層防禦協議進行二階段確認：
1. **第一層防禦：HandleID 升冪排序**：
   - 將孤島全體節點按 64 位元 `HandleID` 數值由小到大嚴格升冪排序（`std::sort`），徹底杜絕多鎖依序鎖定時的反向死鎖。
2. **第二層防禦：弱引用 Pin 守衛生命週期釘住 (Pinning)**：
   - 遍歷升冪清單，為每個節點獲取 `ControlBlockPinGuard`。在釘住期間各節點 `weak_count >= 1`，保證併發業務執行緒絕無法銷毀任何一個 ControlBlock 墓碑，100% 杜絕釋放後使用（UAF）。
3. **第三層防禦：全體鎖定二次複查 (Two-Phase Lock & Verify)**：
   - 依升冪順序逐一獲取全體孤島節點之 `m_owners_mutex`。
   - 二次檢查孤島全體成員的存活態與 `m_owners`：若孤島內任何節點之 owner 位於孤島集合之外（例如在排隊間隙有其他執行緒併發加邊連回 Root 或外部），**孤島已宣告復活，收集器立即解開所有鎖並全盤放棄拆解！**
4. **第四層防禦：原子標記 Destructing 態**：
   - 經複查確認為封閉孤島後，對全體孤島節點原子標記 `m_is_destructing = true`。
   - 此標記徹底封鎖外部任何併發加邊請求，並阻止殭屍物件死者甦醒。

#### 階段五：外科手術式靜音斷鏈與延遲銷毀交棒 (Silent Unbind & Deferred Deletion)
1. 解開所有 `m_owners_mutex` 鎖。
2. 逐一調用 `Registry::UnregisterEdge(owner, target, silent=true)`，以靜音模式切斷孤島成員間的相互強擁有邊緣。
   - `silent=true` 靜音模式保證斷鏈時不會再次向嫌疑犯佇列推入多餘項目。
3. 邊緣切斷後，孤島節點的強引用計數（`StrongCount`）自然遞減至 0。
4. 核心自動將其移交至 `DeferredDeleteQueue`（延遲銷毀佇列），由平級佇列平行壓平解構釋放肉體記憶體。
5. **收集器本身絕不卡在解構耗時上**，移交完畢後立即回頭處理下一批嫌疑犯，保證進程級背景清潔的高吞吐與低延遲！

---

## 🛡️ 4. OOM 被動自救與 Passkey 權杖防禦機制

1. **記憶體換頁自救**：當第三方插件配置實體記憶體遭遇記憶體耗盡（OOM）時，核心具備被動換頁釋放冷物件之能力。
2. **雙重防護原則**：
   - **編譯期權杖防禦 (Passkey Pattern)**：底層救援函式強制要求合法構造樣板專屬權杖，外部任何外掛業務程式碼無法直接實例化，杜絕第三方插件主動發起全域記憶體調度。
   - **執行期情境驗證**：核心在觸發脫水自救前，校驗當前 HandleID 是否正處於合法預留或脫水重建狀態，非合法情境之調用一律拒絕。


---

## 🧩 5. 動態模組架構與生命週期反向錨定規範 (Life-Bound Retention RFC)

在微核心與外掛（Plugin / MODULE）架構中，動態庫載入器必須保證執行期代碼段與虛擬函式表（vtable）的絕對有效性。

### 5.1 禁絕手動卸載定理 (No Manual Unload Invariant)
* **定理**：載入器介面**嚴禁提供任何手動卸載函式**（如 `Unload()`）。
* **公理**：在多執行緒與非同步任務圖中，任何執行緒無法預知其他執行緒是否仍有閉包正在執行外掛虛擬函式。提前手動卸載動態庫必然引發作業系統將代碼段解除映射（Unmap），導致記憶體訪問違規（Access Violation / SIGSEGV）。

### 5.2 生命週期反向錨定模型 (Life-Bound Retention Model)
動態模組之卸載完全由受管物件之生命週期計數自然驅動：

```text
Structure ModuleControlBlock:
    system_handle: NativeModuleHandle
    use_count: AtomicUInt64
    is_first_loaded: Boolean
    is_permanent_resident: Boolean
    cleanup_hooks: List<Function>
    terminal_shutdown_hook: Nullable<Function>
    post_unload_hooks: List<Function(String)>
End Structure

Procedure RetainModule(block):
    block.use_count.FetchAdd(1)
End Procedure

Procedure ReleaseModule(block):
    If block.use_count.FetchSub(1) == 1 Then
        TriggerModuleUnloadWorkflow(block)
    End If
End Procedure
```

* **弱引用晉升機制 (Weak Dynamic Module)**：
  提供無所有權之弱觀察者。呼叫端可在放棄初始強引用後，隨時透過原子性 `Lock()` 嘗試晉升；若模組已卸載則安全回傳無效句柄，杜絕懸空指標。

---

## 🤝 6. 外掛非同步善後握手協定與終端常駐模式 (Async Handshake & Resident RFC)

### 6.1 主執行緒 0ms 延遲非同步握手協定 (Async Shutdown Handshake)
為解決外掛在卸載收尾時執行耗時 I/O（磁碟落盤、GPU 緩衝區釋放、網路關閉）導致主執行緒卡頓之問題，核心定義非同步握手狀態機：

```text
[主程式執行緒]                               [背景卸載等待線程]                         [外掛動態庫]
      │                                             │                                       │
釋放最後引用 (Release)                              │                                       │
      │ ─── 移交背景線程 (0ms 立即返回繼續運作) ──> │                                       │
      │                                             │ ─── 調用非同步收尾函式(附帶 Token) ──>│
      │                                             │                                       │ 執行耗時落盤與善後...
      │                                             │ ─── 進入條件變數阻塞等待 ────         │
      │                                             │                             │         │
      │                                             │ <── 呼叫 Token.OnReady() ───│ 善後完畢，主動握手喚醒！
      │                                             │     (握手成功，喚醒背景線程)           │ (代碼段安全終止)
      │                                             │                                       
      │                                             │ 物理調用 FreeLibrary / dlclose
      │                                             │ 觸發 PostUnloadHooks 通知主程式
```

### 6.2 外掛終端收尾保證 (Terminal Shutdown Guarantee)
外掛可註冊終端收尾回呼（Terminal Shutdown Symbol），保證於外掛所屬的所有活體物件全部釋放完畢後**最後執行一次**。

### 6.3 外掛拒絕卸載轉為常駐模式與生命週期接管 (Resident Mode & Ownership Handover)
* 若外掛之終端收尾函式執行後傳回**非零代碼**（或 bool 回傳 `false`），核心將其識別為「外掛主動拒絕卸載」。
* 核心立即將其原生句柄登記進全域常駐表（`resident_table`），取消物理卸載調用，使該動態庫安全常駐於記憶體，杜絕外掛因全域回呼未清理而崩潰。
* **重用與所有權交接機制（Transfer of Ownership）**：當日後再次調用 `load()` 重新載入該模組時，系統在 `resident_table` 命中後，**立即將其自常駐表中移除**，並為其建立全新的控制區塊接管該句柄；若日後新控制區塊解構且外掛同意卸載，物理卸載後常駐表保證無殘留，杜絕懸空野指標。

### 6.4 離棧延遲卸載防自毀保護 (Deferred Unload Defense)
若模組內部的某個回呼自身釋放了該模組的最後一個引用計數，核心強制將 `FreeLibrary` 操作延遲至非同步背景離棧執行緒執行，防止模組在呼叫棧仍在該動態庫內部時物理卸載自身的代碼段導致致命崩潰。

---

## 🆔 7. 次世代身分標識與維度解耦架構 (Next-Gen Identity & Dimensional Decoupling RFC)

### 7.1 圖論自由與無環 Merkle DAG 的維度解耦公理
在次世代分散式與內容定址（CAS）標準中，純粹的 CID 資料流在數學上必然是有向無環圖（Merkle DAG）；然而，在真實業務執行期，物件間的雙向擁有與循環參照（Cyclic References）是不可剝奪的自由。

OuroKore 透過**「維度解耦（Dimensional Decoupling）」**完美調和此範式衝突：
* **儲存與規格層（次世代維度）**：資料不可變，以 SHA-256 Multihash (**CIDv1**) 定址，保證防竄改、去重與 P2P 傳播。
* **執行期記憶體層（OuroKore 核心維度）**：放開拘束，賦予物件在記憶體中自由組網、雙向參照的絕對自由；由核心原生的原子讀寫鎖、CycleCollector 與 DeferredDeleteQueue 在背後安全兜底。

### 7.2 雙軌身分識別體系
1. **執行期記憶體身分證**：**100% 唯一只認 64 位元整數 HandleID**。
   - 拒絕在核心登錄表或 ControlBlock 中維護字串或 128 位元 UUID，以維持最高 CPU 快取效率與原子操作。
2. **規格與持久化標籤**：**100% 唯一只認 CID**。
   - 徹底廢除全域 UUID 映射機制與相關系統庫依賴。
3. **網關隔離**：次世代零件能力由外掛驅動 **OrkGatewayDriver** 實現，核心引擎保持零更動。

### 7.3 次世代基礎工具鏈架構賦能 (Base Utilities Foundation for Gateway)
為讓 `OrkGatewayDriver` 外掛能以最高效能與最高安全性完成零件管理，`ourokore_base` 基礎庫在架構底層提供了 4 大專屬工具基石（純 C++23、零外部依賴、純 CPU 運算）：

```
                 [ OrkGatewayDriver 次世代網關 ]
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
  [ Endian.hpp ]          [ Sha256.hpp ]         [ FileUtils.hpp ]
  強制 Little-Endian    純 CPU Multihash CID    Crash-Resistant 原子落盤
  (跨 CPU 決定性佈局)     (0x12 0x20 + SHA-256)   (Flush + MoveFileExW)
                                │
                                ▼
                         [ PathUtils.hpp ]
                      Vendor/[SubPath/...]ModuleStem 標籤
                      與沙盒目錄穿越 (../) 攔截防禦
```

1. **跨架構決定性佈局 (`Endian.hpp`)**：
   - 確保所有不可變規格藍圖在寫入二進位串流與計算雜湊前，強制採用 Little-Endian。消弭跨架構差池，保證跨平台產生的 CID 100% 吻合。
2. **純 CPU 零依賴 CAS 內容定址 (`Sha256.hpp` / `CidV1`)**：
   - 符合 RFC 6234，零外部密碼庫依賴（徹底移除 Windows `bcrypt.lib` 與 OpenSSL 鏈結負擔），提供純 CPU 增量與一次性計算，封裝標準 34 位元組 Multihash (`0x12, 0x20 + 32-byte hash`)。
3. **物理級防損毀原子寫入 (`FileUtils.hpp`)**：
   - CAS 內容定址庫要求檔案一旦落盤便永不損毀。工具庫採用三階段物理保證：**「同目錄隨機暫存檔寫入 ➔ 磁碟強制刷盤 (`FlushFileBuffers` / `fsync`) ➔ OS 原生原子替換 (`MoveFileExW` / `rename`)」**。即使遭遇突發斷電，原檔案絕對無損。
4. **階層標籤解析與目錄穿越隔離 (`PathUtils.hpp`)**：
   - 提供 `parse_factory_tag` 嚴格驗證階層式工廠標籤（`Vendor/[SubPath/...]ModuleStem`，如 `OuroKore/xxx/yyy/zzz/GoodPlugin_1.0.0`），並由管理器依平台自動補綴動態庫副檔名，並以 `is_safe_relative_path` 在路徑拼接前嚴密阻截 `../`、`..\\` 與磁碟機代號等穿越企圖，徹底捍衛宿主檔案沙盒。

### 7.4 宿主特權能力裝配與 WASM 沙盒標準介面 (IPartProvider & Capability Pattern)
為解決 WebAssembly (WASM) 沙盒環境下「WASM 模組如何安全獲取其所需的依賴 WASM 零件」之核心命題，OuroKore 確立了「**能力導向（Capability-Based）的兩階解耦架構**」：

```
  [ OrkGatewayDriver ] ──(注入)──> [ HostContext (特權主控) ]
                                            │
                                            │ CreatePartProvider() (最小特權裁切)
                                            ▼
                                   [ IPartProvider 抽象標準 ]
                                            │
                       ┌────────────────────┴────────────────────┐
                       ▼                                         ▼
            [ 主程式原生調用 ]                          [ 傳遞給 WASM 沙盒 ]
         (Direct Native Access)                    (WASM Import / Object Capability)
```

1. **核心標準介面 (`IPartProvider`) 與外掛解耦**：
   - `IPartProvider` 為 OuroKore 官方 SDK 標準契約，不依賴任何特定外掛實作。
   - 包含兩大標準能力：
     * **`CreateNewPart(factory_tag, init_params)`**：全新零件創生生成。
     * **`RestorePart(persistence_id)`**：自 CAS 不可變藍圖（CIDv1）或永續識別碼還原物件。
2. **宿主特權裝配模式 (`HostContext::CreatePartProvider`)**：
   - 宿主以最高特權持有 `HostContext`，將動態載入之 `OrkGatewayDriver` 注入，裝配產出安全的 `std::shared_ptr<IPartProvider>`。
   - 產出之介面**嚴格剔除**關閉核心、強制 GC、儲存驅動切換等進程級特權，符合最小特權原則 (ISP)。
3. **WASM 跨沙盒安全閉環**：
   - 宿主將 `IPartProvider` 作為受管能力權柄（Capability Handle）傳遞給 WASM 實例。
   - WASM 模組呼叫 `provider->CreateNewPart` 索求相依模組時，由宿主代為自 CAS 檢索二進位流並實例化，再將受管 `HandleID` 返還。
   - WASM 模組代碼 100% 保持純粹沙盒計算，無須碰觸實體檔案系統或持有載入特權，達成最高規格的零信任架構防禦。
