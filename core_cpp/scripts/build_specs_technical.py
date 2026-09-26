# -*- coding: utf-8 -*-
"""
生成 specs/technical/ 目錄下所有技術手冊與規範手冊
"""
from pathlib import Path

def generate_technical_specs(specs_dir: Path):
    tech_dir = specs_dir / "technical"
    tech_dir.mkdir(parents=True, exist_ok=True)

    # 01_system_architecture.md
    (tech_dir / "01_system_architecture.md").write_text('''# 01. OuroKore 系統架構設計規範 (System Architecture)

## 🏛️ 1. 三層邊界隔離架構 (Three-Tier Architecture)

OuroKore 核心嚴格遵循「**C ABI 為底，各語言 Wrapper 為糖**」之架構體系，劃分三大隔離層次：

```
+----------------------------------------------------------------------+
|                     主程式層 (Host Application)                      |
|  - 進入點 main() 持有唯一之 HostContext                               |
|  - 全域生命週期控制 (Init / Shutdown / Reset)                        |
|  - 儲存驅動注入、脫水換頁策略配置、非同步執行緒池調度                 |
+----------------------------------------------------------------------+
                                   │
                                   ▼
+----------------------------------------------------------------------+
|                  第三方插件層 (Plugin / Component)                   |
|  - 領域物件繼承 OuroObject，嚴禁存取內部 ControlBlock 裸指標          |
|  - 拓撲邊緣透過 OwningHandle、UnboundHandle、OwningContainerHandle 表達 |
|  - 棧上受管指針 OuroPtr<T>，執行緒安全讀寫鎖 OuroReadLock/WriteLock    |
|  - 物理隔離：絕不暴露任何進程級特權 API (零洩漏保證)                  |
+----------------------------------------------------------------------+
                                   │
                                   ▼
+----------------------------------------------------------------------+
|                       純 C ABI 核心層 (core.dll)                     |
|  - component_api.h / core.h / host_api.h                             |
|  - 純整數狀態碼 (int32_t)、HandleID (uint64_t)、純 C 回呼函式指標     |
|  - 所有導出函式內部嚴格 try-catch 攔截 C++ 例外，保證跨 DLL ABI 穩定  |
+----------------------------------------------------------------------+
```

---

## 🔄 2. 循環參照收集器原理 (CycleCollector)

* **圖論演算法**：針對 `OwningHandle` 所形成的潛在循環孤島，`CycleCollector` 在背景採用試探性減計數法（Trial Deletion）：
  1. **染色偵測**：走訪可疑節點子圖，將內部邊緣的參照從臨時計數中扣除。
  2. **根可達性驗證**：若子圖中所有節點的外部根引用（`root_count`）皆為 0 且強引用全部來自環路內部，判定為孤島。
  3. **外科手術斷鏈**：在靜音模式下原子解除環內邊緣，使物件引用計數自然歸零，平滑移交延遲銷毀隊列。
* **執行緒屏障等待**：呼叫 `host->CollectCycles()` 時，呼叫端安全阻塞於條件變數等待當前批次走訪完成，消滅 Data Race。
''', encoding="utf-8")

    # 02_binary_protocols.md
    (tech_dir / "02_binary_protocols.md").write_text('''# 02. 二進位串流與藍圖打包協議 (Binary Protocols)

## 📜 1. 藍圖佈局與純 Payload 分離原則

OuroKore 藍圖打包嚴格禁止將物件內部私有資料與拓撲邊緣混雜在一起：
1. **純資料區 (Pure Payload)**：透過 `OuroStream::WriteProperty` 寫入具備型別校驗與 Key-Value 結構的單純數值。
2. **邊緣名冊區 (Edge Roster)**：框架自動遍歷物件的 `m_registered_handles`，依序寫入：
   * `Slot Name (String)` -> `Target Count (uint32_t)` -> `Target HandleIDs (uint64_t...)`。

---

## 🛡️ 2. 防禦性邊界檢查與兩階段反序列化 (Two-Phase Apply)

反序列化外部資料串流時，嚴防各類惡意攻擊與記憶體損毀：
1. **重複鍵阻斷 (Fail-Fast Duplicate Key Guard)**：若藍圖串流中存在重複屬性名稱，立即拋出 `OuroDuplicateKeyException`。
2. **記憶體爆炸防禦 (Sanity Upper Bounds)**：插槽數量與目標 Handle 數量嚴格受上限約束（如單一插槽上限 1,000,000），防止惡意構造的巨大數字耗盡記憶體。
3. **兩階段驗證套用**：反序列化時先在棧上或暫存區驗證整體資料完整性，無例外發生後才原子更新物件內部狀態。
''', encoding="utf-8")

    # 03_c_abi_and_memory.md
    (tech_dir / "03_c_abi_and_memory.md").write_text('''# 03. 純 C ABI 規格與記憶體佈局規範 (C ABI & Memory)

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
''', encoding="utf-8")

    # 04_concurrency_and_locks.md
    (tech_dir / "04_concurrency_and_locks.md").write_text('''# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks)

## 🚦 1. 單向鎖階層規範 (Lock Ordering Invariants)

在多執行緒併發環境下，OuroKore 嚴格遵循以下單向取鎖順序，違者視為架構級 Bug：

```
[ Registry 全域讀寫鎖 (m_registry_mutex) ]
                  │
                  ▼
   [ ControlBlock 物件獨占鎖 (m_mutex) ]
                  │
                  ▼
       [ 延遲銷毀隊列鎖 (m_queue_mutex) ]
```

### 鐵律防線：
1. **嚴禁反向鎖定**：禁止在持有子物件獨占鎖的情況下，逆向索取父物件的獨占鎖。
2. **生命週期解構無鎖調用**：在調用使用者定義的 `~OuroObject()` 解構函式期間，絕對不持有任何全域註冊表互斥鎖，防範巢狀解構引發死鎖。

---

## 🧵 2. FixedThreadPool 與 DynamicThreadPool 防自我死鎖

* **Worker 執行緒自我辨識**：工作執行緒內部標記 `thread_local bool is_worker_thread`。
* **`wait_idle()` 防呆保護**：若工作執行緒在自身任務中嘗試呼叫 `wait_idle()`，內部立即判定為潛在自我死鎖並 Fail-Fast，徹底保護系統穩定。
''', encoding="utf-8")

    print("✅ specs/technical/ 全套 4 份技術手冊生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_technical_specs(specs_dir)
