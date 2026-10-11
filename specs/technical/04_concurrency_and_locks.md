# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks RFC)

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

---

## 🔄 6. 脫水換頁與多執行緒併發復水雙重檢查鎖協議 (Concurrent Rehydration Double-Checked Locking RFC)

當多個執行緒同時存取或復水同一個脫水物件時，核心實作必須保證序列化載入之執行緒安全與防死鎖：
1. **ControlBlock 獨佔排他鎖保護**：
   - 復水執行期必須首先獲取物件 ControlBlock 之獨佔寫鎖（`ork_lock_object`），確保同一物件在同一時刻僅允許單一執行緒執行串流反序列化與實體重綁。
2. **Double-Checked Locking 雙重驗證鐵律**：
   - 獲取寫鎖後，必須立即執行二次檢查判定前序執行緒是否已完成復水：
     ```text
     Condition IsAlreadyLoaded(id):
         Return ork_get_storage_state(id) != Dehydrated AND ork_is_payload_loaded(id) == True
     ```
   - 若雙重條件成立，表示該物件之實體記憶體指標已就緒，當前執行緒必須立即安全早退（Return），嚴禁重複反序列化或二次覆寫實體指標。
3. **持鎖期間嚴禁調用 `ork_acquire_object_pointer`**：
   - 嚴禁在持有 `ork_lock_object` 期間呼叫 `ork_acquire_object_pointer`。
   - 原因一（防止自我死鎖）：`ork_acquire_object_pointer` 在未獲取指標時會觸發 `rehydrate_fn`，造成同執行緒對不可重入鎖二次獲取而發生死鎖。
   - 原因二（避免強引用阻斷）：`ork_acquire_object_pointer` 要求強引用計數 > 0，但手動復水期間外部智慧指標尚未建立，強引用為 0 會導致合法已載入物件被誤判為未載入，進而引發併發覆寫破壞。

---

## 🛡️ 7. 物件鎖定之二次確認與 TOCTOU 防禦規範 (Object Locking Double-Checked Locking RFC)

為防範全域註冊表讀寫鎖（`m_registry_mutex`）與物件讀寫鎖（`cb->m_rw_lock`）產生 AB-BA 逆向死鎖，核心在 `ork_lock_object` 與 `ork_lock_object_shared` 中採用兩階段取鎖模型：
1. **第一階段（Pinning 弱引用釘住）**：在持有 `m_registry_mutex` 共享讀鎖下尋找目標，原子遞增 `cb->m_weak_count`，隨後**立即釋放名冊讀鎖**。
2. **第二階段（無鎖等待目標鎖）**：無名冊鎖保護下等待 `cb->m_rw_lock`（寫鎖或讀鎖）。
3. **Double-Checked Locking 鐵律**：
   - 在釋放名冊鎖至成功獲得物件鎖的排隊間隙，並發執行緒可能已發起物件銷毀（如 `UnregisterObject` 或延遲清理）。
   - 因此，**成功獲取物件鎖後，必須立即二次驗證物件拆解旗標**：
     ```text
     If cb.is_destructing == True Then
         ReleaseObjectLock(cb)      // 立即釋放剛獲取的讀寫鎖
         ReleasePin(target_id, cb)  // 原子遞減 weak_count，若為最後持有者則清理墓碑
         Return Error(ObjectNotFound / Destructing)
     End If
     ```
   - 徹底杜絕呼叫端持有一把已指向被釋放 Payload 的死鎖，100% 杜絕 TOCTOU (Time-of-Check to Time-of-Use) 與 UAF。

---

## 🚪 8. 進程退出與靜態單例解構之無鎖清理防線 (Process Exit Lockless Tear-Down RFC)

在 Windows/POSIX 作業系統中，進程終止（CRT `exit()`）與動態程式庫卸載（`LdrShutdownProcess`）期間，作業系統持有了進程級的 **Loader Lock**：
1. **Loader Lock 期間禁止嘗試獲取讀寫互斥鎖**：
   - 在 Loader Lock 期間，背景工作執行緒可能已被作業系統強制凍結或終止。
   - 若靜態單例解構子（`~Registry()`）嘗試獲取 `std::shared_mutex`（在 MinGW 下底層為 `pthread_rwlock_wrlock`），會與作業系統同步物件或已凍結執行緒衝突，造成進程退出死鎖。
2. **無鎖置換（Lockless Swap）兩階段清理架構**：
   - 解構子（`~Registry()`）執行時，所有業務執行緒已經停止，無並發寫入威脅。解構子必須以無鎖模式執行 `ClearInternal(acquire_lock=false)`：
     - 步驟一：無鎖極速換出名冊（`to_cleanup.swap(m_object_map)`）。
     - 步驟二：無鎖依序調用 `cb->DeletePayload()` 釋放實體記憶體。
     - 步驟三：無鎖依序 `delete cb` 釋放墓碑控制區塊。
     - 步驟四：無鎖清空持久化映射名冊。
   - 常規執行期重置（`ork::Registry::Clear()` / `ork_shutdown_runtime`）則維持獨佔寫入鎖保護（`acquire_lock=true`），達成靜態退出與動態排空的雙軌安全。
