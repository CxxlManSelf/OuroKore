# 04. Handle 拓撲管理系統 (Handles & Topology)

OuroKore 透過三種關鍵代數類別，精準表達物件圖中各種複雜的持有、引用與生命週期關係。

---

## 🗂️ Handle 類別分工總覽

| 類別 | 持有權屬性 | 邊緣拓撲登記 | 適用場景 |
| :--- | :--- | :--- | :--- |
| **`OwningHandle<T>`** | 強擁有權 (Strong) | 自動向父物件登記 Slot | 單一子物件、樹狀關聯、圖內部雙向互指 |
| **`OwningContainerHandle`** | 強擁有權 (Strong) | 自動向父物件登記動態容器 | 道具清單、可變子節點集合 |
| **`UnboundHandle<T>`** | 無繫結弱引用 (Unbound/Non-owning) | 不占用拓撲邊緣 | 外部旁路觀察、動態 DLL 模組非同步熱卸載防釘死、快取索引 |
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

## 3. `UnboundHandle<T>`：解耦弱引用與動態外掛非同步熱卸載

### 🚫 心智模型澄清：`UnboundHandle` 不是拿來破環的！
| 傳統 C++ (`std::weak_ptr`) | OuroKore (`UnboundHandle<T>`) |
| :--- | :--- |
| 主要用於手動打破 `shared_ptr` 的雙向循環引用 | **切勿用於破環！** 業務圖循環參照一律由 `OwningHandle` + `CycleCollector` 負責 |
| 需搭配 `lock()` 換取 `shared_ptr` | 透過 `.LockAndAcquire()` 換取短暫根保護的 `OuroPtr<T>` |
| 僅防單純記憶體洩漏 | **核心使命**：跨動態 DLL / 外掛插件邊界之生命週期解耦、外掛非同步熱卸載防釘死、純唯讀旁路快取 |

若物件需要關聯一個「隨時可能被卸載、摧毀或動態替換」的外部服務、外掛模組或快取索引，使用 `UnboundHandle`：

```cpp
class CombatSystem : public ork::OuroObject {
public:
    ork::UnboundHandle<ork::OuroObject> m_ai_module;

    void ExecuteAI() {
        // 原子鎖定晉升：防範 TOCTOU 競態、野指標與非同步卸載衝突
        if (auto ai = m_ai_module.LockAndAcquire()) {
            // 目標活躍在線且已取得棧上保護，安全執行
            std::cout << "AI 模組在線！" << std::endl;
        } else {
            // 目標已銷毀或正在非同步卸載中，內部自動完成惰性修剪 (Lazy Pruning)
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
