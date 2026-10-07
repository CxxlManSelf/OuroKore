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

OuroKore 採用非同步 Bacon-Rajan 試探性減計數圖論演算法（Trial Deletion），安全偵測由強擁有權邊緣形成的孤島環路。
以下使用**中性結構化演算法偽代碼（Neutral Algorithmic Pseudocode）**完整定義其處理邏輯：

```text
Algorithm: BaconRajanCycleCollection

Global State:
    SuspectQueue: Queue of Reference<ControlBlock>
    DeferredDeleteQueue: Queue of Reference<ControlBlock>

Procedure RegisterSuspect(node):
    If node.color != PURPLE Then
        node.color = PURPLE
        SuspectQueue.Push(node)
    End If
End Procedure

Procedure CollectCycles():
    // 階段一：試探性減去內部邊緣 (MarkGray)
    For Each suspect In SuspectQueue Do
        If suspect.color == PURPLE Then
            MarkGray(suspect)
        Else
            SuspectQueue.Remove(suspect)
        End If
    End For

    // 階段二：掃描根可達性 (ScanRoots)
    For Each suspect In SuspectQueue Do
        Scan(suspect)
    End For

    // 階段三：孤島斷鏈與延遲回收 (CollectWhite)
    For Each suspect In SuspectQueue Do
        SuspectQueue.Remove(suspect)
        CollectWhite(suspect)
    End For
End Procedure

Procedure MarkGray(node):
    If node.color != GRAY Then
        node.color = GRAY
        For Each child In node.RegisteredOutgoingEdges Do
            child.local_in_count = child.local_in_count - 1
            MarkGray(child)
        End For
    End If
End Procedure

Procedure Scan(node):
    If node.color == GRAY Then
        If node.local_in_count > 0 Or node.root_count > 0 Then
            ScanBlack(node)
        Else
            node.color = WHITE
            For Each child In node.RegisteredOutgoingEdges Do
                Scan(child)
            End For
        End If
    End If
End Procedure

Procedure ScanBlack(node):
    node.color = BLACK
    For Each child In node.RegisteredOutgoingEdges Do
        child.local_in_count = child.local_in_count + 1
        If child.color != BLACK Then
            ScanBlack(child)
        End If
    End For
End Procedure

Procedure CollectWhite(node):
    If node.color == WHITE And Not node.IsInDeferredDeleteQueue Then
        node.color = BLACK
        // 外科手術斷鏈：原子清除所有出邊，觸發計數歸零並移交延遲銷毀隊列
        node.ReleaseAllOutgoingEdges()
        DeferredDeleteQueue.Push(node)
        For Each child In node.RegisteredOutgoingEdges Do
            CollectWhite(child)
        End For
    End If
End Procedure
```

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

### 6.3 外掛拒絕卸載轉為常駐模式 (Permanent Resident Mode)
* 若外掛之終端收尾函式執行後傳回**非零代碼**，核心將其識別為「外掛主動拒絕卸載」。
* 核心立即將其標記為 `is_permanent_resident = True`，取消物理卸載調用，使該動態庫安全常駐於記憶體直至進程終止，杜絕外掛因全局註冊回呼無法清理而崩潰。

### 6.4 離棧延遲卸載防自毀保護 (Deferred Unload Defense)
若模組內部的某個回呼自身釋放了該模組的最後一個引用計數，核心強制將 `FreeLibrary` 操作延遲至非同步背景離棧執行緒執行，防止模組在呼叫棧仍在該動態庫內部時物理卸載自身的代碼段導致致命崩潰。

---

## 🌲 7. 基礎工具層：樹狀物件節點與異質階層架構 (Heterogeneous Object Nodes & Tree Topology RFC)

OuroKore 基礎工具庫（`ourokore_base`）提供無相依之現代高效能樹狀物件節點設施（`TreeNodeBase<D>`、`TreeNode<T>`）與文字 DSL 串流器（`TreeIO`）：

### 7.1 物件節點本體論與異質多型階層
* **節點即為物件本體（The Node IS The Object）── 徹底廢除「容器」概念**：
  樹狀結構並非被動裝載資料的皮囊容器（Container），而是實體的**物件節點（Object Node）**。透過 CRTP 樣板基底 `TreeNodeBase<D>`，領域實體自身即為具備完整 C++ 記憶體佈局與業務邏輯的領域節點，基底僅負責注入階層拓撲、名稱索引與並發鎖機制。
* **原生異質物件節點階層（Heterogeneous Object Nodes）**：
  定義共通多型基底節點類別（例如 `class BaseNode : public TreeNodeBase<BaseNode>`）後，任何衍生領域節點（如 `MonsterNode`、`ItemNode` 等）均可在同一個父節點序列中並存管理。
* **C++20 Concept 強型別零手動轉型直出**：
  `AddChild<SubT>`、`PushElement<SubT>`、`InsertBefore<SubT>` 等介面受 `std::derived_from<SubT, D>` 編譯期約束，直接回傳強型別 `std::shared_ptr<SubT>`，呼叫端享有零手動轉型（Zero-Casting）極致便利。

### 7.2 衍生類別職責邊界與嚴格私有封裝 (Strict Encapsulation Invariant)
* **衍生類別專注領域資料處理**：衍生類別僅負責業務欄位與行為方法，**絕不直接碰觸底層內部拓撲**。
* **拓撲成員全面私有化 (`private`)**：底層所有成員變數（`m_elements`、`m_nameMap`、`m_parent`、`m_self`、`m_treeMutex` 等）全面收斂為 `private`，節點操作一律透過公開 API，杜絕繞過鎖修改拓撲的並發競態。
* **受保護基底建構子 (`protected`)**：`explicit TreeNodeBase(name)` 宣告為 `protected`，僅供衍生類別構造自身時調用，外部禁止實例化裸基底。

