# 03. 純 C ABI 規格與記憶體佈局規範 (C ABI & Memory RFC)

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
