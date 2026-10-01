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
class Boss : public ork::OuroObject {
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

在傳統 C++ (`std::shared_ptr`) 架構下，雙向互指會造成循環引用（Circular Reference），迫使開發者必須小心翼翼地手動將其中一方改為 `std::weak_ptr` 來破環；**但在 OuroKore 體系中，這項心智負擔被徹底消滅**：
* **業務圖內部雙向互指／網狀循環，請一律大膽、直接使用 `OwningHandle`！**
* **為什麼不需要手動破環？**：因為 OuroKore 配備了進程級背景 `CycleCollector`。
  * **孤島判定條件**：當整個互指圖**失去所有來自環外的強引用持有**時——無論是直接持有環內節點的棧上根引用（`OuroPtr`）歸零，或是上游父物件斷開了指向該圖的 `OwningHandle` 入邊——整張互指圖在圖論上將無法向上溯源至任何 Root 根節點，形成「自娛自樂」的封閉孤島。
  * **背景非同步解構**：收集器會自動在微秒級鎖保護下透過逆向廣度優先搜尋（Upstream BFS）偵測並鎖定孤島，隨後以「外科手術式斷鏈（Silent Unbind）」切斷環內互指邊緣並交由延遲銷毀佇列平行釋放，絕不造成呼叫堆疊溢位（Stack Overflow）亦無記憶體洩漏。
* ⚠️ **重要原則**：開發者**絕不應該**為了「破環」而將業務圖內的欄位改成 `UnboundHandle`。物件間的拓撲共生性應由 `OwningHandle` 誠實表達。

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
class CombatSystem : public ork::OuroObject {
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

### 🧩 外掛模組載入、生命週期綁定與自動卸載（`DynamicLibrary`）

在外掛管理端，載入與卸載動態庫（DLL）時請遵循以下黃金準則：
1. **禁絕手動卸載**：`DynamicLibrary` 刻意不提供手動 `unload()` 介面，以防提早手動卸載導致正在執行的物件虛擬函式表 (vtable) 與成員函式代碼段失效引發記憶體崩潰。
2. **物件生命週期反向錨定**：透過 `bind_lifecycle()` 將產生的外掛物件與動態庫綁定，當該外掛產生的所有物件全部解構後，底層動態庫才會在引用計數歸零時自動安全卸載。
3. **⚠️ 關鍵約束（load 回傳值之生命週期約束）**：
   `ork::DynamicLibrary::load()` 的回傳值本身「已經將動態庫綁定（持有引用計數）」。**若呼叫端不放棄此回傳值變數（如長存於成員/全域變數、或外層未離開作用域/未呼叫 `reset()`），DLL 是絕對不會被卸載的！**
   呼叫端必須在完成物件綁定後，主動呼叫 `lib.reset()` 或讓其隨工廠作用域自然解構，將唯一的存活權杖全權移交給產生的物件持有。

#### 實戰範例：
```cpp
#include <ourokore/base/DynamicLibrary.hpp>

// 1. 載入外掛 DLL（load 回傳值持有一份引用計數 1）
auto lib = ork::DynamicLibrary::load("AIPlugin.dll");
if (!lib) {
    std::cerr << "外掛載入失敗: " << lib.get_last_error() << std::endl;
    return;
}

// 2. 獲取工廠函式符號
auto create_fn = lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyAIPlugin");

// 3. 建立實體並透過 bind_lifecycle 綁定生命週期（此時引用計數為 2）
auto ai_raw = create_fn();
std::shared_ptr<IAIPlugin> ai_instance = lib.bind_lifecycle(ai_raw, destroy_fn);

// 4. ⚠️ 關鍵：呼叫端主動放棄 load() 回傳的初始句柄！
// 若呼叫端未來仍可能需要使用該動態庫，可在 reset() 之前保留一份弱引用觀察者：
ork::WeakDynamicLibrary weak_lib = lib.to_weak();
lib.reset(); // 放棄持有權，引用計數降為 1，此時 DLL 存活權杖全權移交給 ai_instance

// 5. 業務安全使用：ai_instance 存活期間 DLL 代碼段絕不被卸載
ai_instance->ExecuteAI();

// 5.1 再次使用需求（弱引用晉升重獲）：
// 主程式若日後需要再次建立新物件或呼叫函式，可透過 weak_lib.lock() 嘗試晉升為強引用：
if (auto locked_lib = weak_lib.lock()) {
    // 晉升成功！先前產生的物件仍存活，DLL 仍在記憶體中，無須重新走 OS LoadLibrary
    auto create_fn2 = locked_lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
    // 使用完畢後 locked_lib 隨作用域解構，不影響自動卸載邏輯
}

// 6. 當外掛生命週期結束、所有持有 ai_instance 的物件全部解構歸零後，DLL 自動在底層卸載！
ai_instance.reset(); // 底層自動安全執行 FreeLibrary / dlclose
// 此時 weak_lib.expired() == true，weak_lib.lock() 安全傳回無效實例
```

#### 🛡️ 受管物件與 DynamicLibrary 之「即時解錨」保證：
在 OuroKore 託管體系中，若透過 `HostContext::SetObjectModuleLoader` 或專職介面 `IObjectModuleBinder` 將動態庫綁定至受管物件：
* **脫水長存**：物件脫水（Dehydrated）落盤期間，DLL 絕對保留不被卸載，確保透明復水時程式碼段 100% 有效。
* **Payload 銷毀即刻解錨**：當物件最後一個強引用歸零並由 `DeferredDeleteQueue` 物理銷毀其 Payload 後，**核心會立即在核心空間主動釋放對 DynamicLibrary 的引用**。
* **弱引用/墓碑零阻礙**：即使該物件仍被 `UnboundHandle`（弱引用）指向使其 ControlBlock 墓碑長存於記憶體，動態模組也絕不會被鎖死，得以在所有實體銷毀後第一時間安全卸載！


---

## 4. `OuroPtr<T>`：棧上生命週期守衛

`OuroPtr` 代表活躍的「根引用（Root Edge）」。只要有任何執行緒在棧上持有某物件的 `OuroPtr`：
* 核心 100% 保證：**該物件絕不會被自動脫水或銷毀！**
* **安全轉發執行 (Zero Raw Pointer Guarantee)**：徹底拔除 `get()`、`operator->` 與 `operator*`，透過 `operator()(Fn&&, Args&&...)` 或 `Invoke(...)` 調用成員函式、成員欄位或 Lambda 閉包，徹底杜絕裸指標逃逸與 UAF 漏洞。
* **原生極速延遲快取**：首次呼叫時透明復水並快取記憶體指標，後續呼叫直接以 $O(1)$ 純暫存器/記憶體原生速度執行。
