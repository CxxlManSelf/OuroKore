# 06. 宿主生命週期與特權管理 (Host Lifecycle)

本章節介紹主程式宿主（Host Application）專屬的架構設計、特權 API 與安全退出機制。

---

## 🛡️ 1. 為什麼需要 `HostContext` 隔離？

在大型專案或插件架構中，第三方插件（動態庫 DLL）若能隨意調用全域停機或 GC 函式，會引發災難性後果：
* 插件隨意呼叫 `Shutdown()` 會殺死全進程的核心背景執行緒。
* 插件隨意呼叫 `FlushStorage()` 會導致主執行緒嚴重掉幀。
* 插件隨意替換脫水器會使主程式的快取策略失效。

**因此，OuroKore 將所有系統級管理特權完全收斂於 `HostContext` 物件中！**

---

## 🔑 2. `HostContext` 特權方法清單

唯有成功調用 `ork::Init()` 的主程式才能持有合法的 `HostContext`：

```cpp
auto host = ork::Init(storage, dehydrator);

// 1. 落盤排空：等待背景所有磁碟 I/O 與銷毀任務完成
host.FlushStorage();

// 2. 延遲銷毀排空：等待延遲隊列清空
host.FlushDeferredDeletions();

// 3. 即時循環回收：強制觸發一輪循環孤島偵測
host.CollectCycles();

// 4. 設定延遲銷毀模式（sync: 同步即時；async: 背景平行）
host.SetDeferredDeleteMode(false);

// 5. 調度緊急記憶體自救脫水
size_t freed = host.TriggerDehydrationRescue(1024 * 1024); // 嘗試騰出 1MB

// 6. 優雅終止核心（HostContext 遵循 RAII 規範，離開作用域時解構式會自動調用 Shutdown()，一般無需手動呼叫）
// 若特殊場景需提前終止，亦可顯式呼叫：host.Shutdown();
```

---

## 🚫 3. 第三方插件呼叫 `Init()` 的防禦機制

若第三方插件在其 DLL 內部嘗試呼叫 `ork::Init()`：
* 核心的**單向不可變防線**會安全拒絕該請求。
* 回傳無效的 `HostContext`（`host.IsValid() == false`）。
* 插件若嘗試在無效的 `HostContext` 上調用任何特權方法，核心立即拋出 `std::runtime_error` 越權異常，徹底隔絕特權穿透！

---

## 🚪 4. 雙層關閉防禦體系：Host RAII 與靜態單例退出防線

為了保證不同呼叫場景下的進程完全安全落地，OuroKore 建立了內外兩層安全關閉體系：

1. **第一道防線：Host 宿主標準 RAII 管理（運行期主動優雅終止）**
   - 主程式持有唯一的 `HostContext`。當 `host` 離開作用域解構時，內部自動調用 `ork_shutdown_runtime()`。
   - 此時進程健康、各執行緒仍可控，核心在名冊獨佔鎖保護下執行 `Registry::Clear()`、排空延遲銷毀隊列、停止背景執行緒並重置狀態。
2. **第二道防線：純外掛環境與進程退出兜底（Exit-Time Lockless Tear-Down）**
   - 在第三方外掛獨立單元測試、或未透過 `HostContext` 初始化的獨立模組中，主程式直接退出進入 CRT `exit()` 與作業系統 Loader Lock 階段。
   - 核心長存的靜態註冊表解構子（`~Registry()`）**自動切換為無鎖兜底模式**（`ClearInternal(false)`），直接執行兩階段無鎖置換與釋放（先清空 Payload 再清空 ControlBlock），絕不嘗試獲取讀寫互斥鎖，徹底避免與 Windows Loader Lock 產生死鎖！
