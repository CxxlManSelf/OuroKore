# 04. Handle 拓撲管理系統 (Handles & Topology)

OuroKore 透過三種關鍵代數類別，精準表達物件圖中各種複雜的持有、引用與生命週期關係。

---

## 🗂️ Handle 類別分工總覽

| 類別 | 持有權屬性 | 邊緣拓撲登記 | 適用場景 |
| :--- | :--- | :--- | :--- |
| **`OwningHandle<T>`** | 強擁有權 (Strong) | 自動向父物件登記 Slot | 單一子物件、樹狀關聯、圖內部雙向互指 |
| **`OwningContainerHandle`** | 強擁有權 (Strong) | 自動向父物件登記動態容器 | 道具清單、可變子節點集合 |
| **`UnboundHandle<T>`** | 純旁觀弱引用 (Non-owning) | 不占用拓撲邊緣（不干涉生死） | 外部旁路觀察、外掛模組隨時卸載防卡死、UI 暫時看一眼、快取索引 |
| **`OuroPtr<T>`** | 棧上根引用 (Root Edge) | 自動向核心登記 Root | 局部變數、計算過程臨時持有、API 回傳值 |

---

## 1. `OwningHandle<T>`：擁有權插槽

`OwningHandle` 代表父物件對子物件的擁有權。宣告時**必須傳入唯一的插槽名稱（Slot Name）**，物件建構時會自動向底層名冊登記：

```cpp
class Boss : public ork::Subclass<Boss, ork::OuroObject> {
public:
    // 自動向 Boss 註冊名為 "MinionSlot" 的邊緣，支援 C++20 UTF-8 字面量與中文槽位
    ork::OwningHandle<Monster> m_minion{u8"隨從槽位_左"};

    void SetMinion(const ork::OuroPtr<Monster> &m) {
        m_minion.Set(m); // 設定目標並建立強持有邊緣
    }

    ork::OuroPtr<Monster> GetMinion() const {
        return m_minion.Get(); // 匯出棧上安全的 OuroPtr
    }
};
```

---

## 2. 業務圖雙向互指與背景循環回收 (Cycle Collection)

> 🗺️ **互動概念圖導覽**：
> 點擊開啟 **[🗺️ 循環參照收集器與逆向圖論走訪全景概念圖](conceptmaps/cycle_collector_map.html)**，直觀透視「嫌疑犯排隊」、「Upstream BFS 逆向快照」、「四層防禦孤島確認」與「外科手術式靜音斷鏈 (Silent Unbind)」的全流程！

在傳統 C++ (`std::shared_ptr`) 架構下，雙向互指會造成循環引用（Circular Reference），迫使開發者必須小心翼翼地手動將其中一方改為 `std::weak_ptr` 來破環；**但在 OuroKore 體系中，這項心智負擔被徹底消滅**：
* **業務圖內部雙向互指／網狀循環，請一律大膽、直接使用 `OwningHandle`！**
* **為什麼不需要手動破環？**：因為 OuroKore 配備了進程級背景清潔工 `CycleCollector`。

---

### 2.1 循環回收器 (CycleCollector) 核心運作原理

1. **嫌疑犯 O(1) 快速入隊 (Suspect Ingestion)**：
   - 當任何強擁有邊緣斷開（例如父物件釋放了子物件，或呼叫了 `m_minion.Reset()`），若目標物件的強引用計數 `StrongCount > 0`（代表內部仍有互指殘留），系統在常數時間（$O(1)$）透過 CAS 原語將其標記並推入背景 `m_suspect_queue` 佇列。
2. **單一背景清潔工 (Single Background Worker)**：
   - 全進程由唯一背景執行緒負責處理，徹底消滅多執行緒走訪圖結構時產生的哲學家就餐死鎖與 CPU 快取顛簸。
   - 每次喚醒時以 `swap` 批次取出，並在批次內進行去重，消除同環多節點引發的重複走訪。
3. **逆向圖論走訪 (Upstream BFS & Micro-Snapshot)**：
   - 從嫌疑犯出發，依據 `ControlBlock::m_owners` 逆向名冊逐級向上廣度優先回溯（BFS）：
     - **微秒級快照**：僅在極短時間獲取節點之 `m_owners_mutex`，複製快照後立即解鎖，絕不長持互斥鎖。
     - **外部存活根早退 (Root Early Exit)**：只要回溯途中觸及 `ORK_ROOT_ID`（代表有棧上 `OuroPtr` 根引用活躍持有）、或外部非受管節點、或正在兩階段構造中的節點，**宣告存活，立即退出走訪，絕不破壞該物件圖！**
4. **四層防禦協議之孤島確認 (DestructIsland Protocol)**：
   - 若回溯完成未發現任何外部根，初步判定為死結孤島，收集器隨後發動嚴格的四層防禦進行二階段確認：
     - **第一層（HandleID 升冪排序）**：孤島節點按 ID 升冪排列，徹底消滅依序取鎖時的反向死鎖。
     - **第二層（弱引用 Pin 釘住）**：獲取 `ControlBlockPinGuard`，釘住期間 `weak_count >= 1`，保證走訪中 ControlBlock 墓碑絕不被併發銷毀。
     - **第三層（全體鎖定二次複查）**：升冪獲取全體成員的 owners 鎖，檢查是否有外部入邊連入（例如在排隊間隙有其他執行緒加邊連回 Root）；若發現外部入邊，**孤島已復活，立即放棄拆解！**
     - **第四層（原子標記 Destructing 態）**：全體標記 `m_is_destructing = true`，徹底阻斷併發加邊與殭屍物件甦醒。
5. **外科手術式靜音斷鏈 (Silent Unbind) 與平級銷毀**：
   - 釋放 owners 鎖後，以靜音模式（`silent=true`）切斷孤島成員間的相互依賴，避免重複入隊。
   - 斷鏈後強計數跌至 0，孤島成員自動交由 `DeferredDeleteQueue`（延遲銷毀佇列）平行壓平解構釋放肉體記憶體。
   - **收集器絕不阻塞在 `delete` 耗時上**，移交後立即回頭處理下一批任務，保證超高吞吐！

---

### 2.2 開發者使用指南與最佳實踐

* ⚠️ **重要原則**：開發者**絕不應該**為了「破環」而將業務圖內的欄位改成 `UnboundHandle`。物件間的拓撲共生性應由 `OwningHandle` 誠實表達。
* **測試與手動調度同步屏障**：
  在單元測試或特定關卡切換時，若需確保所有殘留孤島立即被回收完畢，可透過宿主特權或核心 API 呼叫顯式回收：
  ```cpp
  // 透過序號屏障委託單一背景執行緒消化排隊批次，調用端安全阻塞等待
  host.CollectCycles(); 
  ```

---

## 3. `UnboundHandle<T>`：純旁觀者句柄（只看不管生死）

### 💡 白話心智模型：
* **`OwningHandle` 是「主管或父母」**：你是我的組員，我對你的工作和生死負責。只要我還需要你，系統就不能把你刪掉。
* **`UnboundHandle` 是「通訊錄」**：我只是把你的電話號碼（ID）記在小本本上。你隨時想離職走人或被公司開除，我都絕不攔你、也絕不霸佔你讓你走不掉。

### 🚫 避坑提醒：千萬不要拿它來「破環」！
| 傳統 C++ (`std::weak_ptr`) | OuroKore (`UnboundHandle<T>`) |
| :--- | :--- |
| 大家常拿來手動打破 `shared_ptr` 的雙向循環死結 | **切勿用於破環！** 業務雙向互指請直接用 `OwningHandle`，交給背景清潔工自動回收 |
| 用 `lock()` 換取 `shared_ptr` | 用 `.LockAndAcquire()` 借用一把安全的短暫保護鎖（`OuroPtr<T>`） |
| 只是普通的弱引用指標 | **核心使命**：我只負責看一眼，絕不干涉對方的生死與卸載！ |

---

### 🎯 什麼時候該用 `UnboundHandle`？（兩個最常見的生活情境）

#### 情境一：外掛模組想走就走（防卡死、熱更新）
想像玩家的主角身上掛了一個「第三方外掛插件提供的自動戰鬥 AI 模組」：
* 如果你用強引用死死抓著它，當玩家想要把外掛關閉、更新外掛 DLL 檔案時，外掛就會因為被你的主角「釘住不放」而卡住卸不掉，甚至導致遊戲崩潰。
* 改用 `UnboundHandle`，外掛隨時可以自己退出下班，主角絕不阻止它走。

#### 情境二：UI 視窗瞄一眼（只觀察、不養它）
玩家在畫面上點選了一隻怪物，UI 彈出一個小視窗顯示怪物的即時血條：
* 怪物如果被其他隊友打死了、或地圖重置了，怪物就應該乾淨俐落地消失。
* UI 視窗千萬不能強行把怪物扣在記憶體裡（否則怪物死了屍體還一直不被釋放）。UI 只要用 `UnboundHandle` 瞄一眼，怪物沒了 UI 就關閉或顯示空值。

---

### 💻 程式碼怎麼寫？先「打電話確認還在不在」（`LockAndAcquire`）

因為對方隨時可能離開，你不能直接拿它來操作。每次要用的時候，只要做一件事：

```cpp
class CombatSystem : public ork::Subclass<CombatSystem, ork::OuroObject> {
public:
    // 旁觀者句柄：只記住模組號碼，不干涉其生死
    ork::UnboundHandle<ork::OuroObject> m_ai_module;

    void ExecuteAI() {
        // 像打電話一樣：確認對方還在不在？如果還在，暫時借用一把安全鎖（OuroPtr）
        if (auto ai = m_ai_module.LockAndAcquire()) {
            // 對方還在線！在 if 作用域內，系統保證對方絕對不會突然消失
            std::cout << "AI 模組在線，執行運算！" << std::endl;
        } else {
            // 對方已經下班、被銷毀或外掛已卸載
            // 此時系統會自動把通訊錄擦乾淨（下次就不用白問了）
            std::cout << "AI 模組已不存在或已被卸載" << std::endl;
        }
    }
};
```

---

## 4. 句柄晉升與脫水排隊同步防線 (LockAndAcquire Anti-TOCTOU & Dehydration Sync)

為了在多執行緒併發、物件頻繁脫水/復水以及藍綠熱載入（Load）環境下徹底杜絕交付「半死物件（Zombie Object）」，`OwningHandle`、`OwningContainerHandle` 與 `UnboundHandle` 的 `.LockAndAcquire()` 具備完全對稱的雙重防禦機制：

1. **原子活體判定與邊緣登記（Anti-Zombie Invariant）**：
   - 內部呼叫 `ork_try_lock_weak(tid)` 進行原子檢驗。若目標物件已被推入延遲刪除隊列（`StrongCount == 0`）或已被標記為孤島拆解中（`m_is_destructing == true`），**嚴禁死者甦醒，立即返回空指標 `OuroPtr()`**，絕不交付半死物件給調用端。
2. **脫水排隊同步防線（Dehydration Synchronization）**：
   - 若目標物件此時正處於脫水獨佔寫鎖（`m_rw_lock`）臨界區中正在釋放肉體記憶體，`LockAndAcquire` 會透過短暫獲取共享讀鎖**自動排隊等待脫水完成**；
   - 確保返回之 `OuroPtr` 物件處於穩定的存活態或脫水態，後續調用順暢觸發透明復水，絕不與脫水流程搶跑碰撞。
3. **Anti-TOCTOU 嚴格回傳檢查與邊緣回滾**：
   - 若在排隊等鎖期間物件剛好被 `CycleCollector` 判定為孤島並標記拆解，核心讀鎖內部自動二次確認解鎖並回傳失敗；
   - 調用端立即**回滾剛登記的 ROOT 邊緣**（`UnboundHandle` 同步觸發怠速修剪 CAS tid -> 0），**絕不重複解鎖**，安全返回空指標。

---

### 🧩 外掛模組載入、生命週期綁定與自動卸載（`DynamicLibrary`）

在外掛管理端，載入與卸載動態庫（DLL）時請遵循以下黃金準則：
1. **禁絕手動卸載**：`DynamicLibrary` 刻意不提供手動 `unload()` 介面，以防提早手動卸載導致正在執行的物件虛擬函式表 (vtable) 與成員函式代碼段失效引發記憶體崩潰。
2. **物件生命週期反向錨定**：透過 `bind_lifecycle()` 將產生的外掛物件與動態庫綁定，當該外掛產生的所有物件全部解構後，底層動態庫才會在引用計數歸零時自動安全卸載。
3. **⚠️ 關鍵約束（load 回傳值之生命週期約束）**：
   `ork::DynamicLibrary::load()` 的回傳值本身「已經將動態庫綁定（持有引用計數）」。**若呼叫端不放棄此回傳值變數（如長存於成員/全域變數、或外層未離開作用域/未呼叫 `reset()`），DLL 是絕對不會被卸載的！**
   呼叫端必須在完成物件綁定後，主動呼叫 `lib.reset()` 或讓其隨工廠作用域自然解構，將唯一的存活權杖全權移交給產生的物件持有。
4. **模組全域啟始與收尾保證（Lifecycle Hooks & Startup/Shutdown Protocol）**：
   - **首次載入精準辨識**：多個模組重複呼叫 `load()` 請求載入相同動態庫時，載入器透過全域規範路徑弱引用快取共享控制區塊。只有第一次進入進程（0 -> 1）時 `lib.is_first_loaded()` 會傳回 `true`；後續重複載入（N -> N+1）傳回 `false`。
   - **全域啟始單次保證**：呼叫 `lib.initialize_once<InitFn>("ork_plugin_init", args...)`，僅在首次載入時執行初始化（避免型別重複註冊或資源衝突），重複載入時自動安全略過。
   - **模組唯一善後收尾與常駐模式 (Terminal Shutdown & Resident Mode)**：透過 `lib.register_shutdown_symbol("ork_plugin_shutdown")` 或 `lib.set_shutdown_hook(...)` 註冊收尾邏輯。外掛在引用歸零時首先執行此唯一入口；若外掛回傳 `false` 拒絕結束，系統自動轉為【常駐模式】，不呼叫 `FreeLibrary`、不發送後置通知，並將原生句柄轉存至全域常駐表（`resident_table`）。**日後若再次呼叫 `load()` 重新載入該模組，系統在常駐表重用該句柄時會立即將其移出常駐表，將生命週期所有權交由新控制區塊託管（接管即移出所有權）**；若新控制區塊日後釋放且外掛同意卸載，則正常物理卸載，確保常駐表絕無懸空指標殘留！若外掛初次即同意結束（回傳 `true` 或 `void`），則正常物理卸載並觸發 `post_unload_hooks`。

#### 實戰範例：
```cpp
#include <ourokore/base/DynamicLibrary.hpp>

// 1. 載入外掛 DLL（load 回傳值持有一份引用計數 1）
auto lib = ork::DynamicLibrary::load("AIPlugin.dll");
if (!lib) {
    std::cerr << "外掛載入失敗: " << lib.get_last_error() << std::endl;
    return;
}

// 2. 模組全域啟始與收尾協定（首次載入時執行初始化，並註冊卸載前收尾）
// 💡 若先前其他模組已載入過此 DLL，initialize_once 會自動安全略過，避免二次初始化！
using PluginInitFn = int32_t (*)(void* host_context);
lib.initialize_once<PluginInitFn>("ork_plugin_init", host_context_ptr);

// 註冊卸載前收尾回呼：保證在所有持有者與物件解構、DLL 真正被卸載前一刻調用
lib.register_shutdown_symbol("ork_plugin_shutdown");

// 3. 獲取工廠函式符號
auto create_fn = lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyAIPlugin");

// 4. 建立實體並透過 bind_lifecycle 綁定生命週期（此時引用計數累加）
auto ai_raw = create_fn();
std::shared_ptr<IAIPlugin> ai_instance = lib.bind_lifecycle(ai_raw, destroy_fn);

// 5. ⚠️ 關鍵：呼叫端主動放棄 load() 回傳的初始句柄！
// 若呼叫端未來仍可能需要使用該動態庫，可在 reset() 之前保留一份弱引用觀察者：
ork::WeakDynamicLibrary weak_lib = lib.to_weak();
lib.reset(); // 放棄持有權，引用計數扣減，存活權杖全權移交給 ai_instance

// 6. 業務安全使用：ai_instance 存活期間 DLL 代碼段絕不被卸載
ai_instance->ExecuteAI();

// 6.1 再次使用需求（弱引用晉升重獲）：
// 主程式若日後需要再次建立新物件或呼叫函式，可透過 weak_lib.lock() 嘗試晉升為強引用：
if (auto locked_lib = weak_lib.lock()) {
    // 晉升成功！先前產生的物件仍存活，DLL 仍在記憶體中，無須重新走 OS LoadLibrary
    auto create_fn2 = locked_lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
    // 使用完畢後 locked_lib 隨作用域解構，不影響自動卸載邏輯
}

// 7. 當外掛生命週期結束、所有持有 ai_instance 的物件全部解構歸零後：
// -> 自動觸發已註冊的 ork_plugin_shutdown() 收尾
// -> 底層自動安全執行 FreeLibrary / dlclose 卸載！
ai_instance.reset(); 
// 此時 weak_lib.expired() == true，weak_lib.lock() 安全傳回無效實例
```

#### 🛡️ 受管物件與 DynamicLibrary 之「即時解錨」保證：
在 OuroKore 託管體系中，若透過 `HostContext::SetObjectModuleLoader` 或專職介面 `IObjectModuleBinder` 將動態庫綁定至受管物件：
* **脫水長存**：物件脫水（Dehydrated）落盤期間，DLL 絕對保留不被卸載，確保透明復水時程式碼段 100% 有效。
* **Payload 銷毀即刻解錨**：當物件最後一個強引用歸零並由 `DeferredDeleteQueue` 物理銷毀其 Payload 後，**核心會立即在核心空間主動釋放對 DynamicLibrary 的引用**。
* **弱引用/墓碑零阻礙**：即使該物件仍被 `UnboundHandle`（弱引用）指向使其 ControlBlock 墓碑長存於記憶體，動態模組也絕不會被鎖死，得以在所有實體銷毀後第一時間安全卸載！


---

## 4. `OuroPtr<T>`：棧上生命週期守衛與極致並發防禦

`OuroPtr` 代表活躍的「根引用（Root Edge）」。只要有任何執行緒在棧上持有某物件的 `OuroPtr`：
* 核心 100% 保證：**該物件絕不會被自動脫水或銷毀！**
* **安全轉發執行 (Zero Raw Pointer Guarantee)**：徹底拔除 `get()`、`operator->` 與 `operator*`，透過 `operator()(Fn&&, Args&&...)` 或 `Invoke(...)` 調用成員函式、成員欄位或 Lambda 閉包，徹底杜絕裸指標逃逸與 UAF 漏洞。
* **原生極速延遲快取**：首次呼叫時透明復水並快取記憶體指標，後續呼叫直接以 $O(1)$ 純暫存器/記憶體原生速度執行。

### 4.1 在途調用釘住（In-Flight Pinning）與雙世代防護
為了在多執行緒環境下兼顧原生極速與 100% 記憶體安全，`OuroPtr` 整合了現代化的**在途調用釘住（In-Flight Pinning）**機制：
1. **進入即釘住**：呼叫 `operator()` 或 `WithObject` 時，內部自動建立 `InFlightPinGuard`，原子遞增當前世代槽位之釘住計數。
2. **免鎖並發讀取**：調用期間即使其他執行緒並發呼叫 `ork::Load()` 進行藍綠置換，置換端在釋放舊實體前必定等待舊世代釘住計數歸零（Drain），保證當前正在執行的成員函式實體記憶體絕對不被提前釋放！
3. **雙世代消除活鎖**：釘住計數器採用雙世代槽位（`m_pins[2]`），置換時遞增世代號，新調用自動路由至新世代槽位，徹底消滅高頻讀取下的活鎖飢餓（Live-Lock）。
4. **原子快取指標與世代失效**：`m_cached_ptr` 與 `m_cached_generation` 全面採用 `std::atomic` 原語，多執行緒共享同一個 `const OuroPtr&` 並發存取 100% 資料競爭自由（Data-Race Free）。一旦偵測到世代切換，舊快取指標原子失效並自動重新獲取最新實體。

### 4.2 孤島拆解態與死亡對象 Fail-Fast 防線
若目標物件已被標記為孤島拆解中（`m_is_destructing == true`）或已無效死亡：
* `InFlightPinGuard` 於建構時第一時間偵測到異常，判定釘住失敗（`IsPinned() == false`）。
* `WithObject` 立刻原子清空快取指標並拋出 `std::runtime_error` 異常，**絕不盲目穿透至成員函式調用**，徹底杜絕釋放後使用（Use-After-Free）。
