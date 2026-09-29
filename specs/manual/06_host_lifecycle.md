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

// 6. 動態模組載入器生命週期錨定（防止脫水物件提早卸載 DLL 引發復水 Crash）
host.SetObjectModuleLoader(obj_id, plugin_dll);
auto loader = host.GetObjectModuleLoader(obj_id);

// 7. 優雅終止核心（HostContext 遵循 RAII 規範，離開作用域時解構式會自動調用 Shutdown()，一般無需手動呼叫）
// 若特殊場景需提前終止，亦可顯式呼叫：host.Shutdown();
```

---

## 🚫 3. 第三方插件呼叫 `Init()` 的防禦機制

若第三方插件在其 DLL 內部嘗試呼叫 `ork::Init()`：
* 核心的**單向不可變防線**會安全拒絕該請求。
* 回傳無效的 `HostContext`（`host.IsValid() == false`）。
* 插件若嘗試在無效的 `HostContext` 上調用任何特權方法，核心立即拋出 `std::runtime_error` 越權異常，徹底隔絕特權穿透！

---

## 🧩 4. 動態外掛生命週期錨定 (Dynamic Plugin Life-Bound Invariant)

在模組化架構中，插件常以動態庫（DLL / SO）形式載入並建立領域物件。當該物件在記憶體中發生**自動脫水（Dehydrate）**時，記憶體中的實體 Payload 會被釋放：
* 若動態庫僅由外層臨時指標持有，一旦呼叫端指標釋放，動態庫引用計數歸零而提早自進程中卸載。
* 隨後若業務再度存取該物件觸發透明復水（Rehydration），底層呼叫復水回呼（`RehydrateCallback`）或解構式時，該記憶體位址已成為無效代碼段，引發嚴重的非法記憶體存取崩潰（Access Violation / Segmentation Fault）。

為徹底杜絕此問題，宿主可透過 `HostContext` 將動態庫實例 `ork::DynamicLibrary` 綁定至該物件的控制區塊（ControlBlock）：
```cpp
// 宿主載入外掛並建立物件
ork::DynamicLibrary plugin("MyPlugin");
auto create_fn = plugin.get<CreateMyObjectFn>("CreateMyObject");
auto my_obj = create_fn();

// 將動態外掛載入器錨定於受管物件之 ControlBlock 墓碑
host.SetObjectModuleLoader(my_obj.GetTargetID(), plugin);
```

### 4.1 最小特權委派：`IObjectModuleBinder` 介面隔離
若專案中有專門負責載入與管理動態外掛的專職單元（如 `PluginManager` 或 `ComponentFactory`），直接將整個 `HostContext` 傳遞給它是過度授權且危險的（專職單元不應具備調用 `Shutdown()`、`FlushStorage()` 等進程級最高特權的能力）。

因此，`HostContext` 提供了介面隔離功能：
```cpp
// 宿主取得輕量之專用模組綁定介面
std::shared_ptr<ork::IObjectModuleBinder> binder = host.GetModuleBinder();

// 將 binder 交給專職外掛管理單元，該單元僅能綁定模組，完全無法碰觸進程級特權
plugin_manager.Init(binder);
```
專職單元僅需引用 `<ourokore/host/IObjectModuleBinder.hpp>`，即可在物件生成時為其錨定動態庫：
```cpp
binder->SetObjectModuleLoader(obj_id, plugin_loader);
```
**安全保證**：
ControlBlock 墓碑在物件脫水期間長存，其內部持有的 `DynamicLibrary` 引用計數絕不歸零；唯有當物件被徹底物理銷毀（ControlBlock 解構）時，動態庫才會在所有物件全數解構後安全卸載。

