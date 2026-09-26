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
''', encoding="utf-8")

    # =========================================================================
    # 02_binary_protocols.md
    # =========================================================================
    (tech_dir / "02_binary_protocols.md").write_text('''# 02. 二進位串流與藍圖打包協議 (Binary Protocols & Wire Format RFC)

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
* **數值屬性值**：直接以原生 Little-Endian 位元組寫入對應大小（如 `Int32` 佔 4 位元組）。
* **防禦性約束**：
  1. **重複鍵阻斷 (Duplicate Key Guard)**：在同一物件序列化串流中，若出現相同之 Key 名稱，解析端必須立即中斷（Fail-Fast 拋出重複鍵異常）。
  2. **鍵名序列匹配驗證 (Key Mismatch Guard)**：反序列化讀取時，讀出的 Key 必須與期望名稱完全一致，否則視為串流版本失配或損毀。

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

## 📋 2. 純 C ABI 導出符號規範 (共 32 個導出函式)

以下以**中性二進位介面符號規格（Neutral ABI Specification）**完整定義 32 個導出函式：

### 📦 類別 A：組件生命週期與弱引用 (`component_api.h`)

1. `ork_register_object`
   - **符號規格**：`Function ork_register_object(obj: RawPointer, destroy_fn: FunctionPointer, out_id: MutablePointer<UInt64>) -> Int32`
   - **說明**：將新建構之領域物件註冊至全域註冊表，配發唯一的 64 位元 `HandleID`，綁定模組專屬的解構回呼。
2. `ork_lock_object`
   - **符號規格**：`Function ork_lock_object(target_id: UInt64) -> Int32`
   - **說明**：取得目標物件控制區塊之獨占寫入互斥鎖。
3. `ork_unlock_object`
   - **符號規格**：`Function ork_unlock_object(target_id: UInt64) -> Int32`
   - **說明**：釋放目標物件控制區塊之獨占寫入互斥鎖。
4. `ork_lock_object_shared`
   - **符號規格**：`Function ork_lock_object_shared(target_id: UInt64) -> Int32`
   - **說明**：取得目標物件控制區塊之共享讀取鎖。
5. `ork_unlock_object_shared`
   - **符號規格**：`Function ork_unlock_object_shared(target_id: UInt64) -> Int32`
   - **說明**：釋放目標物件控制區塊之共享讀取鎖。
6. `ork_acquire_object_pointer`
   - **符號規格**：`Function ork_acquire_object_pointer(target_id: UInt64, out_obj: MutablePointer<RawPointer>) -> Int32`
   - **說明**：獲取目標物件實體指標；若目標物件處於脫水狀態，核心將自動透明觸發復水流程還原 Payload。
7. `ork_set_active_owner`
   - **符號規格**：`Function ork_set_active_owner(owner_id: UInt64) -> Int32`
   - **說明**：設定當前執行緒之 Active Owner 上下文（供子物件建構時向父物件自動登記 Handle 槽位）。
8. `ork_get_active_owner`
   - **符號規格**：`Function ork_get_active_owner(out_owner_id: MutablePointer<UInt64>) -> Int32`
   - **說明**：取得當前執行緒之 Active Owner 上下文 ID。
9. `ork_get_storage_state`
   - **符號規格**：`Function ork_get_storage_state(target_id: UInt64, out_state: MutablePointer<UInt8>) -> Int32`
   - **說明**：查詢目標物件當前之 StorageState（0: UnsavedNew, 1: Clean, 2: Dirty, 3: Dehydrated）。
10. `ork_mark_dirty`
    - **符號規格**：`Function ork_mark_dirty(target_id: UInt64) -> Int32`
    - **說明**：將目標物件狀態由 Clean 原子標記轉移為 Dirty。
11. `ork_get_root_edge_count`
    - **符號規格**：`Function ork_get_root_edge_count(target_id: UInt64, out_count: MutablePointer<UInt32>) -> Int32`
    - **說明**：查詢目標物件當前活躍之外部根指針（OuroPtr）引用計數。
12. `ork_dehydrate_object`
    - **符號規格**：`Function ork_dehydrate_object(target_id: UInt64) -> Int32`
    - **說明**：對指定物件執行記憶體脫水；序列化落盤後安全釋放實體記憶體，保留控制區塊。
13. `ork_register_edge`
    - **符號規格**：`Function ork_register_edge(parent_id: UInt64, child_id: UInt64) -> Int32`
    - **說明**：向領域圖登記父物件至子物件之強引用拓撲邊緣（child 之 `strong_in_count` 遞增）。
14. `ork_unregister_edge`
    - **符號規格**：`Function ork_unregister_edge(parent_id: UInt64, child_id: UInt64) -> Int32`
    - **說明**：移除父對子之強引用拓撲邊緣（child 之 `strong_in_count` 遞減，降至 0 觸發延遲銷毀）。
15. `ork_register_weak`
    - **符號規格**：`Function ork_register_weak(target_id: UInt64) -> Int32`
    - **說明**：增加目標物件之無繫結弱引用計數（`unbound_count` 遞增）。
16. `ork_unregister_weak`
    - **符號規格**：`Function ork_unregister_weak(target_id: UInt64) -> Int32`
    - **說明**：減少目標物件之無繫結弱引用計數（`unbound_count` 遞減）。
17. `ork_check_alive`
    - **符號規格**：`Function ork_check_alive(target_id: UInt64, out_alive: MutablePointer<Int32>) -> Int32`
    - **說明**：查詢目標物件是否存活且未處於銷毀/墓碑態。
18. `ork_try_lock_weak`
    - **符號規格**：`Function ork_try_lock_weak(target_id: UInt64) -> Int32`
    - **說明**：嘗試原子晉升弱引用為根強引用，消滅 TOCTOU 競態。

---

### ⚙️ 類別 B：核心排程與延遲銷毀 (`core.h`)

19. `ork_try_initialize_core`
    - **符號規格**：`Function ork_try_initialize_core() -> Int32`
    - **說明**：確保全域核心資料結構與背景服務初始化完成。
20. `ork_collect_cycles`
    - **符號規格**：`Function ork_collect_cycles() -> Int32`
    - **說明**：同步觸發一輪循環孤島分析與試探性斷鏈回收。
21. `ork_flush_deferred_deletions`
    - **符號規格**：`Function ork_flush_deferred_deletions() -> Int32`
    - **說明**：阻塞等待延遲銷毀隊列目前積壓之所有物件釋放任務執行完成。
22. `ork_set_deferred_delete_mode`
    - **符號規格**：`Function ork_set_deferred_delete_mode(mode: Int32) -> Int32`
    - **說明**：切換延遲銷毀模式（0: 非同步背景執行緒池處理，1: 即時同步主執行緒處理）。
23. `ork_get_deferred_delete_pending_count`
    - **符號規格**：`Function ork_get_deferred_delete_pending_count() -> Int32`
    - **說明**：查詢延遲銷毀隊列目前待處理的物件數量。
24. `ork_get_cycle_suspect_count`
    - **符號規格**：`Function ork_get_cycle_suspect_count() -> Int32`
    - **說明**：查詢循環回收器中待分析之嫌疑節點數量。
25. `ork_stop_cycle_collector`
    - **符號規格**：`Function ork_stop_cycle_collector() -> Int32`
    - **說明**：安全停止循環回收器之後台工作線程。
26. `ork_stop_deferred_deletions`
    - **符號規格**：`Function ork_stop_deferred_deletions() -> Int32`
    - **說明**：安全停止延遲銷毀佇列之後台工作線程。

---

### 🛡️ 類別 C：宿主特權專用介面 (`host_api.h`)

27. `ork_flush_storage`
    - **符號規格**：`Function ork_flush_storage() -> Int32`
    - **說明**：宿主特權：雙管線同步排空，等待非同步 I/O 落盤與延遲銷毀全部執行完畢。
28. `ork_shutdown_runtime`
    - **符號規格**：`Function ork_shutdown_runtime() -> Int32`
    - **說明**：宿主特權：終止核心執行緒池與執行時期背景服務。
29. `ork_set_object_destroyed_callback`
    - **符號規格**：`Function ork_set_object_destroyed_callback(callback: FunctionPointer<UInt64 -> Void>) -> Int32`
    - **說明**：宿主特權：註冊進程級全域物件銷毀監聽回呼。
30. `ork_trigger_dehydration_rescue`
    - **符號規格**：`Function ork_trigger_dehydration_rescue(bytes_needed: UInt64, out_freed: MutablePointer<UInt64>, out_has_more: MutablePointer<Int32>) -> Int32`
    - **說明**：宿主特權：主動調度脫水器執行緊急記憶體救援換頁。
31. `ork_set_storage_state_for_testing`
    - **符號規格**：`Function ork_set_storage_state_for_testing(id: UInt64, state: UInt8) -> Int32`
    - **說明**：白盒測試特權：手動強制覆寫目標物件之 StorageState。
32. `ork_clear_object_payload_for_testing`
    - **符號規格**：`Function ork_clear_object_payload_for_testing(id: UInt64) -> Int32`
    - **說明**：白盒測試特權：直接置空目標物件之實體記憶體 Payload 模擬冷脫水態。
''', encoding="utf-8")

    # =========================================================================
    # 04_concurrency_and_locks.md
    # =========================================================================
    (tech_dir / "04_concurrency_and_locks.md").write_text('''# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks RFC)

本文件定義 OuroKore 系統中多執行緒併發存取、全域單向鎖偏序與任務排程器的防死鎖規範。

---

## 🚦 1. 全域單向鎖偏序規範 (Lock Partial Ordering Invariants)

為杜絕跨執行緒併發存取引發的循環等待死鎖，任何語言實現之 OuroKore 核心必須嚴格遵守以下單向鎖順序：

```
[ 層級 1：全域註冊表讀寫鎖 (Registry Mutex) ]
                     │
                     ▼
  [ 層級 2：ControlBlock 物件互斥鎖 (Object Mutex) ]
                     │
                     ▼
   [ 層級 3：佇列與排程器互斥鎖 (Queue / Worker Mutex) ]
```

### 鐵律防線：
1. **嚴禁逆向取鎖**：禁止在持有層級 2（物件鎖）的情況下，嘗試索取層級 1（全域註冊表鎖）；禁止在持有子物件獨占鎖的情況下，逆向索取父物件的獨占鎖。
2. **領域物件終結器無鎖調用防線 (Finalizer Outside Locks)**：
   - 核心在調用領域物件的釋放/終結邏輯（Destructor / Finalizer / Dispose）或執行釋放回呼期間，**絕對不可持有任何全域註冊表鎖或佇列鎖**。
   - 目的：防範物件在終結邏輯中遞迴存取其他物件或觸發連鎖清理而造成自死鎖。

---

## 🧵 2. 執行緒池 Worker 辨識與防遞迴等待死鎖

在高效能非同步執行緒池（`FixedThreadPool` / `DynamicThreadPool`）中：
1. **工作執行緒識別標記**：每個被執行緒池管理的工作線程必須具備執行緒在地標記（Thread-Local Flag: `IsWorker = True`）。
2. **等待排空防呆保護 (Wait Idle Invariant)**：
   - 若某個 Worker 執行緒在其承擔的任務內部呼叫了等待整個執行緒池排空（`WaitIdle`）之操作，內部必須立即判定此為自我死鎖行為並主動 Fail-Fast 拋出異常，杜絕死鎖擴散至整個進程。
''', encoding="utf-8")

    print("✅ specs/technical/ 全套 4 份高標準技術手冊已依據中性語言 RFC 規範重新生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_technical_specs(specs_dir)
