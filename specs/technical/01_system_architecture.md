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
