# -*- coding: utf-8 -*-
"""
生成 specs/technical/ 目錄下所有技術手冊與跨語言標準規範手冊 (build_specs_technical.py)
為 OuroKore 框架提供語言無關（Language-Agnostic）的權威架構與資料二進位協定規範。
本手冊中所有演算法與介面表達一律採用中性語言（Neutral Pseudocode & IDL），
確保他人或其他程式語言（如 Rust、C#、Go 等）能精確同構實作並 100% 互通二進位資料。
"""
from pathlib import Path

def generate_technical_specs(specs_dir: Path):
    tech_dir = specs_dir / "technical"
    tech_dir.mkdir(parents=True, exist_ok=True)

    # =========================================================================
    # 01_system_architecture.md
    # =========================================================================
    (tech_dir / "01_system_architecture.md").write_text('''# 01. OuroKore 系統架構設計規範 (System Architecture RFC)

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
''', encoding="utf-8")

    # =========================================================================
    # 02_binary_protocols.md
    # =========================================================================
    (tech_dir / "02_binary_protocols.md").write_text(r'''# 02. 二進位串流與藍圖打包協議 (Binary Protocols & Wire Format RFC)

本文件詳細定義 OuroKore 的**實體二進位資料串流佈局（Binary Wire Format）**。
任何第三方工具、其他程式語言（C#、Rust、Go、Python）實現的 OuroKore 引擎或解析器，只要嚴格遵循本規格，即可與 C++ 實現版本產生的二進位藍圖或脫水存檔 **100% 互通與雙向讀寫**。

---

## 📐 1. 基礎編碼規範與字節序鐵律 (Endianness Invariant)

1. **字節序 (Endianness)**：
   - 全系統二進位資料流中的所有多位元組整數與浮點數，**一律強制採用 Little-Endian（小端序）**。
   - 若在 Big-Endian 平台上讀寫，實作層必須主動執行位元組反轉轉換。
2. **基元資料型別大小與編碼表**：

| 型別名稱 | 二進位大小 | 編碼與佈局規範 |
| :--- | :--- | :--- |
| `Boolean` | 1 Byte | `0x00`: False，`0x01`: True（非零值均解讀為 True） |
| `Int8 / UInt8` | 1 Byte | 8 位元有號/無號整數 |
| `Int16 / UInt16` | 2 Bytes | 16 位元有號/無號整數，Little-Endian |
| `Int32 / UInt32` | 4 Bytes | 32 位元有號/無號整數，Little-Endian |
| `Int64 / UInt64` | 8 Bytes | 64 位元有號/無號整數，Little-Endian |
| `HandleID` | 8 Bytes | 等同 `UInt64`，全域唯一物件識別碼，Little-Endian |
| `Float32` | 4 Bytes | IEEE 754-2008 單精度浮點數，Little-Endian |
| `Float64` | 8 Bytes | IEEE 754-2008 雙精度浮點數，Little-Endian |
| `String` | 動態 (4 + N) | **長度前綴 UTF-8 字串**：先寫入 4 位元組 `UInt32 len`，接續 `len` 個位元組的 UTF-8 資料，**無** null 結尾字元。若 `len == 0`，僅佔用 4 位元組且無後續負載。 |

---

## 📦 2. 兩段式藍圖二進位資料流格式 (Two-Segment Wire Layout)

每一個 OuroKore 物件的二進位藍圖串流，嚴格依序由**兩大段落**組成：

```
+-------------------------------------------------------------------------+
|                  段落一：純屬性區 (Pure Payload KV Segment)              |
|  [Property 1: Key + Value] [Property 2: Key + Value] ... [Property M]  |
+-------------------------------------------------------------------------+
                                    │
                                    ▼
+-------------------------------------------------------------------------+
|                 段落二：拓撲邊緣名冊區 (Edge Roster Segment)             |
|  [edge_count: UInt32]                                                   |
|    - Slot 1: [slot_name: String] [target_count: UInt32] [HandleID * N]  |
|    - Slot 2: [slot_name: String] [target_count: UInt32] [HandleID * K]  |
|    ...                                                                  |
+-------------------------------------------------------------------------+
```

### 2.1 段落一：純屬性資料區 (Pure Payload KV Segment)
物件內部依序寫入自訂欄位。每個屬性的二進位封裝結構如下：

```
+-------------------------------+-----------------------------------+
| 鍵名長度 (UInt32, 4 Bytes)    | 鍵名內容 (UTF-8 bytes, key_len B) |
+-------------------------------+-----------------------------------+
| 屬性數值二進位負載 (Value Payload, 長度視型別而定)                |
+-------------------------------------------------------------------+
```

* **字串屬性值**：以長度前綴格式寫入（`UInt32 val_len` + UTF-8 bytes）。
  * 支援跨語言標準字串與現代 C++20 原生 `std::u8string`、`char8_t*`、`string_view`。
* **數值屬性值**：直接以原生 Little-Endian 位元組寫入對應大小（如 `Int32` 佔 4 位元組）。
* **防禦性約束**：
  1. **重複鍵阻斷 (Duplicate Key Guard)**：在同一物件序列化串流中，若出現相同之 Key 名稱，解析端必須立即中斷（Fail-Fast 拋出重複鍵異常）。
  2. **鍵名序列匹配驗證 (Key Mismatch Guard)**：反序列化讀取時，讀出的 Key 必須與期望名稱完全一致，否則視為串流版本失配或損毀。
  3. **裸指標位址封鎖 (Raw Pointer Prohibition)**：二進位資料流嚴格禁止將任何記憶體指標位址（如物件指標或裸指標）以 POD 形式寫入串流；實作端必須在編譯期（例如透過 `static_assert`）或執行期檢測並強制拒絕，防止反序列化時產生懸空指標崩潰。

### 2.2 段落二：拓撲邊緣名冊區 (Edge Roster Segment)
接續在純屬性資料區之後，由核心自動遍歷物件的插槽（Slot）名冊並寫入：

```
[edge_count: UInt32] (4 Bytes)
  │
  ├─► Slot 0:
  │     [name_len: UInt32] (4 Bytes)
  │     [slot_name: UTF-8] (name_len Bytes)
  │     [target_count: UInt32] (4 Bytes)
  │     [target_ids: UInt64 * target_count] (8 * target_count Bytes)
  │
  ├─► Slot 1:
  │     [name_len: UInt32] ...
  ...
```

* `edge_count`：物件內持有的總插槽數量。
* 對於每一個 Slot：
  * `slot_name`：插槽名稱（長度前綴 UTF-8 字串）。
  * `target_count`：該插槽所關聯的目標物件數量。
  * `target_ids`：連續的 `UInt64` HandleID 陣列。

---

## 🛡️ 3. 反序列化安全邊界常數與兩階段防線 (Safety Invariants)

為防止惡意構造的二進位串流引發記憶體耗盡（OOM）或整數溢位攻擊，所有解析實作必須實施以下門禁：

| 安全常數 | 上限值 | 目的與防禦機制 |
| :--- | :--- | :--- |
| `kMaxEdgeCount` | `100,000` | 單一物件所允許的最大插槽數量，防止巨量 edge_count 迴圈。 |
| `kMaxTargetsPerSlot` | `100,000` | 單一插槽所允許的最大目標 ID 數量，防止容器爆量配置。 |
| `kMaxSlotNameLength` | `1024` Bytes | 單一插槽名稱之最大字元長度。 |

### 兩階段驗證套用原則 (Two-Phase Apply)
在反序列化任何資料串流時，實作端必須遵循：
1. **第一階段（驗證與暫存）**：先在暫存結構中完整解析、驗證串流長度與所有防禦約束。若串流意外截斷（Truncated）或驗證失敗，立即終止並拋出例外。
2. **第二階段（原子套用）**：所有資料驗證無誤後，才正式套用至物件的記憶體屬性與拓撲關係中。保證強例外安全（Strong Exception Safety），絕不留下半套損毀的物件狀態。

---

## 💾 4. 儲存驅動落盤協議 (Storage Driver Wire Contract)

持久化儲存介面是 OuroKore 與實體儲存媒體溝通的二進位契約。以**中性介面描述語言（Neutral IDL）**定義如下：

```text
Interface StorageDriver:
    // 落盤儲存操作：將物件資料以 HandleID 為鍵寫入介質
    Function Save(handle_id: UInt64, data: ByteSequence, size: UInt64) -> Boolean

    // 讀取復水操作：依據 HandleID 讀取物件原始二進位資料
    Function Load(handle_id: UInt64, out_buffer: MutableByteSequence, buffer_size: UInt64, out_actual_size: MutableRef<UInt64>) -> Boolean

    // 刪除實體操作：自儲存介質中永久移除物件資料
    Function Delete(handle_id: UInt64) -> Boolean
End Interface
```

* **Key-Value 格式**：儲存驅動以 `HandleID (UInt64)` 為唯一 Key，以本規範定義的兩段式二進位串流為 Value。
* **跨語言相容性**：無論底層介質採用本機檔案（File Storage）、記憶體（InMemoryStorage）、SQLite 或分散式 KV 資料庫，其儲存的資料二進位 Payload 均完全相同，可直接被不同語言之 OuroKore 核心交換讀取。


---

## 🌲 5. 樹狀結構階層與文字 DSL 串流協議 (Hierarchical Tree & Text DSL Wire Format RFC)

本節定義 OuroKore 階層式容器 `TreeNode` 與文字 DSL 串流 `TreeIO` 的資料交換規範。

### 5.1 資料純度自動推導雙模態 (Data-Driven Morphism)
樹節點本身不儲存形態列舉，形態完全由子節點結構純度於執行期自動推導：
* **物件模式（Object Mode，DSL 界定符 `{}`）**：子節點全體均為具名節點（`child_count == named_child_count`）。
* **陣列模式（Array Mode，DSL 界定符 `()`）**：混入任何無名（匿名）節點（`child_count > named_child_count`）。

### 5.2 四大正交界定符與零等號哲學 (Orthogonal Delimiters)
文字 DSL 採用四個完全正交之語法 Token，等號 `=` 僅為可選裝飾符號：
* `[節點名稱]`：名稱標記。
* `"字串內容"`：Payload 資料（支援 0~255 二進位位元組與轉義字元 `\"`、`\\`、`\n`、`\xHH`）。
* `{具名成員}`：物件區塊。
* `(列表元素)`：陣列區塊。

#### 兩種緊湊傳輸編碼模式 (CompactMode Wire Styles)：
1. **模式 1：格式化排版 (CompactMode::Pretty)**：含標準縮排、空白與換行，供人類閱讀（Allman 風格）。
2. **模式 2：緊湊傳輸 (CompactMode::Compact)**：`[Key]="Value"{[Child]="1"}`，無多餘空格與換行，具名賦值保留等號 `=`。
   - 規範保證：連續具名空節點（如 `[A][B]`）、匿名空元素、物件區塊均 100% 精準對稱還原，單元素反序列化時拓撲身分永不降級脫殼。

### 5.3 註解語法與界定符遮蔽
狀態機原生支援三種風格註解：
* `// 單行註解`（跳至行尾）
* `/* 區塊註解 */`（跳至閉合符 `*/`）
* `# 腳本註解`（跳至行尾）
* **遮蔽保證**：註解內部包含的引號與括號均被狀態機嚴格忽略，不得觸發任何狀態轉移。

### 5.4 顯式堆疊非遞迴 FSM 反序列化演算法 (Non-recursive FSM Deserialization)
反序列化演算法以堆積（Heap）顯式堆疊 `Stack<ParseFrame>` 驅動，呼叫棧（Call Stack）深度恆為 $O(1)$，數學證明巨深文字 DSL 免疫呼叫堆疊溢位（Stack Overflow）：

```text
Structure ParseFrame:
    current_node: NodeHandle
    state: ParserState
    accumulated_name: String
    accumulated_data: String
End Structure

Function DeserializeFromString(dsl_text: String) -> NodeHandle:
    Let root = CreateRootNode()
    Let stack = DynamicStack<ParseFrame>()
    stack.Push(ParseFrame(root, STATE_SEEK_NODE))
    
    Let cursor = 0
    While cursor < dsl_text.Length Do
        Let ch = dsl_text[cursor]
        
        // 略過空白字元與三種註解
        If IsCommentOrWhitespace(ch, dsl_text, cursor) Then
            cursor = SkipCommentOrWhitespace(dsl_text, cursor)
            Continue
        End If
        
        // 狀態機基於 stack.Top() 轉移：
        Match stack.Top().state With
            Case STATE_SEEK_NODE:
                If ch == '[' Then
                    stack.Top().state = STATE_READ_NAME
                Else If ch == '{' Or ch == '(' Then
                    Let child = stack.Top().current_node.AddChild()
                    stack.Push(ParseFrame(child, STATE_SEEK_NODE))
                Else If ch == '}' Or ch == ')' Then
                    stack.Pop() // 顯式出棧，零遞迴返回！
                End If
            Case STATE_READ_NAME:
                // 解析至閉合中括號 ']' 並填入 accumulated_name
            Case STATE_READ_DATA:
                // 解析至閉合雙引號 '"' 並填入 accumulated_data
        End Match
        cursor = cursor + 1
    End While
    
    Return root
End Function
```
''', encoding="utf-8")

    # =========================================================================
    # 03_c_abi_and_memory.md
    # =========================================================================
    (tech_dir / "03_c_abi_and_memory.md").write_text('''# 03. 純 C ABI 規格與記憶體佈局規範 (C ABI & Memory RFC)

本文件定義 OuroKore 動態程式庫核心層（`core.dll` / `libourokore_core.so`）的純 C ABI 規格。
跨語言使用介面（C# P/Invoke、Rust FFI、Python ctypes/cffi、Go cgo）均以此標準符號規格為底座。

---

## 📌 1. ABI 約束與呼叫慣例 (Calling Convention & Invariants)

1. **跨平台呼叫慣例**：所有導出符號均具備跨平台統一呼叫約定（Windows 上採用標準 C 呼叫慣例，全平台二進位相容）。
2. **純整數狀態碼 (Status Code)**：所有函式一律回傳 `Int32` 狀態碼，標準列舉如下：
   - `0`: `ORK_STATUS_OK`（成功）
   - `1`: `ORK_STATUS_ERROR_NOT_FOUND`（物件或句柄不存在）
   - `2`: `ORK_STATUS_ERROR_ALREADY_EXISTS`（物件或識別碼已存在）
   - `3`: `ORK_STATUS_ERROR_INVALID_ARGUMENT`（參數非法或為空指標）
   - `4`: `ORK_STATUS_ERROR_OOM`（記憶體耗盡）
   - `5`: `ORK_STATUS_ERROR_ACCESS_DENIED`（權限不足或違反邊界防護）
   - `6`: `ORK_STATUS_ERROR_IO`（I/O 或序列化錯誤）
   - `7`: `ORK_STATUS_ERROR_UNKNOWN`（未知異常）
3. **零例外逃逸保證 (No Exception Leaks)**：所有導出函式保證攔截所有語言層例外，嚴禁任何例外跨越動態庫邊界引發進程崩潰。
4. **記憶體釋放隔離原則 (Allocator Isolation)**：跨動態模組建立的物件記憶體，必須由註冊的析構回呼在其原始分配器的堆疊中釋放，禁止在核心內部跨模組直接釋放。

---

## 📋 2. 純 C ABI 導出符號規範 (共 37 個導出函式)

以下以**中性二進位介面符號規格（Neutral ABI Specification）**完整定義 37 個導出函式：

### 📦 類別 A：組件生命週期、型別系統與弱引用 (`component_api.h`)

1. `ork_register_object`
   - **符號規格**：`Function ork_register_object(obj: RawPointer, destroy_fn: FunctionPointer, out_id: MutablePointer<UInt64>) -> Int32`
   - **說明**：將新建構之領域物件註冊至全域註冊表，配發唯一的 64 位元 `HandleID`，綁定模組專屬的解構回呼。
2. `ork_register_object_with_type`
   - **符號規格**：`Function ork_register_object_with_type(obj: RawPointer, destroy_fn: FunctionPointer, type_id: UInt64, out_id: MutablePointer<UInt64>) -> Int32`
   - **說明**：將新建構之領域物件註冊至全域註冊表，同時綁定其靜態 64 位元 `TypeID`。
3. `ork_register_type`
   - **符號規格**：`Function ork_register_type(type_id: UInt64, name_utf8: CString, parent_type_id: UInt64) -> Int32`
   - **說明**：向全域型別註冊表登記型別識別碼、UTF-8 類別名稱與父類別關係（構建單一繼承拓撲樹，防循環繼承）。
4. `ork_get_object_type`
   - **符號規格**：`Function ork_get_object_type(target_id: UInt64, out_type_id: MutablePointer<UInt64>) -> Int32`
   - **說明**：純記憶體快速查詢目標物件之 TypeID（脫水狀態零 I/O 保證，絕不觸發復水）。
5. `ork_is_instance_of`
   - **符號規格**：`Function ork_is_instance_of(target_id: UInt64, target_type_id: UInt64, out_is_instance: MutablePointer<Int32>) -> Int32`
   - **說明**：檢查目標物件是否屬於或繼承自指定 TypeID（純記憶體查詢，脫水狀態零 I/O）。
6. `ork_is_subclass_of`
   - **符號規格**：`Function ork_is_subclass_of(derived_type: UInt64, base_type: UInt64, out_is_subclass: MutablePointer<Int32>) -> Int32`
   - **說明**：查詢型別註冊表中兩個 TypeID 是否具備派生繼承關係。
7. `ork_lock_object`
   - **符號規格**：`Function ork_lock_object(target_id: UInt64) -> Int32`
   - **說明**：取得目標物件控制區塊之獨占寫入互斥鎖。
8. `ork_unlock_object`
   - **符號規格**：`Function ork_unlock_object(target_id: UInt64) -> Int32`
   - **說明**：釋放目標物件控制區塊之獨占寫入互斥鎖。
9. `ork_lock_object_shared`
   - **符號規格**：`Function ork_lock_object_shared(target_id: UInt64) -> Int32`
   - **說明**：取得目標物件控制區塊之共享讀取鎖。
10. `ork_unlock_object_shared`
    - **符號規格**：`Function ork_unlock_object_shared(target_id: UInt64) -> Int32`
    - **說明**：釋放目標物件控制區塊之共享讀取鎖。
11. `ork_acquire_object_pointer`
    - **符號規格**：`Function ork_acquire_object_pointer(target_id: UInt64, out_obj: MutablePointer<RawPointer>) -> Int32`
    - **說明**：獲取目標物件實體指標；若目標物件處於脫水狀態，核心將自動透明觸發復水流程還原 Payload。
12. `ork_set_active_owner`
    - **符號規格**：`Function ork_set_active_owner(owner_id: UInt64) -> Int32`
    - **說明**：設定當前執行緒之 Active Owner 上下文（供子物件建構時向父物件自動登記 Handle 槽位）。
13. `ork_get_active_owner`
    - **符號規格**：`Function ork_get_active_owner(out_owner_id: MutablePointer<UInt64>) -> Int32`
    - **說明**：取得當前執行緒之 Active Owner 上下文 ID。
14. `ork_get_storage_state`
    - **符號規格**：`Function ork_get_storage_state(target_id: UInt64, out_state: MutablePointer<UInt8>) -> Int32`
    - **說明**：查詢目標物件當前之 StorageState（0: UnsavedNew, 1: Clean, 2: Dirty, 3: Dehydrated）。
15. `ork_mark_dirty`
    - **符號規格**：`Function ork_mark_dirty(target_id: UInt64) -> Int32`
    - **說明**：將目標物件狀態由 Clean 原子標記轉移為 Dirty。
16. `ork_get_root_edge_count`
    - **符號規格**：`Function ork_get_root_edge_count(target_id: UInt64, out_count: MutablePointer<UInt32>) -> Int32`
    - **說明**：查詢目標物件當前活躍之外部根指針（OuroPtr）引用計數。
17. `ork_dehydrate_object`
    - **符號規格**：`Function ork_dehydrate_object(target_id: UInt64) -> Int32`
    - **說明**：對指定物件執行記憶體脫水；序列化落盤後安全釋放實體記憶體，保留控制區塊。
18. `ork_register_edge`
    - **符號規格**：`Function ork_register_edge(parent_id: UInt64, child_id: UInt64) -> Int32`
    - **說明**：向領域圖登記父物件至子物件之強引用拓撲邊緣（child 之 `strong_in_count` 遞增）。
19. `ork_unregister_edge`
    - **符號規格**：`Function ork_unregister_edge(parent_id: UInt64, child_id: UInt64) -> Int32`
    - **說明**：移除父對子之強引用拓撲邊緣（child 之 `strong_in_count` 遞減，降至 0 觸發延遲銷毀）。
20. `ork_register_weak`
    - **符號規格**：`Function ork_register_weak(target_id: UInt64) -> Int32`
    - **說明**：增加目標物件之無繫結弱引用計數（`unbound_count` 遞增）。
21. `ork_unregister_weak`
    - **符號規格**：`Function ork_unregister_weak(target_id: UInt64) -> Int32`
    - **說明**：減少目標物件之無繫結弱引用計數（`unbound_count` 遞減）。
22. `ork_check_alive`
    - **符號規格**：`Function ork_check_alive(target_id: UInt64, out_alive: MutablePointer<Int32>) -> Int32`
    - **說明**：查詢目標物件是否存活且未處於銷毀/墓碑態（純記憶體查詢，絕不引發脫水復水）。
23. `ork_try_lock_weak`
    - **符號規格**：`Function ork_try_lock_weak(target_id: UInt64) -> Int32`
    - **說明**：嘗試原子晉升弱引用為根強引用，消滅 TOCTOU 競態。

---

### ⚙️ 類別 B：核心排程與延遲銷毀 (`core.h`)

24. `ork_try_initialize_core`
    - **符號規格**：`Function ork_try_initialize_core() -> Int32`
    - **說明**：確保全域核心資料結構與背景服務初始化完成。
25. `ork_collect_cycles`
    - **符號規格**：`Function ork_collect_cycles() -> Int32`
    - **說明**：同步觸發一輪循環孤島分析與試探性斷鏈回收。
26. `ork_flush_deferred_deletions`
    - **符號規格**：`Function ork_flush_deferred_deletions() -> Int32`
    - **說明**：阻塞等待延遲銷毀隊列目前積壓之所有物件釋放任務執行完成。
27. `ork_set_deferred_delete_mode`
    - **符號規格**：`Function ork_set_deferred_delete_mode(mode: Int32) -> Int32`
    - **說明**：切換延遲銷毀模式（0: 非同步背景執行緒池處理，1: 即時同步主執行緒處理）。
28. `ork_get_deferred_delete_pending_count`
    - **符號規格**：`Function ork_get_deferred_delete_pending_count() -> Int32`
    - **說明**：查詢延遲銷毀隊列目前待處理的物件數量。
29. `ork_get_cycle_suspect_count`
    - **符號規格**：`Function ork_get_cycle_suspect_count() -> Int32`
    - **說明**：查詢循環回收器中待分析之嫌疑節點數量。
30. `ork_stop_cycle_collector`
    - **符號規格**：`Function ork_stop_cycle_collector() -> Int32`
    - **說明**：安全停止循環回收器之後台工作線程。
31. `ork_stop_deferred_deletions`
    - **符號規格**：`Function ork_stop_deferred_deletions() -> Int32`
    - **說明**：安全停止延遲銷毀佇列之後台工作線程。

---

### 🛡️ 類別 C：宿主特權專用介面 (`host_api.h`)

32. `ork_flush_storage`
    - **符號規格**：`Function ork_flush_storage() -> Int32`
    - **說明**：宿主特權：雙管線同步排空，等待非同步 I/O 落盤與延遲銷毀全部執行完畢。
33. `ork_shutdown_runtime`
    - **符號規格**：`Function ork_shutdown_runtime() -> Int32`
    - **說明**：宿主特權：終止核心執行緒池與執行時期背景服務。
34. `ork_set_object_destroyed_callback`
    - **符號規格**：`Function ork_set_object_destroyed_callback(callback: FunctionPointer<UInt64 -> Void>) -> Int32`
    - **說明**：宿主特權：註冊進程級全域物件銷毀監聽回呼。
35. `ork_trigger_dehydration_rescue`
    - **符號規格**：`Function ork_trigger_dehydration_rescue(bytes_needed: UInt64, out_freed: MutablePointer<UInt64>, out_has_more: MutablePointer<Int32>) -> Int32`
    - **說明**：宿主特權：主動調度脫水器執行緊急記憶體救援換頁。
36. `ork_set_storage_state_for_testing`
    - **符號規格**：`Function ork_set_storage_state_for_testing(id: UInt64, state: UInt8) -> Int32`
    - **說明**：白盒測試特權：手動強制覆寫目標物件之 StorageState。
37. `ork_clear_object_payload_for_testing`
    - **符號規格**：`Function ork_clear_object_payload_for_testing(id: UInt64) -> Int32`
    - **說明**：白盒測試特權：直接置空目標物件之實體記憶體 Payload 模擬冷脫水態。

---

### 💾 類別 D：Base 模組 Heap 追蹤與清空檢驗介面 (`heap_api.h`)

38. `ork_heap_allocate`
    - **符號規格**：`Function ork_heap_allocate(size: UInt64, file: CString, line: Int32) -> RawPointer`
    - **說明**：配置記憶體並依編譯期方案記錄檔名與行號。
39. `ork_heap_allocate_aligned`
    - **符號規格**：`Function ork_heap_allocate_aligned(size: UInt64, alignment: UInt64, file: CString, line: Int32) -> RawPointer`
    - **說明**：配置滿足特定對齊之記憶體。
40. `ork_heap_deallocate`
    - **符號規格**：`Function ork_heap_deallocate(ptr: RawPointer) -> Void`
    - **說明**：釋放受管記憶體並扣減統計計數。
41. `ork_heap_deallocate_aligned`
    - **符號規格**：`Function ork_heap_deallocate_aligned(ptr: RawPointer, alignment: UInt64) -> Void`
    - **說明**：釋放對齊受管記憶體。
42. `ork_heap_is_clean`
    - **符號規格**：`Function ork_heap_is_clean() -> Int32`
    - **說明**：查詢當前模組 Heap 是否已完全歸零（1 為清空，0 為有洩漏）。
43. `ork_heap_get_active_allocations`
    - **符號規格**：`Function ork_heap_get_active_allocations() -> UInt64`
    - **說明**：取得當前存活配置區塊數。
44. `ork_heap_get_active_bytes`
    - **符號規格**：`Function ork_heap_get_active_bytes() -> UInt64`
    - **說明**：取得當前存活佔用位元組數。
45. `ork_heap_dump_leaks`
    - **符號規格**：`Function ork_heap_dump_leaks(out_buf: MutablePointer<Char>, buf_size: UInt64) -> Int32`
    - **說明**：輸出格式化 UTF-8 洩漏報告字串至緩衝區。
46. `ork_heap_assert_clean`
    - **符號規格**：`Function ork_heap_assert_clean(context_name: CString) -> Int32`
    - **說明**：斷言 Heap 必須清空（成功傳回 0，有洩漏傳回 -1）。
47. `ork_heap_reset`
    - **符號規格**：`Function ork_heap_reset() -> Void`
    - **說明**：重設 HeapTracker 統計數據與記錄。
''', encoding="utf-8")


    # =========================================================================
    # 04_concurrency_and_locks.md
    # =========================================================================
    (tech_dir / "04_concurrency_and_locks.md").write_text(r'''# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks RFC)

本文件定義 OuroKore 系統中多執行緒併發存取、全域單向鎖偏序、樹狀結構不可重入鎖遍歷以及任務排程器的完整防死鎖規範。

---

## 🚦 1. 全域單向鎖偏序五層架構 (Complete 5-Tier Lock Partial Ordering Hierarchy)

為杜絕跨執行緒併發存取引發的循環等待死鎖，任何語言實現之 OuroKore 核心必須嚴格遵守以下單向鎖順序：

```
[ 層級 1：全域註冊表讀寫鎖 (Global Registry Mutex) ]
                       │
                       ▼
  [ 層級 2：ControlBlock 物件互斥鎖 (Object Mutex) ]
                       │
                       ▼
 [ 層級 3：樹狀結構拓撲讀寫鎖 (Tree Topology Mutex - shared_mutex) ]
                       │
                       ▼
  [ 層級 4：樹節點 Payload 資料鎖 (Tree Data Mutex) ]
                       │
                       ▼
    [ 層級 5：佇列與排程器互斥鎖 (Queue / Worker Mutex) ]
```

### 鐵律防線與死鎖數學反證：
1. **嚴禁逆向取鎖 (No Inverted Lock Acquisition)**：
   - 禁止在持有層級 2（物件鎖）時索取層級 1（全域註冊表鎖）。
   - 禁止在持有層級 4（資料鎖）時索取層級 3（拓撲鎖）。
   - 禁止在持有子物件鎖時逆向索取父物件鎖。
2. **領域物件終結器無鎖調用防線 (Finalizer Outside Locks)**：
   - 核心在調用領域物件的釋放/終結邏輯（Destructor / Dispose）或執行自訂清理回呼期間，**絕對不可持有任何全域註冊表鎖或佇列鎖**。
   - 目的：防範物件在終結邏輯中遞迴存取其他物件引發連鎖自我死鎖。

---

## 🔒 2. 樹狀結構不可重入鎖與防死鎖遍歷兩階段範式 (Tree Traversal & Deadlock Prevention RFC)

### 2.1 整樹共享讀寫鎖架構
* 樹狀結構中，根節點（Root）與其所有子孫節點（Descendants）**共享同一個樹級讀寫鎖（`shared_mutex`）**。
* 此鎖為**不可重入鎖（Non-recursive Mutex）**，以取得最高之硬體級讀寫併發效能。

### 2.2 遍歷期間拓撲不可變鐵律
> **高壓線禁忌**：在持共享讀鎖（Shared Read Lock）遍歷樹節點期間，**絕對禁止調用任何拓撲異動介面**（如 `AddChild()`、`RemoveChild()`、`PushElement()`、`ClearChildren()` 等）。因為拓撲修改需索取獨占寫鎖（Unique Write Lock），當前執行緒若嘗試索取將立即引發不可重入死鎖！

### 2.3 兩階段延遲操作演算法 (Two-Phase Deferred Mutation Algorithm)
若業務邏輯需要依據遍歷結果動態新增或修剪節點，必須嚴格採用兩階段演算法：

```text
Procedure PruneTreeSafely(root):
    Let to_remove = List<NodeHandle>()
    
    // 【第一階段：持讀鎖安全收集目標節點】
    AcquireSharedReadLock(root.GetTreeMutex())
    For Each child In root.Elements Do
        If child.GetData() == "過期項目" Then
            to_remove.Append(child) // 僅收集句柄，絕不在此調用 RemoveChild！
        End If
    End For
    ReleaseSharedReadLock(root.GetTreeMutex()) // 讀鎖在此安全解構釋放！

    // 【第二階段：無鎖或依需索取寫鎖批次執行拓撲異動】
    For Each target In to_remove Do
        root.RemoveChild(target) // 安全！內部獨占寫鎖不會與讀鎖衝突
    End For
End Procedure
```

---

## 🧵 3. 非同步執行緒池 (ThreadPool) 任務排程模型

### 3.1 工作執行緒識別與自死鎖防呆 (Worker Thread Identification)
1. **Thread-Local 識別標記**：每個被執行緒池管理的工作線程必須具備執行緒在地標記（`IsWorker = True`）。
2. **等待排空自死鎖防呆 (Wait Idle Invariant)**：
   - 若某個 Worker 執行緒在其承擔的任務內部呼叫了等待整個執行緒池排空（`WaitForAll` / `WaitIdle`）之操作，內部必須立即判定此為自我死鎖行為並主動 Fail-Fast 拋出異常，杜絕死鎖擴散至整個進程。

### 3.2 彈性動態線程池之伸縮狀態機 (Dynamic Thread Scaling)
動態執行緒池（`DynamicThreadPool`）具備自適應工作量自動擴充與縮容能力：
* **擴容條件**：當新任務抵達且當前排隊任務數 > 0，且當前線程數 < `max_threads` 時，立即生成新 Worker 線程。
* **縮容條件**：當工作線程等待任務超過指定之閒置逾時（`idle_timeout`），且當前線程數 > `min_threads` 時，Worker 線程自動終止解構退出。

---

## 📦 4. 執行緒安全佇列與同步原語規格 (Concurrency Primitives RFC)

1. **執行緒安全阻塞佇列 (`ThreadSafeQueue<T>`)**：
   - 內部由互斥鎖與條件變數（Condition Variable）保護。
   - `Push(item)`：入隊後發出信號喚醒至少一個等待者。
   - `TryPop(item)`：非阻塞提取，若為空立即回傳 `False`。
   - `WaitAndPop()`：阻塞直到隊列有元素或超時返回。
2. **計數信號量 (`Semaphore`)**：
   - 維護原子計數值，支援跨執行緒之資源配額控制與限流。
3. **條件事件 (`Event`)**：
   - 支援手動重置（Manual Reset，一次喚醒所有等待執行緒）與自動重置（Auto Reset，一次僅喚醒單一執行緒）兩種模式。

---

## 🤝 5. 外掛卸載背景等待執行緒與主執行緒零卡頓握手模型

主程式在釋放外掛最後引用時，將收尾等待交棒給背景工作執行緒，背景線程以條件變數等待外掛調用 `on_ready_to_unload()` 握手信號：
* 主程式執行緒耗時為 0ms，完全免疫外掛的卸載卡頓。
* 背景執行緒在收到握手或達到逾時限制後，才執行作業系統級的動態庫卸載。
''', encoding="utf-8")

    print("✅ specs/technical/ 全套 4 份高標準技術手冊已依據中性語言 RFC 規範重新生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_technical_specs(specs_dir)
