# 03. 純 C ABI 規格與記憶體佈局規範 (C ABI & Memory)

## 📌 1. C ABI 導出符號規範總覽 (共 32 個導出函式)

所有跨動態庫呼叫慣例統一為 `ORK_CALL`（Windows 上為 `__stdcall` 或預設 cdecl，跨平台符號一致）。所有回傳值皆為純整數狀態碼 `int32_t`。

### 📦 類別 A：組件生命週期與弱引用 (`component_api.h`)

1. `ork_register_object(OuroObject* obj, ork_destroy_fn_t destroy_fn, HandleID* out_id)`
   * 註冊新建構之物件至全域註冊表，配發全域唯一 `HandleID`。
2. `ork_lock_object(HandleID target_id)`
   * 獨占鎖定目標物件之 ControlBlock。
3. `ork_unlock_object(HandleID target_id)`
   * 釋放目標物件之獨占鎖。
4. `ork_lock_object_shared(HandleID target_id)`
   * 共享（讀取）鎖定目標物件之 ControlBlock。
5. `ork_unlock_object_shared(HandleID target_id)`
   * 釋放目標物件之共享讀鎖。
6. `ork_acquire_object_pointer(HandleID target_id, OuroObject** out_obj)`
   * 獲取目標物件實體裸指標（若已脫水則透明觸發自動復水）。
7. `ork_set_active_owner(HandleID owner_id)`
   * 設定當前執行緒之 Active Owner 上下文（供子物件 Handle 自動向父物件登記）。
8. `ork_get_active_owner(HandleID* out_owner_id)`
   * 取得當前執行緒之 Active Owner ID。
9. `ork_get_storage_state(HandleID target_id, uint8_t* out_state)`
   * 取得物件當前之 StorageState（0: UnsavedNew, 1: Clean, 2: Dirty, 3: Dehydrated）。
10. `ork_mark_dirty(HandleID target_id)`
    * 原子將物件狀態由 Clean 轉移為 Dirty。
11. `ork_get_root_edge_count(HandleID target_id, uint32_t* out_count)`
    * 取得目標物件之活躍棧上根指標（OuroPtr）計數。
12. `ork_dehydrate_object(HandleID target_id)`
    * 執行物件記憶體脫水（若活躍根計數為 0，釋放實體記憶體並保留墓碑）。
13. `ork_register_edge(HandleID parent_id, HandleID child_id)`
    * 登記父對子之強擁有權拓撲邊緣（增加 child 強引用）。
14. `ork_unregister_edge(HandleID parent_id, HandleID child_id)`
    * 解除父對子之擁有權邊緣（減少 child 強引用，降至 0 移交延遲銷毀隊列）。
15. `ork_register_weak(HandleID target_id)`
    * 增加目標物件之弱引用計數。
16. `ork_unregister_weak(HandleID target_id)`
    * 減少目標物件之弱引用計數。
17. `ork_check_alive(HandleID target_id, int32_t* out_alive)`
    * 檢查目標物件是否存活且未處於拆解銷毀狀態。
18. `ork_try_lock_weak(HandleID target_id)`
    * 嘗試將弱引用原子鎖定並晉升為強根引用（防範 TOCTOU 競態）。

---

### ⚙️ 類別 B：核心基礎與延遲銷毀 (`core.h`)

19. `ork_try_initialize_core(void)`
    * 確保底層 Registry 與隊列核心結構已初始化。
20. `ork_collect_cycles(void)`
    * 觸發同步執行一輪循環孤島分析與回收。
21. `ork_flush_deferred_deletions(void)`
    * 同步排空並等待延遲銷毀隊列任務完成。
22. `ork_set_deferred_delete_mode(int32_t mode)`
    * 設定延遲銷毀模式（0: Async 背景執行緒池，1: Sync 即時同步執行）。
23. `ork_get_deferred_delete_pending_count(void)`
    * 取得延遲銷毀隊列目前待處理之任務數量。
24. `ork_get_cycle_suspect_count(void)`
    * 取得循環收集器佇列中之嫌疑犯節點數量。
25. `ork_stop_cycle_collector(void)`
    * 停止循環回收器背景工作執行緒。
26. `ork_stop_deferred_deletions(void)`
    * 停止延遲銷毀背景工作執行緒。

---

### 🛡️ 類別 C：宿主特權專用介面 (`host_api.h`)

27. `ork_flush_storage(void)`
    * 雙管線同步排空：等待磁碟寫入落盤與延遲銷毀全部清空。
28. `ork_shutdown_runtime(void)`
    * 優雅終止核心執行緒池與背景任務，確保退出無懸掛。
29. `ork_set_object_destroyed_callback(void (*callback)(HandleID id))`
    * 註冊進程級全域物件銷毀監聽回呼。
30. `ork_trigger_dehydration_rescue(size_t bytes_needed, size_t* out_freed, int32_t* out_has_more)`
    * 宿主專用緊急記憶體脫水救援排程。
31. `ork_set_storage_state_for_testing(HandleID id, uint8_t state)`
    * 白盒測試專用：強制修改物件 StorageState。
32. `ork_clear_object_payload_for_testing(HandleID id)`
    * 白盒測試專用：直接清空記憶體 Payload 模擬脫水狀態。

---

## 🧱 2. 跨模組 CRT 隔離與自定義 Deleter

* 物件實體記憶體的釋放（`delete obj`）必須在其**原始建立之動態模組（DLL/Plugin）的 CRT 堆疊**中執行。
* 透過 `ork_register_object` 傳入之 `ork_destroy_fn_t` 回呼達成 CRT 隔離，杜絕 Windows 跨 DLL CRT `free()` 崩潰。
