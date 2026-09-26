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
    // 自動向 Boss 註冊名為 "MinionSlot" 的邊緣
    ork::OwningHandle<Monster> m_minion{"MinionSlot"};

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
* **為什麼不需要手動破環？**：因為 OuroKore 配備了進程級背景 `CycleCollector`。當整個互指圖的外部根引用（`OuroPtr`）皆歸零時，收集器會自動在微秒級鎖保護下偵測封閉孤島，並以「外科手術斷鏈」安全解構，絕不造成呼叫堆疊溢位（Stack Overflow）亦無記憶體洩漏。
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

## 4. `OuroPtr<T>`：棧上生命週期守衛

`OuroPtr` 代表活躍的「根引用（Root Edge）」。只要有任何執行緒在棧上持有某物件的 `OuroPtr`：
* 核心 100% 保證：**該物件絕不會被自動脫水或銷毀！**
* 具備完整指針語意（`->`、`*`、`bool` 檢查）。
