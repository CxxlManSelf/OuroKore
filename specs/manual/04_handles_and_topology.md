# 04. Handle 拓撲管理系統 (Handles & Topology)

OuroKore 透過三種關鍵代數類別，精準表達物件圖中各種複雜的持有、引用與生命週期關係。

---

## 🗂️ Handle 類別分工總覽

| 類別 | 持有權屬性 | 邊緣拓撲登記 | 適用場景 |
| :--- | :--- | :--- | :--- |
| **`OwningHandle<T>`** | 強擁有權 (Strong) | 自動向父物件登記 Slot | 單一子物件、樹狀關聯、圖內部雙向互指 |
| **`OwningContainerHandle`** | 強擁有權 (Strong) | 自動向父物件登記動態容器 | 道具清單、可變子節點集合 |
| **`WeakHandle<T>`** | 弱引用 (Weak/Non-owning) | 不占用拓撲邊緣 | 外部旁路觀察、動態 DLL 模組防釘死、快取索引 |
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

在傳統 `std::shared_ptr` 下，雙向互指會造成嚴重的記憶體洩漏；但在 OuroKore 中：
* **業務圖內部雙向互指請直接使用 `OwningHandle`！**
* 當整個互指圖的外部根引用（`OuroPtr`）皆消失時，底層 `CycleCollector` 會在背景非同步偵測到循環孤島，自動執行外科手術斷鏈並安全回收，開發者無須承擔心理負擔。

---

## 3. `WeakHandle<T>`：解耦弱引用與防釘死保護

若物件需要關聯一個「隨時可能被卸載、摧毀或替換」的外部服務或模組，使用 `WeakHandle`：

```cpp
class CombatSystem : public ork::OuroObject {
public:
    ork::WeakHandle<ork::OuroObject> m_ai_module;

    void ExecuteAI() {
        // 原子鎖定晉升：防範 TOCTOU 競態與野指標
        if (auto ai = m_ai_module.LockAndAcquire()) {
            // 目標活躍在線且已取得棧上保護，安全執行
            std::cout << "AI 模組在線！" << std::endl;
        } else {
            // 目標已銷毀或卸載，內部自動完成惰性修剪 (Lazy Pruning)
            std::cout << "AI 模組不存在或已被卸載" << std::endl;
        }
    }
};
```

---

## 4. `OuroPtr<T>`：棧上生命週期守衛

`OuroPtr` 代表活躍的「根引用（Root Edge）」。只要有任何執行緒在棧上持有某物件的 `OuroPtr`：
* 核心 100% 保證：**該物件絕不會被自動脫水或銷毀！**
* 具備完整指針語意（`->`、`*`、`bool` 檢查）。
