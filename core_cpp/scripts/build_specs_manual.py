# -*- coding: utf-8 -*-
"""
生成 specs/manual/ 目錄下所有使用者說明書手冊
"""
from pathlib import Path

def generate_manual_specs(specs_dir: Path):
    manual_dir = specs_dir / "manual"
    manual_dir.mkdir(parents=True, exist_ok=True)

    # 01_introduction.md
    (manual_dir / "01_introduction.md").write_text('''# 01. OuroKore 系統概述與架構哲學

## 📖 什麼是 OuroKore？

**OuroKore** 是一個專為**超大規模物件圖（Large-Scale Object Graph）**、**極致執行緒安全**、**記憶體吃緊時自動換頁脫水（Dehydration/Rehydration）**以及**藍圖持久化打包（Blueprint Packaging）**所設計的高效能 C++ 領域物件託管系統。

它廣泛適用於：
* 3A 遊戲世界實體與階層拓撲管理（角色、裝備、技能樹、場景物件）。
* 大規模世界編輯器與 CAD 節點圖系統。
* 需要高併發讀寫、記憶體配額受限且須透明落盤的高效能後端架構。

---

## 🧭 核心心智模型與三大基石

```
+-----------------------------------------------------------------------+
|                             OuroKore 架構                             |
+------------------------------------+----------------------------------+
| 1. 控制區塊與 Handle 代數系統      | 2. 透明換頁脫水與復水系統        |
|    - ControlBlock 跨模組託管       |    - 四態生命週期 (StorageState)  |
|    - OwningHandle (強擁有權邊緣)   |    - 內建 LRU 自動淘汰換頁       |
|    - UnboundHandle (無繫結解耦引用)|    - Transparent Rehydration     |
|    - OuroPtr (棧上安全根守衛)      |    - OOM 緊急自救脫水救援        |
+------------------------------------+----------------------------------+
| 3. 純 Payload 與拓撲分離藍圖打包    | 4. 三層權限隔離與 HostContext    |
|    - Pure Payload 序列化           |    - HostContext 獨佔特權        |
|    - Edge Roster 拓撲槽位自動打包  |    - Plugin 物理隔離零洩漏       |
|    - 兩階段套用與例外安全防禦      |    - 純 C ABI 底座多語言支援     |
+------------------------------------+----------------------------------+
```

### 1. 控制區塊與 Handle 代數系統 (ControlBlock & Handle System)
* **禁止裸指標**：領域物件絕不直接由 `new`/`delete` 或裸指標管理，而是統一配發全域唯一的 64 位元 `HandleID`，由底層控制區塊（`ControlBlock`）託管。
* **強邊界與弱引用分工**：
  * `OwningHandle<T>`：宣告單一子物件擁有權插槽（邊緣拓撲），形成清晰的父子持有樹。業務圖內部雙向與互指關聯亦放膽使用，由背景 `CycleCollector` 自動偵測孤島並非同步消化。
  * `OwningContainerHandle`：宣告動態子物件容器（如背包道具清單、子節點陣列）。
  * `UnboundHandle<T>`：純旁觀者句柄（只記住對方號碼，絕不干涉生死）。專為「外掛隨時卸載防卡死」、「UI 視窗暫時看一眼」等旁路觀察設計。要使用時先確認對方是否還在（`LockAndAcquire()`），若對方已離職或被銷毀則自動傳回空值並清理記錄，絕不卡住對方的釋放流程。
  * `OuroPtr<T>`：棧上或全域根引用守衛（Root Edge），內部自動調用 `ork_acquire_object_pointer` 與讀寫鎖，保證在活躍存取期間物件絕不被脫水或物理銷毀。

### 2. 記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)
* **儲存狀態四態模型**：`UnsavedNew(0)`、`Clean(1)`、`Dirty(2)`、`Dehydrated(3)`。
* **無感自動脫水**：當記憶體達到上限時，由脫水器（如內建的 `OuroLRUAutoDehydrator`）選出最久未被存取的冷物件，將其 Payload 序列化落盤至持久化驅動（`IStorageDriver`），隨後釋放實體記憶體，保留控制區塊墓碑。
* **透明按需復水**：當程式再次存取已脫水物件時，ControlBlock 透過預先註冊的回呼透明地重新配置空殼實體、讀取藍圖還原狀態並重新綁定指標。呼叫端完全無須編寫任何載入代碼！

### 3. 三層架構與邊界隔離原則
OuroKore 嚴格劃分三大權限層級：
1. **主程式宿主層（Host Application）**：透過 `HostContext` 獨佔進程生命週期、執行緒池注入、自動脫水策略與特權維護功能。
2. **組件與插件層（Component / Plugin）**：僅能使用受管物件、安全指標、Handle 拓撲與讀寫鎖，物理隔絕所有破壞性特權。
3. **底層純 C ABI（Cross-Language FFI）**：所有底層操作以純整數狀態碼、HandleID 與 C 函式指標封裝，100% 杜絕 C++ 例外跨動態庫逃逸，為未來綁定 C#、Rust、Python 提供完備基礎。
''', encoding="utf-8")

    # 02_quickstart.md
    (manual_dir / "02_quickstart.md").write_text('''# 02. 5 分鐘快速上手 (Quickstart)

本章節帶領您從零開始建立第一個 OuroKore 應用程式，體驗宿主初始化、自訂領域物件、屬性讀寫與序列化存檔。

---

## 🛠️ 第一步：主程式初始化核心 (Host Entry Point)

在應用程式進入點（如 `main()`）中，透過 `ork::Init()` 取得唯一的 **`HostContext`**：

```cpp
#include <iostream>
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>

int main() {
    // 1. 配置儲存驅動（以記憶體驅動為例）
    auto storage = std::make_shared<ork::InMemoryStorage>();

    // 2. 初始化核心並獲取宿主特權控制物件（RAII 自動管理生命週期）
    ork::HostContext host = ork::Init(storage);
    if (!host.IsValid()) {
        std::cerr << "核心已被其他主程式初始化！" << std::endl;
        return 1;
    }

    std::cout << "OuroKore 核心啟動成功！" << std::endl;

    // 當離開 main() 時，host 解構會自動呼叫 host.Shutdown() 優雅退出
    return 0;
}
```

---

## 📦 第二步：定義自訂領域物件 (Define OuroObject)

凡是交由 OuroKore 託管的領域物件（透過 `ork::CreateObject<T>()` 建立者），**一律強制繼承自 `ork::Subclass<T, Base = ork::OuroObject>` 樣板基底**。
**嚴格禁止直接裸繼承 `OuroObject`**（如 `class Player : public ork::OuroObject`），直接繼承將在編譯期被 `static_assert` 阻擋；亦禁止外部直接 `new`：

```cpp
#include <ourokore/component/OuroCore.hpp>
#include <string>

class Player : public ork::Subclass<Player, ork::OuroObject> {
public:
    Player() = default;

    // 執行緒安全之屬性存取介面
    std::string GetName() const {
        ork::OuroReadLock lock(*this); // 取得共享讀鎖
        return m_name;
    }

    void SetName(std::string name) {
        ork::OuroWriteLock lock(*this); // 取得獨占寫鎖；解構時自動原子標記 Dirty！
        m_name = std::move(name);
    }

    int32_t GetScore() const {
        ork::OuroReadLock lock(*this);
        return m_score;
    }

    void AddScore(int32_t delta) {
        ork::OuroWriteLock lock(*this);
        m_score += delta;
    }

    // 實作純 Payload 序列化協議
    void SerializePayload(ork::OuroStream &stream) const override {
        stream.WriteProperty("name", m_name);
        stream.WriteProperty("score", m_score);
    }

    void DeserializePayload(ork::OuroStream &stream) override {
        stream.ReadProperty("name", m_name);
        stream.ReadProperty("score", m_score);
    }

private:
    std::string m_name{"Knight"};
    int32_t     m_score{0};
};
```

---

## 🚀 第三步：建立物件與存檔

使用 `ork::CreateObject<T>()` 實例化領域物件，取得棧上保護的 `ork::OuroPtr<T>`：

```cpp
// 1. 建立玩家物件
ork::OuroPtr<Player> player = ork::CreateObject<Player>();
player(&Player::SetName, "亞瑟王");
player(&Player::AddScore, 100);

// 2. 存檔至儲存驅動
bool save_ok = ork::Save(player);
assert(save_ok == true);

// 3. 取得物件全域唯一識別碼
ork::HandleID pid = player.GetTargetID();
std::cout << "玩家建立成功，HandleID: " << pid << std::endl;
```

---

## 🌳 第四步：使用樹狀結構容器與文字 DSL 配置 (Tree & TreeIO)

除了託管型持久化物件外，OuroKore 還提供了高效能、雙模態統合的通用樹狀容器與文字 DSL 串流工具（位於 `<ourokore/base/Tree.hpp>` 與 `<ourokore/base/TreeIO.hpp>`），非常適合用於遊戲設定檔、屬性樹、技能樹與文字 DSL 讀寫：

```cpp
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using ork::base::Tree; // 即 ork::base::StringTreeNode
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根節點
auto config = Tree::CreateRoot(u8"GameConfig");
config->SetData("1.0.0");

// 2. 建立具名子節點 (O(1) 雜湊尋址) 與陣列清單 (O(1) 連續記憶體隨機下標)
auto server = config->AddChild(u8"Server");
server->AddChild(u8"IP")->SetData("127.0.0.1");
server->AddChild(u8"Port")->SetData("8080");

auto channels = config->AddChild(u8"Channels");
channels->AddChild()->SetData("General");
channels->AddChild()->SetData("Trade");

// 3. 輸出文字 DSL（支援標準美化與 3 種緊湊輸出）
std::string dsl = TreeIO::SerializeToString(config, CompactMode::WithEqual);
std::cout << "匯出 DSL: " << dsl << std::endl;
// 輸出: [GameConfig]="1.0.0"{[Server]{[IP]="127.0.0.1"[Port]="8080"}[Channels]("General""Trade")}

// 4. 寬容型狀態機反序列化（原生支援 //、/* */ 與 # 註解過濾）
std::string input_dsl = R"(
    // 伺服器啟動設定檔
    # 請勿任意變更 IP 參數
    [GameConfig] = "1.0.0"
    {
        /* 內部連線設定 */
        [Server]
        {
            [IP] = "127.0.0.1" // 本機監聽
            [Port] = "8080"
        }
        [Channels] = (
            "General"
            /* 暫時關閉頻道："PVP" */
            "Trade"
        )
    }
)";

auto restored = TreeIO::DeserializeFromString(input_dsl);
assert((*restored)[u8"Server"][u8"Port"]->GetData() == "8080");
assert((*restored)[u8"Channels"][0]->GetData() == "General");
```
''', encoding="utf-8")

    # 03_domain_object_design.md
    (manual_dir / "03_domain_object_design.md").write_text('''# 03. 領域物件設計規範 (Domain Object Design)

本章節介紹如何遵循 OuroKore 規範設計高效能、多執行緒安全的領域模型物件。

---

## 🔒 1. 執行緒安全與自動 Dirty 標記（重要！）

在多執行緒併發環境下，手動維護「物件是否被修改（Dirty 狀態）」非常容易遺漏或產生 Data Race。OuroKore 採用 **RAII 獨占寫鎖與原子標髒** 的一體化設計：

```cpp
class Character : public ork::Subclass<Character, ork::OuroObject> {
public:
    // 讀取：使用 OuroReadLock（多個讀取者可同時併發）
    int32_t GetHp() const {
        ork::OuroReadLock lock(*this);
        return m_hp;
    }

    // 修改：使用 OuroWriteLock（獨占鎖）
    // 當 lock 解構離開作用域時，內部自動調用 ork_mark_dirty() 原子標記 Dirty！
    void SetHp(int32_t hp) {
        ork::OuroWriteLock lock(*this);
        m_hp = hp;
    }

    // 跨多屬性複合操作
    void TakeDamage(int32_t damage) {
        ork::OuroWriteLock lock(*this);
        m_hp = std::max(0, m_hp - damage);
        if (m_hp == 0) {
            m_is_dead = true;
        }
    }

private:
    int32_t m_hp{100};
    bool    m_is_dead{false};
};
```

> [!TIP]
> 始終將純資料欄位（Payload）放在 `private` 或 `protected` 中，並透過 Getter/Setter 提供存取，保證每一次修改都受到 `OuroWriteLock` 的安全保護。

---

## 📦 2. 序列化協議實作 (Pure Payload Serialization)

OuroKore 嚴格實施「純資料（Pure Payload）」與「關聯拓撲（Edge Roster）」分離打包原則：
* **您只需要負責自身的屬性資料**（整數、浮點數、字串、POD 二進位緩衝區）。
* **所有成員插槽（`OwningHandle`）均由框架自動註冊並打包**，絕對不需要也不可以在 `SerializePayload` 裡手動序列化 Handle！

```cpp
void SerializePayload(ork::OuroStream &stream) const override {
    stream.WriteProperty("hp", m_hp);
    stream.WriteProperty("is_dead", m_is_dead);
}

void DeserializePayload(ork::OuroStream &stream) override {
    stream.ReadProperty("hp", m_hp);
    stream.ReadProperty("is_dead", m_is_dead);
}
```

---

## 🚫 3. 繼承禁令：禁止菱形多重繼承 (Diamond Inheritance Forbidden)

為確保跨模組 CRT 記憶體安全釋放（Deleter）與物件型別精確轉換：
* `T*` 必須能無二義性隱式轉換為 `OuroObject*`。
* 核心在編譯時期透過 `static_assert` 嚴格禁止菱形繼承。

---

---

## 🏷️ 4. 型別系統宣告與安全多型轉型 (CRTP Subclass Type System)

所有受管領域物件均採用 **CRTP 免巨集自動型別系統**，透過繼承 `ork::Subclass<Derived, Base>`，在編譯時期自動萃取類別名稱並向核心型別登錄系統登記繼承樹，**類別體內完全無需撰寫任何侵入性巨集**即可獲得完整的 RTTI 與多型轉型支援：

```cpp
// 1. 基底領域物件（繼承自 ork::Subclass<Creature, ork::OuroObject>，Base 預設為 OuroObject）
class Creature : public ork::Subclass<Creature, ork::OuroObject> {
public:
    Creature() = default;
    explicit Creature(int32_t hp) : m_hp(hp) {}

    int32_t GetHp() const { ork::OuroReadLock lock(*this); return m_hp; }
    void SetHp(int32_t hp) { ork::OuroWriteLock lock(*this); m_hp = hp; }
private:
    int32_t m_hp{100};
};

// 2. 子類別繼承：Base 參數指定直接父類別 Creature
// 支援透過 Subclass(...) 完美轉發參數至父類別建構子！
class Monster : public ork::Subclass<Monster, Creature> {
public:
    Monster() = default;
    Monster(int32_t hp, int32_t rage) : Subclass(hp), m_rage(rage) {}

    int32_t GetRage() const { ork::OuroReadLock lock(*this); return m_rage; }
private:
    int32_t m_rage{50};
};

// 3. 孫類別／曾孫類別多層繼承
class BossMonster : public ork::Subclass<BossMonster, Monster> {
public:
    BossMonster() = default;
    BossMonster(int32_t hp, int32_t rage, std::string skill)
        : Subclass(hp, rage), m_special_skill(std::move(skill)) {}

    void CastUltimateSkill() {
        ork::OuroWriteLock lock(*this);
        // 施放絕招...
    }
private:
    std::string m_special_skill{"Meteor"};
};
```

> [!NOTE]
> **免巨集優勢**：
> - 完全拋棄舊式 `ORK_OBJECT` 巨集，語法更貼近現代標準 C++20。
> - 支援帶參數建構子轉發（透過呼叫 `Subclass(...)`）。
> - 型別識別碼在編譯期與載入時自動計算並註冊，完全杜絕手動漏寫巨集導致的繼承樹斷層。

### 1. 成員呼叫鐵律：僅接受成員函式指標
為徹底消除裸指標逃逸與懸垂指標（UAF）漏洞，`OuroPtr<T>` 徹底拔除了 `operator->`、`operator*` 與 `get()`：
* **標準調用方式**：透過成員指標運算子轉發 `ptr(&ClassName::Method, args...)` 或 `ptr.Invoke(&ClassName::Method, args...)`。
* **嚴禁直接使用裸指標或 Lambda**：`OuroPtr` 的 `operator()` 嚴格限定僅接受成員函式指標，以防止 Lambda 閉包無意捕獲並外洩裸指標；若需在極端效能情境下執行自定義閉包操作，僅限在受控範圍內使用 `ork::WithObject(ptr, lambda)`。
* **原生極速延遲快取**：首次呼叫時透明復水並快取指標，後續呼叫直接以 $O(1)$ 純暫存器原生速度執行。

```cpp
ork::OuroPtr<BossMonster> boss = ork::CreateObject<BossMonster>();

// 正確調用方式：
boss(&BossMonster::CastUltimateSkill);
boss(&BossMonster::SetHp, 9999);

// 錯誤語法（編譯失敗）：
// boss->CastUltimateSkill(); // ❌ OuroPtr 無 operator->
```

### 2. 型別判定（純 ControlBlock 查詢，零 I/O 脫水安全）
使用 `ptr.Is<TargetT>()` 可以檢查物件是否為 `TargetT` 或其派生子類別（支援完整多型繼承樹判定）：
```cpp
ork::OuroPtr<Creature> c = ork::CreateObject<BossMonster>();

// 支援沿著繼承鏈向上判定：
assert(c.Is<BossMonster>() == true);
assert(c.Is<Monster>() == true);
assert(c.Is<Creature>() == true);
assert(c.Is<ork::OuroObject>() == true);

// 脫水保證：物件即使脫水落盤，型別資訊永存於 ControlBlock 墓碑中，
// 呼叫 Is<T>() 為純記憶體比對，絕對不會觸發磁碟 I/O 復水！
```

### 3. 安全向下/向上轉型（Downcasting & Upcasting）
* **左值轉型 (`ptr.As<TargetT>()`)**：
  若型別相符，安全增加一條根引用（Root Edge）並回傳型別為 `OuroPtr<TargetT>` 的新句柄；若型別不符則安全回傳空句柄（可直接作為 `bool` 判空），**絕不拋出未定義行為或記憶體崩潰**。
  ```cpp
  ork::OuroPtr<BossMonster> boss_ptr = c.As<BossMonster>();
  if (boss_ptr) {
      // 轉型成功，安全執行專屬方法
      boss_ptr(&BossMonster::CastUltimateSkill);
  }
  ```
* **右值移動轉型 (`std::move(ptr).As<TargetT>()`，極度推薦)**：
  **零引用計數變更開銷！** 原指標的根引用所有權會直接原子移交給新指標，原指標被安全清空；若轉型失敗，原根引用會自動釋放歸零。
  ```cpp
  // 零開銷原子轉移所有權
  ork::OuroPtr<BossMonster> moved_boss = std::move(c).As<BossMonster>();
  assert(!c); // c 已被掏空
  assert(moved_boss);
  ```

### 4. STL 風格轉型函式
框架亦提供與標準庫慣例相容的模板轉型函式（全面支援左值拷貝與右值移動）：
```cpp
// 動態安全檢查轉型（同 As<T>()）
auto boss1 = ork::dynamic_pointer_cast<BossMonster>(creature_ptr);
auto boss2 = ork::dynamic_pointer_cast<BossMonster>(std::move(creature_ptr));

// 靜態轉型（不檢查 TypeID，極致效能，需由開發者保證型別安全）
auto static_boss = ork::static_pointer_cast<BossMonster>(creature_ptr);
```

### 5. 插槽與弱引用的多型賦值與晉升轉型
* **OwningHandle 協變賦值**：基底類別插槽可直接接收衍生類別指標：
  ```cpp
  ork::OwningHandle<Creature> occupant{"OccupantSlot"};
  occupant = boss_ptr; // 自動註冊擁有權拓撲邊緣
  ```
* **UnboundHandle 晉升轉型**：弱引用在呼叫 `LockAndAcquire` 時可直接模板化指定子型別：
  ```cpp
  ork::UnboundHandle<Creature> visitor = boss_ptr;

  // 晉升時直接轉型為 BossMonster，若物件已銷毀或型別不符則回傳空 OuroPtr
  if (auto boss = visitor.LockAndAcquire<BossMonster>()) {
      boss(&BossMonster::CastUltimateSkill);
  }
  ```
''', encoding="utf-8")

    # 04_handles_and_topology.md
    (manual_dir / "04_handles_and_topology.md").write_text('''# 04. Handle 拓撲管理系統 (Handles & Topology)

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
   - **模組唯一善後收尾與常駐模式 (Terminal Shutdown & Resident Mode)**：透過 `lib.register_shutdown_symbol("ork_plugin_shutdown")` 或 `lib.set_shutdown_hook(...)` 註冊收尾邏輯。外掛在引用歸零時首先執行此唯一入口；若外掛回傳 `false` 拒絕結束，系統自動轉為【常駐模式】，不呼叫 `FreeLibrary`、不發送後置通知，並保持全域路徑登錄長存以供後續無縫重用！若同意結束（回傳 `true` 或 `void`），則正常物理卸載並觸發 `post_unload_hooks`。

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

## 4. `OuroPtr<T>`：棧上生命週期守衛

`OuroPtr` 代表活躍的「根引用（Root Edge）」。只要有任何執行緒在棧上持有某物件的 `OuroPtr`：
* 核心 100% 保證：**該物件絕不會被自動脫水或銷毀！**
* **安全轉發執行 (Zero Raw Pointer Guarantee)**：徹底拔除 `get()`、`operator->` 與 `operator*`，透過 `operator()(Fn&&, Args&&...)` 或 `Invoke(...)` 調用成員函式、成員欄位或 Lambda 閉包，徹底杜絕裸指標逃逸與 UAF 漏洞。
* **原生極速延遲快取**：首次呼叫時透明復水並快取記憶體指標，後續呼叫直接以 $O(1)$ 純暫存器/記憶體原生速度執行。
''', encoding="utf-8")

    # 05_dehydration_and_storage.md
    (manual_dir / "05_dehydration_and_storage.md").write_text('''# 05. 自動換頁脫水與儲存驅動 (Dehydration & Storage)

OuroKore 具備針對超大型世界物件圖的記憶體自動分頁技術，長時間未活動的物件自動釋放實體記憶體，存取時透明復原。

---

## 🧊 1. 脫水 (Dehydration) 的運作機制

當物件長時間未被存取且滿足脫水條件時：
1. **檢查 Root 計數**：若 `root_count > 0`（代表有執行緒持有 `OuroPtr`），脫水程序安全略過，絕不打擾活躍業務。
2. **藍圖序列化**：若狀態為 `Dirty` 或 `UnsavedNew`，自動呼叫儲存驅動落盤。
3. **實體記憶體卸載**：銷毀 C++ 物件實體記憶體，控制區塊（ControlBlock）保留於記憶體中並標記為 `Dehydrated` 墓碑。
4. **Handle 拓撲不變性**：物件的全域唯一識別碼 `HandleID` 與指向它的所有 Handle **完全保持不變**。

---

## 💧 2. 透明按需復水 (Transparent Rehydration)

當任何程式碼透過 `handle.Get()`、`handle.LockAndAcquire()` 或 `OuroPtr` 嘗試存取已處於 `Dehydrated` 狀態的物件時：
* 控制區塊自動攔截該請求。
* 觸發預先註冊的復水回呼，自儲存驅動讀取藍圖串流。
* 在模組 CRT 中重新 `new T()` 配置記憶體，反序列化狀態並重新綁定至原控制區塊。
* **呼叫端代碼對這一切完全無感！**

---

## 📦 3. 手動脫水與右值消耗語意 (Manual Dehydration & Move Consume)

除了由背景脫水器自動換頁外，應用端亦可依業務邏輯主動發起脫水：

```cpp
ork::OuroPtr<Monster> boss = ork::CreateObject<Monster>();
ork::HandleID boss_id = boss.GetTargetID();
ork::Save(boss); // 脫水前確保狀態落盤

// 方式一：右值移動消耗脫水（強烈推薦）
// ⚠️ 注意：傳入的原 boss 指標將被立即釋放並重置清空，杜絕懸空指針！
bool dehydrated = ork::Dehydrate(std::move(boss));
assert(!boss); // 原指標已安全清空

// 方式二：非同步背景脫水（不卡頓主執行緒）
// std::future<ork::AsyncResult<void>> future = ork::DehydrateAsync(boss_id);

// 方式三：依 HandleID 脫水
// bool ok = ork::Dehydrate(boss_id);
// 若此時有其他執行緒持有 OuroPtr（root_count > 0），脫水將安全略過並回傳 false
```

---

## 🔄 4. 手動顯式復水 (Explicit Rehydration)

在絕大多數場景下，推薦依賴 **透明按需復水**（直接調用 `handle.Get()` 或 `OuroPtr` 方法）。若特定場景需要在背景預先載入，亦可主動顯式復水：

```cpp
// 1. 同步顯式復水：配置新空殼、載入藍圖並回傳全新活躍 OuroPtr
ork::OuroPtr<Monster> restored_boss = ork::Rehydrate<Monster>(boss_id);
restored_boss(&Monster::Attack);

// 2. 非同步背景復水（預先熱身）：
std::future<ork::AsyncResult<Monster>> future = ork::RehydrateAsync<Monster>(boss_id);
// ... 主迴圈繼續執行其他任務 ...
auto result = future.get();
if (result.success) {
    ork::OuroPtr<Monster> async_boss = std::move(result.ptr);
    async_boss(&Monster::Attack);
}
```

> [!NOTE]
> **私有封閉防禦保證**：
> 核心內部的復水底層回呼（`RehydrateCallback`）被嚴格收斂於私有實作中，回傳值為 `void` 且透過 ControlBlock 私有綁定，嚴格杜絕外洩物件原始裸指標。

---

## 🔍 5. 儲存狀態與零 I/O 墓碑查詢保證 (Zero-I/O StorageState)

```cpp
// 查詢物件儲存四態：UnsavedNew(0), Clean(1), Dirty(2), Dehydrated(3)
ork::StorageState state = ork::GetStorageState(boss_id);
if (state == ork::StorageState::Dehydrated) {
    std::cout << "物件目前已脫水落盤，記憶體已釋放" << std::endl;
}

// 🛡️ 零 I/O 保證：
// 以下查詢純比對記憶體中之 ControlBlock 墓碑，絕對不會誘發磁碟 I/O 復水：
bool alive = ork::IsAlive(boss_id);
uint32_t roots = ork::GetRootEdgeCount(boss_id);
```

---

## ⚙️ 6. 配置儲存驅動與 LRU 自動脫水器

```cpp
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>
#include <ourokore/component/builtin/OuroLRUAutoDehydrator.hpp>

// 1. 建立儲存驅動
auto storage = std::make_shared<ork::InMemoryStorage>();

// 2. 建立 LRU 自動脫水器（最大快取 1000 個物件）
auto lru = std::make_shared<ork::OuroLRUAutoDehydrator>(storage, 1000);

// 3. 宿主初始化時注入
ork::HostContext host = ork::Init(storage, lru);

// 隨時可由 Host 調整或更換脫水策略
host.SetAutoDehydrator(lru);
```
''', encoding="utf-8")

    # 06_host_lifecycle.md
    (manual_dir / "06_host_lifecycle.md").write_text('''# 06. 宿主生命週期與特權管理 (Host Lifecycle)

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
''', encoding="utf-8")

    # 07_api_reference.md
    (manual_dir / "07_api_reference.md").write_text('''# 07. 公開 C++ API 參照手冊 (API Reference)

本手冊彙整 OuroKore 面向應用開發者與宿主主程式之所有公開核心類別與全域介面。

---

## 🏛️ 1. 宿主專屬類別：`ork::HostContext`
* **標頭檔**：`ourokore/host/HostContext.hpp`
* **方法**：
  * `bool IsValid() const noexcept`：檢查是否具備合法宿主主控權。
  * `void Shutdown()`：優雅終止核心背景任務與執行緒池。
  * `void FlushStorage()`：同步排空並等待所有藍圖磁碟寫入與銷毀落盤。
  * `void FlushDeferredDeletions()`：同步排空延遲物理銷毀隊列。
  * `void CollectCycles()`：同步觸發一輪循環參照檢測與孤島解開。
  * `void SetDeferredDeleteMode(bool sync)`：設定非同步延遲銷毀或即時同步模式。
  * `void SetAutoDehydrator(std::shared_ptr<IAutoDehydrator>)`：設定全域自動脫水模組。
  * `std::shared_ptr<IAutoDehydrator> GetAutoDehydrator() const`：取得當前自動脫水模組。
  * `void SetStorageDriver(std::shared_ptr<IStorageDriver>)`：設定儲存驅動。
  * `std::shared_ptr<IStorageDriver> GetStorageDriver() const`：取得當前儲存驅動。
  * `size_t TriggerDehydrationRescue(size_t bytes_needed)`：緊急脫水指定位元組數。
  * `void SetObjectModuleLoader(HandleID id, const DynamicLibrary &loader)`：綁定動態庫載入器至受管物件控制區塊（脫水長存，Payload 銷毀即刻解錨）。
  * `DynamicLibrary GetObjectModuleLoader(HandleID id) const`：取得物件綁定之動態庫載入器。
  * `std::shared_ptr<IObjectModuleBinder> GetModuleBinder() const`：取得專職模組綁定介面（最小特權原則，委派給外掛工廠）。

---

## 📦 2. 領域物件基底與 CRTP 樣板：`ork::OuroObject` / `ork::Subclass`
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **類別基底 `ork::OuroObject`**：
  * 所有託管物件之抽象基類，嚴禁外部 `new` 或值拷貝。
  * `HandleID GetObjectID() const`：取得物件之全域唯一識別碼。
  * `StorageState GetStorageState() const`：取得物件當前儲存狀態（Clean/Dirty/Dehydrated/UnsavedNew）。
  * `ork_type_id_t GetTypeID() const`：取得物件當前之 64 位元 TypeID（支援多型與繼承階層查詢）。
  * `virtual void SerializePayload(OuroStream &stream) const`：純資料屬性序列化介面。
  * `virtual void DeserializePayload(OuroStream &stream)`：純資料屬性反序列化介面。
* **樣板基底 `ork::Subclass<Derived, Base = ork::OuroObject>`**：
  * **所有領域物件強制繼承之 CRTP 基底**（免巨集自動型別系統）。
  * `static constexpr const char* StaticTypeName()`：自動在編譯期萃取類別名稱。
  * `static TypeID StaticTypeID()`：自動以 FNV-1a 計算並向核心註冊繼承關係樹。
  * 支援帶參數建構子完美轉發：`Subclass(args...)` 直接初始化父類別。

---

## 🔗 3. 智慧 Handle 系統
* **標頭檔**：`ourokore/component/Handles.hpp`
* **類別**：
  * `OwningHandle<T>`：強持有槽位，宣告為物件成員。方法：`Set()`, `Get()`, `Release()`, `GetTargetID()`。
  * `OwningContainerHandle`：動態強持有容器，方法：`AddTarget()`, `RemoveTarget()`, `GetTargetIDs()`。
  * `UnboundHandle<T>`：無繫結非擁有型引用，方法：`LockAndAcquire()`, `GetTargetID()`, `IsAlive()`, `Release()`。
  * `OuroPtr<T>`：棧上活躍根指標守衛，支援 `operator()(Fn&&, Args&&...)`, `Invoke(...)`, `operator bool()`, `IsAlive()`, `GetTargetID()`, `Release()`。
    * `template <typename U> bool Is() const`：判定物件是否屬於或繼承自型別 `U`（純記憶體查詢，脫水狀態零 I/O 保證）。
    * `template <typename U> OuroPtr<U> As() const &`：向下/向上安全轉型（左值增持根引用）。
    * `template <typename U> OuroPtr<U> As() &&`：右值移動轉型（**零引用計數開銷**轉移所有權）。
    * `ork_type_id_t GetTypeID() const`：取得目標物件 TypeID。
    * `dynamic_pointer_cast<U>(ptr)` / `static_pointer_cast<U>(ptr)`：STL 風格轉型支援。

---

## 🔒 4. 併發同步守衛
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **類別**：
  * `OuroReadLock`：共享讀鎖 RAII 守衛。
  * `OuroWriteLock`：獨占寫鎖 RAII 守衛，**解構時自動原子標記 Dirty**。

---

## 🏭 5. 物件工廠與持久化介面
* **標頭檔**：`ourokore/component/OuroCore.hpp`
* **函式**：
  * `CreateObject<T>(args...)`：建立受管領域物件（自動通報脫水模組登記）。
  * `CreatePermanentObject<T>(args...)`：建立永久常駐物件（不參與脫水換頁）。
  * `Save(OuroPtr<T>)` / `Load(OuroPtr<T>)`：同步存檔與自磁碟載入刷新。
  * `SaveAsync(OuroPtr<T>)` / `LoadAsync(OuroPtr<T>)`：非同步背景存檔與載入。
  * `Dehydrate(HandleID id)`：依 ID 脫水（若 root_count > 0 則安全略過傳回 false）。
  * `Dehydrate(OuroPtr<T> &&ptr)`：右值移動消耗脫水（清空原指標，防止懸空）。
  * `DehydrateAsync(HandleID id)` / `DehydrateAsync(OuroPtr<T> &&ptr)`：非同步背景脫水。
  * `Rehydrate<T>(HandleID id)` / `Rehydrate<T>(const OuroPtr<T> &ptr)`：顯式手動復水，回傳全新 `OuroPtr<T>`。
  * `RehydrateAsync<T>(HandleID id)` / `RehydrateAsync<T>(const OuroPtr<T> &ptr)`：非同步背景顯式復水。
  * `IsAlive(HandleID id)`：查詢物件是否存活（純 ControlBlock 查詢，零 I/O 保證）。
  * `GetStorageState(HandleID id)`：查詢物件當前 StorageState（純 ControlBlock 查詢，零 I/O 保證）。
  * `GetRootEdgeCount(HandleID id)`：查詢目標當前活躍根邊緣數量。
  * `SaveBatch(...)` / `LoadBatch(...)`：多核心平行批次操作。

---

## 🧩 6. 跨平台動態庫與插件載入器：`ork::DynamicLibrary`
* **標頭檔**：`ourokore/base/DynamicLibrary.hpp`
* **設計哲學與卸載核心原則**：
  * **禁絕手動卸載 (No Manual Unload)**：載入器不提供手動 `unload()` 介面，杜絕因提前手動卸載導致正在執行的物件虛擬函式表 (vtable) 與代碼段失效崩潰。
  * **生命週期反向錨定與自動卸載 (Life-Bound Retention & Auto Unload)**：設計期望應用端將動態庫「所產生的物件」與動態庫建立生命週期綁定（透過 `bind_lifecycle()` 或在工廠 Deleter 閉包中捕捉 `DynamicLibrary` 實例）。當該動態庫產生的所有物件全部解構銷毀後，底層動態庫才會在引用計數歸零時自動且安全地卸載（`FreeLibrary` / `dlclose`）。
  * ⚠️ **關鍵約束注意（load 回傳值之生命週期綁定）**：`ork::DynamicLibrary::load()` 的回傳值本身「已經將其綁定（持有一份引用計數）」。如果不放棄該回傳值（例如長存於全域或成員變數、或外層未離開作用域/未重設），動態庫是絕對不會被卸載的！因此，若希望依賴產生物件全部解構後自動卸載 DLL，呼叫端在完成物件構造與綁定後，必須主動放棄/釋放 `load()` 傳回的初始句柄（例如讓其隨工廠作用域自然解構，或呼叫 `reset()` 放棄持有）。
* **方法**：
  * `static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = Default)`：自 UTF-8 路徑載入動態庫（Windows 內部使用 Unicode `LoadLibraryW`，杜絕本地 ANSI/CP950 亂碼）。回傳之句柄已持有動態庫引用。
  * `static DynamicLibrary load(const std::filesystem::path &path, ...)`：自檔案路徑載入動態庫。
  * `void reset() noexcept`：放棄當前持有的動態庫句柄（扣減引用計數），使存活權杖全權移交給綁定物件。
  * `bool is_loaded() const noexcept`：查詢動態庫是否載入成功。
  * `const std::string &get_last_error() const noexcept`：取得 UTF-8 格式的系統錯誤訊息。
  * `std::string get_path_utf8() const noexcept`：取得載入函式庫之 UTF-8 規範路徑。
  * `size_t use_count() const noexcept`：取得當前動態庫的存活引用計數（含句柄變數與綁定物件）。
  * `template <typename FuncT> auto get_symbol(std::string_view name) const noexcept`：解析動態庫導出符號並智慧推導函式指標型別。
  * `template <typename T, typename DeleterT> std::shared_ptr<T> bind_lifecycle(T *raw_ptr, DeleterT deleter)`：將自訂裸指標與動態庫存活權杖綁定，確保指標銷毀前動態庫永不卸載。
  * `std::shared_ptr<const void> create_lifetime_token() const noexcept`：產生純生命週期存活權杖（Pure Lifetime Token），無須綁定單一裸指標，任何容器、樹狀結構（如整棵樹的所有節點）或非同步任務均可共享持有，只要任一節點存活即保證 DLL 絕對不被物理卸載。
  * `static std::filesystem::path format_filename(std::string_view base_name)`：依作業系統格式化動態庫檔名（Windows `.dll`、Linux `.so`、macOS `.dylib`）。
  * `bool is_first_loaded() const noexcept`：查詢本次 `load()` 取得的實例是否為動態庫於進程中的首次載入（0 -> 1）。若為 false 代表先前已由其他模組載入並存活中。
  * `void set_shutdown_hook(std::function<bool()> hook)`：設定模組唯一的善後收尾回呼。傳回 `true` 允許物理卸載；傳回 `false` 拒絕卸載轉為常駐模式。
  * `bool register_shutdown_symbol(std::string_view symbol_name)`：依據符號名稱自動解析收尾函式並註冊（支援 `bool()` / `int()` / `void()`）。
  * `void add_post_unload_hook(std::function<void()> hook)`：註冊在動態函式庫物理卸載（FreeLibrary / dlclose）完成後執行的通知回呼（Post-Unload Hook）。僅在外掛同意卸載且成功物理卸載後才觸發。
  * `void enable_deferred_unload(bool enable = true) noexcept`：啟用非同步離棧延遲卸載模式。卸載動作自動移交獨立背景執行緒執行，達成主程式零卡頓與無崩潰卸載。
  * `bool is_deferred_unload_enabled() const noexcept`：查詢當前是否啟用了非同步離棧延遲卸載模式。
  * `template <typename FuncT, typename... Args> bool initialize_once(std::string_view symbol_name, Args &&...args)`：僅在首次載入（0 -> 1）時執行指定的符號初始化函式，重複載入時自動安全略過。
  * `WeakDynamicLibrary to_weak() const noexcept`：建立並取得該動態庫之弱引用觀察者（不增加強引用計數，不阻止自動卸載）。

---

## 👁️ 6.1 動態庫弱引用觀察者：`ork::WeakDynamicLibrary`
* **標頭檔**：`ourokore/base/DynamicLibrary.hpp`
* **設計目的**：提供類似 `std::weak_ptr` 的無所有權觀察與晉升機制。當主程式為配合自動卸載而呼叫 `DynamicLibrary::reset()` 放棄初始強引用後，若日後需要再次建立物件或監控模組狀態，可透過本類別之 `lock()` 安全晉升重獲強引用（無須重新 LoadLibrary）；若所有受管物件均已解構且 DLL 已卸載，`lock()` 則安全傳回無效實例。
* **方法**：
  * `WeakDynamicLibrary(const DynamicLibrary &lib) noexcept`：從強引用 DynamicLibrary 構造弱引用觀察者。
  * `DynamicLibrary lock() const noexcept`：嘗試將弱引用晉升為強引用。若動態庫仍存活傳回有效實例；若已卸載則傳回無效實例。
  * `bool expired() const noexcept`：查詢動態庫是否已經卸載或過期。
  * `size_t use_count() const noexcept`：查詢當前存活之強引用計數（所有綁定活體物件與強引用總數）。
  * `std::weak_ptr<const void> create_weak_lifetime_token() const noexcept`：產生對應於本動態庫的弱引用權杖。
  * `void reset() noexcept`：重設弱引用為空狀態。
  * `explicit operator bool() const noexcept`：等同於 `!expired()`。

---

## 🌐 7. 全域 UTF-8 零拷貝輔助工具：`ork::utf8`
* **標頭檔**：`ourokore/base/utf8.hpp`
* **函式與工具**：
  * `ork::utf8::as_view(str)`：將 `std::string`、`std::u8string`、`std::string_view`、`std::u8string_view`、`const char*`、`const char8_t*` 零拷貝轉為 `std::string_view`。
  * `ork::utf8::to_string(str)`：將各類字串統一轉為 `std::string`。
  * `ork::utf8::to_u8string(view)`：將字串視圖轉為 C++20 原生 `std::u8string`。
  * `ork::utf8::is_string_like_v<T>`：編譯期型別特徵萃取，判斷是否為字串相關型別。

---

## ⚙️ 8. 現代高效能雜湊工具模組：`ork::base::Hash`
* **標頭檔**：`ourokore/base/Hash.hpp`
* **設計哲學**：相容 C++20 `constexpr` 編譯期常數計算、現代雜湊演算法、字面量運算子支援。
* **演算法與函式**：
  * `ork::base::Fnv1a64(data)`：FNV-1a 64-bit 雜湊演算法（全域 TypeID 與字串 ID 唯一標準）。
  * `ork::base::Fnv1a32(data)`：FNV-1a 32-bit 雜湊演算法。
  * `ork::base::Crc32(data)`：CRC32 (IEEE 802.3) 校驗碼（資料完整性與防竄改驗證）。
  * `ork::base::MurmurHash3(data, seed)`：MurmurHash3 32-bit 高品質雜湊演算法。
  * `ork::base::HashCombine(seed, v1, v2, ...)`：Boost / Container 標準變參組合雜湊。
  * 使用者自訂字面量（`using namespace ork::base::literals;`）：
    * `""_fnv64`：編譯期直接計算為 64 位元常數整數。
    * `""_fnv32`：編譯期直接計算為 32 位元常數整數。
    * `""_crc32`：編譯期直接計算為 CRC32 常數校驗碼。

---

## 🌳 9. 樹狀結構節點與文字 DSL 串流：`ork::base::TreeNode<T>` / `ork::base::TreeIO`
* **標頭檔**：`ourokore/base/Tree.hpp`、`ourokore/base/TreeIO.hpp`
* **設計哲學**：
  * **單一容器雙模態統合（Unified Dual-Mode）**：全體子節點統一由連續記憶體 `std::vector` 儲存（享有 CPU 快取極速預讀），具名字節點由 `std::unordered_map` 提供 $O(1)$ 雜湊尋址。**下標與名稱存取 100% 互通**，存取到的為同一節點實體。
  * **形態由資料自動推導（Data-Driven Morphism）**：依據子節點結構純度自動判定——全具名者自動判定為物件模式（大括號 `{}`），混入匿名元素者自動判定為陣列模式（小括號 `()`）。
  * **CRTP 自定義衍生節點擴充（Extensible CRTP Hierarchy）**：支援繼承 `TreeNodeBase<Derived>` 定義強型別領域節點，序列化與反序列化自適應萃取衍生型別，零成本零強制轉型。
  * **極致執行緒安全**：結構拓撲鎖（`m_mutex`）與資料 Payload 鎖（`m_dataMutex`）獨立讀寫分離，高頻資料更新不阻礙樹結構遍歷。
  * **防遞迴析構爆棧**：內建顯式堆疊迭代析構，巨型深樹解構時由堆積迴圈安全釋放，徹底杜絕遞迴析構引發呼叫堆疊溢位（Stack Overflow）與行程退出 UAF。
  * **非遞迴顯式堆疊反序列化**：反序列化全程採用 Heap 顯式堆疊非遞迴狀態機，Call Stack 深度恆為 $O(1)$，巨深巢狀文字 DSL 免疫 Stack Overflow。
  * **正交界定符與無等號緊湊支援**：四大正交界定符 `[名稱]`、`"資料"`、`{物件}`、`(陣列)`，等號 `=` 為純無視裝飾符號。支援極致緊湊無等號模式（`WithoutEqual`），連續具名空節點、匿名空元素、物件陣列 `( { [a]="1" } )` 100% 精確對稱還原，單元素容器拓撲絕不脫殼降級。
* **核心類別與方法**：
  * **樣板基底 `TreeNodeBase<Derived>`**：
    * `CreateRoot(name)` / `CreateArray(name)`：建立樹之根節點。
    * `bool IsObject() / bool IsArray()`：純資料內容驅動判定（`m_elements.size() > m_nameMap.size()` 為陣列）。
    * `NodePtr PushElement()` / `bool PushElement(element)`：向尾端追加匿名元素。
    * `size_t ElementCount()` / `size_t ChildCount()` / `size_t Size()`：取得子節點總數（$O(1)$）。
    * `NodePtr GetElementAt(index)` / `operator[](size_t index)`：隨機下標存取元素（$O(1)$）。
    * `NodePtr FindChildByName(name)` / `operator[](const std::u8string &name)`：按名稱尋找子節點（$O(1)$）。
    * `bool HasChild(name)`：查詢子節點存在性。
    * `NodePtr AddChild(name)`（相容別名 `AddBackChild`）：新增具名或匿名子節點（$O(1)$）。
    * `NodePtr InsertBefore(child, name)` / `NodePtr InsertAfter(child, name)`：指定位置插入子節點。
    * `bool RemoveElementAt(index)` / `bool RemoveChild(child)` / `bool RemoveChildByName(name)`：移除子節點。
    * `void ClearChildren()` / `ClearElements()`：清空所有子項目。
    * `auto begin() / end()` / `rbegin() / rend()` / `Reversed()` / `GetTreeMutex()`：支援配合樹級讀寫鎖進行標準 STL 迭代器與 range-for 安全走訪（支援 `for (auto &c : node->Reversed())` 零成本反向視圖）。⚠️ **關鍵防禦鐵律**：走訪期間僅供純資料使用（`GetData` / `GetName`），**絕對禁止在此期間執行節點拓撲修改（如 `AddChild` / `RemoveChild`）**，否則會因非遞迴讀寫鎖引發重複加鎖死鎖（Deadlock）！
    * `DetachFromParent()`：安全斷開與父節點之雙向弱關聯並自立為新樹（配發專屬獨立鎖）。
  * **具體節點 `TreeNode<T>`（`StringTreeNode` 預設 `T = std::string`）**：
    * `T GetData()` / `void SetData(const T &)` / `void SetData(T &&)`：安全存取節點資料（受資料讀寫鎖保護）。
  * **文字 DSL 串流工具 `TreeIO`**：
    * `CompactMode` 列舉：`None`（標準美化縮排換行）、`WithEqual`（保留等號緊湊 `="`）、`WithoutEqual`（不保留等號極致緊湊 `"`）。
    * `static void Serialize<Node = StringTreeNode>(ostream, root, data_to_string, indent_width, mode)`：輸出文字 DSL 至串流，支援應用端自訂 CRTP 衍生節點與 3 種緊湊模式。
    * `static void SerializeCompact<Node = StringTreeNode>(ostream, root, mode)`：緊湊序列化便捷函式。
    * `static std::string SerializeToString<Node = StringTreeNode>(root, ...)`：直接輸出文字 DSL 字串（支援 CompactMode 列舉、布林緊湊旗標或自訂縮排與 data_to_string 轉發）。
    * `static std::shared_ptr<NodeType> Deserialize<NodeType = StringTreeNode>(istream, data_handler)`：寬容型狀態機自輸入串流反序列化（支援 CRTP 節點替換與資料型別自適應，精準回傳應用端節點智慧指針；handler 支援值轉換或 `(node, str) -> void` 節點現地賦值）。
    * `static std::shared_ptr<NodeType> DeserializeFromString<NodeType = StringTreeNode>(string_view, data_handler)`：自文字字串反序列化（支援 CRTP 節點替換與資料型別自適應，精準回傳應用端節點智慧指針）。
''', encoding="utf-8")

    # 08_tree_and_dsl.md
    (manual_dir / "08_tree_and_dsl.md").write_text(r'''# 08. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)

本章節介紹 OuroKore 基礎工具庫（`ourokore_base`）中的現代高效能階層容器 `TreeNode<T>` 與文字 DSL 串流工具 `TreeIO`。

---

## 🧭 1. 設計哲學與心智模型

1. **單一容器雙模態統合（Unified Dual-Mode）**：
   - 傳統 JSON / XML 解析庫常將「物件（Object/Map）」與「陣列（Array/List）」切分為兩種不相容的容器型別。
   - OuroKore 徹底終結兩者分裂：所有子節點底層均由連續記憶體 `std::vector` 儲存（享有連續記憶體快取極速讀取與保序特性），具名字節點由 `std::unordered_map` 提供 $O(1)$ 名稱雜湊尋址。
   - **下標與名稱存取 100% 互通**：`node[0]` 與 `node[u8"HP"]` 存取到的為同一個實體，隨機下標與鍵名存取均為 $O(1)$！

2. **形態由資料自動推導（Data-Driven Morphism）**：
   - 容器不需要顯式設定或轉換形態，由子節點結構純度自動判定：
     * **全具名字節點**：自動推導為物件形態（DSL 輸出使用大括號 `{}`）。
     * **混入任何無名字節點**：自動推導為陣列形態（DSL 輸出使用小括號 `()`）。

3. **極致執行緒安全**：
   - 樹狀結構拓撲鎖（`m_mutex`）與資料 Payload 鎖（`m_dataMutex`）獨立讀寫分離，高頻資料更新絕不阻礙樹結構遍歷。

4. **百萬層深樹顯式堆疊迭代防爆棧析構（Iterative Stack-Overflow Defense）**：
   - 內建顯式堆疊展平析構機制，巨型深樹解構時將遞迴展開為堆積迴圈以 $O(1)$ 呼叫深度安全釋放，杜絕遞迴爆棧，並提供向後相容之 `AsyncNodeDeletor`。

5. **百萬層深樹顯式堆疊非遞迴反序列化（Non-recursive FSM Deserialization）**：
   - 反序列化完全由 Heap 上的顯式堆疊 `std::vector<ParseFrame>` 驅動，Call Stack 呼叫深度恆為 $O(1)$，徹底杜絕深層巢狀文字 DSL 引發呼叫堆疊溢位（Stack Overflow）。

---

## 📝 2. 文字 DSL 語法與界定符

OuroKore 文字 DSL 採用四個互不干擾的正交界定符：
* `[名稱]`：節點名稱標記。
* `"資料"`：節點資料內容（支援 0~255 二進位位元組與完整脫字元轉義 `\"`、`\\`、`\n`、`\xHH`）。
* `{物件}`：具名子節點群集（大括號）。
* `(陣列)`：陣列元素清單（小括號）。

> [!IMPORTANT]
> **正交界定符與零等號哲學**：
> 四大界定符 `[]` `""` `{}` `()` 為唯一的語法 Token，等號 `=` 僅為可選裝飾符號。在 `CompactMode::WithoutEqual` 極致緊湊模式下完全省略 `=`（例如 `[Player]"英雄角色"`），連續空節點（如 `[FlagA][FlagB]`）、匿名空元素、物件陣列（如 `( { [id]="1" } { [id]="2" } )`）均 100% 精準對稱序列化與反序列化。單元素匿名容器（如 `("Item")` 或 `{ [Key]="Val" }`）反序列化時完整保留容器拓撲身分，絕不發生單元素脫殼降級為葉節點的 Bug。

### 註解語法原生支援
文字 DSL 反序列化狀態機原生支援三種風格的註解：
* **`//` 單行註解**：跳過至行尾。
* **`/* ... */` 區塊註解**：跳過至閉合符號 `*/`。
* **`#` 腳本註解**：跳過至行尾。

> [!NOTE]
> **註解內語法界定符防禦**：即使註解內部包含引號（`"`）、括號（`[` `]` `{}` `()`）或任意文字，狀態機均會將其完整略過，絕不干擾節點解析！

---

## 💻 3. 基礎使用範例

```cpp
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using ork::base::Tree; // 即 ork::base::StringTreeNode
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根節點
auto player = Tree::CreateRoot(u8"Player");
player->SetData("英雄角色");

// 2. 建立具名屬性 (AddChild 支援具名或無名)
auto hp = player->AddChild(u8"HP");
hp->SetData("100");

// 3. 建立陣列清單
auto inventory = player->AddChild(u8"Inventory");
inventory->AddChild()->SetData("草藥");
inventory->AddChild()->SetData("黃金盔甲");

// 4. 互通性驗證
assert(inventory->ElementCount() == 2);
assert((*inventory)[0]->GetData() == "草藥");     // O(1) 連續向量下標存取
assert((*player)[0] == hp);                      // 具名節點亦可透過下標 0 存取！
assert((*player)[u8"HP"] == hp);
```

---

## 🗜️ 4. 序列化與 3 種緊湊傳輸模式

`TreeIO` 序列化全面採用顯式堆疊走訪（非遞迴），並提供 3 種格式化輸出：

```cpp
// 模式 1：標準美化縮排模式 (CompactMode::None)
// 輸出含標準縮排、換行與空格，適合人類閱讀與配置編輯
TreeIO::Serialize(std::cout, player, CompactMode::None);

// 模式 2：保留等號緊湊模式 (CompactMode::WithEqual)
// 輸出: [Player]="英雄角色"{[HP]="100"[Inventory]("草藥""黃金盔甲")}
std::string compact_with_eq = TreeIO::SerializeToString(player, CompactMode::WithEqual);

// 模式 3：無等號極致緊湊模式 (CompactMode::WithoutEqual)
// 輸出: [Player]"英雄角色"{[HP]"100"[Inventory]("草藥""黃金盔甲")}
std::string compact_no_eq = TreeIO::SerializeToString(player, CompactMode::WithoutEqual);
```

---

## 🔄 5. 寬容型反序列化與註解過濾

寬容型有限狀態機（FSM）自動略過非預期雜訊，並完整支援串流與字串解析：

```cpp
std::string dsl_text = R"(
    // 伺服器遊戲存檔
    /* 區塊註解：此處包含 [FakeNode] "FakeData" 均被安全忽略 */
    # 這是腳本註解
    [Player] = "英雄角色" // 行尾註解
    {
        [HP] = "100" # 生命值
        [Inventory] = (
            "草藥"
            /* 暫時排除裝備："生鏽鐵劍" */
            "黃金盔甲"
        )
    }
)";

// 支援從 std::istream (std::istringstream) 或字串視圖直接解析
std::istringstream iss(dsl_text);
auto restored = TreeIO::Deserialize(iss); // 或 TreeIO::DeserializeFromString(dsl_text)

assert(restored->GetName() == u8"Player");
assert((*restored)[u8"HP"]->GetData() == "100");
assert((*(*restored)[u8"Inventory"])[0]->GetData() == "草藥");
```

---

## 🧬 6. CRTP 自定義衍生節點與雙模式 Handler

應用端可透過 CRTP 繼承 `TreeNodeBase<Derived>` 打造專屬強型別領域節點，反序列化時享有一體化型別自動萃取（精準回傳 `std::shared_ptr<CustomNode>`），並可搭配 In-place Node Setter 回呼：

```cpp
// 1. 定義自訂 CRTP 領域節點
class HeroNode : public ork::base::TreeNodeBase<HeroNode> {
public:
    std::string role_title;
    int combat_power{999};

    explicit HeroNode(std::u8string name = u8"")
        : TreeNodeBase<HeroNode>(std::move(name)) {}
};

// 2. 一鍵反序列化精準轉化為自定義衍生節點（子節點亦為 HeroNode 型別）
auto hero = TreeIO::DeserializeFromString<HeroNode>(
    dsl_text,
    // 支援 In-place Node Setter Handler 直接解構並賦值給領域節點欄位：
    [](const std::shared_ptr<HeroNode> &node, const std::string &raw_val) {
        node->role_title = raw_val;
    }
);

static_assert(std::is_same_v<decltype(hero), std::shared_ptr<HeroNode>>);
assert(hero->role_title == "英雄角色");
```

---

## 🔒 7. 整樹走訪安全範式與死鎖防禦指南 (Tree Traversal & Deadlock Prevention)

OuroKore 的樹狀結構採用**「整棵樹（Root 與所有子孫節點）共享同一個讀寫鎖（`std::shared_mutex`）」**之架構，確保跨節點操作之原子性與跨樹獨立性。

由於 `std::shared_mutex` 為**不可重入鎖（Non-recursive Mutex）**，在進行整樹或子樹遍歷時，必須誓死遵守以下黃金法則：

### ⚠️ 高壓線禁忌：走訪期間「只能做資料存取，絕不能操作節點拓撲」

> [!CAUTION]
> **嚴禁在持讀鎖走訪期間調用節點拓撲修改介面！**
> 在持共享讀鎖（`std::shared_lock`）的保護區塊內，若調用 `AddChild()`、`RemoveChild()`、`PushElement()`、`ClearChildren()`、`DetachFromParent()` 等會索取獨占寫鎖（`std::unique_lock`）的函式，**當前執行緒會立即引發不可重入的重複鎖死鎖（Deadlock）！**

### 1. 標準整樹唯讀走訪（遞迴或深度走訪）
只需在最外層 Root 節點取得一次樹級讀鎖，遞迴走訪整個階層期間零多餘加鎖開銷，且能 100% 保證拓撲結構不被其他執行緒篡改：

```cpp
// 走訪輔助函式（專職資料存取或純分析）
void TraverseTreeData(const StringTreeNode::NodePtr &node) {
    if (!node) return;
    
    // 讀取節點名稱與 Payload 資料（安全）
    std::cout << "節點名稱: " << ork::utf8::to_string(node->GetName())
              << ", 內容: " << node->GetData() << std::endl;
              
    // 走訪所有直接子節點（零加鎖，沿用外層讀鎖）
    for (const auto &child : *node) {
        TraverseTreeData(child);
    }
}

// 呼叫端：在最外層持讀鎖保護整棵樹走訪
void ReadTreeSafely(const StringTreeNode::NodePtr &root) {
    std::shared_lock<std::shared_mutex> lock(root->GetTreeMutex());
    TraverseTreeData(root);
}

// 呼叫端：由右向左（反向）走訪，直接使用 node->Reversed() 視圖糖衣（零拷貝）
void ReadTreeReverseSafely(const StringTreeNode::NodePtr &root) {
    std::shared_lock<std::shared_mutex> lock(root->GetTreeMutex());
    for (const auto &child : root->Reversed()) {
        if (child) {
            std::cout << child->GetData() << std::endl;
        }
    }
}
```

### 2. 邊走訪邊過濾並刪除節點之安全範式（兩階段延遲操作）
若業務邏輯需要依據節點資料「動態移除或增修子節點」，**切勿在走訪迴圈中直接調用 `RemoveChild()`**！必須採用「**第一階段收集目標 -> 釋放讀鎖 -> 第二階段批次修改**」的兩階段安全範式：

```cpp
void PruneTreeSafely(const StringTreeNode::NodePtr &root) {
    std::vector<StringTreeNode::NodePtr> to_remove;
    
    // 【第一階段：持讀鎖安全收集待刪除節點】
    {
        std::shared_lock<std::shared_mutex> lock(root->GetTreeMutex());
        for (const auto &child : *root) {
            if (child && child->GetData() == "過期項目") {
                to_remove.push_back(child); // 僅收集指針，絕不在此調用 RemoveChild！
            }
        }
    } // 讀鎖在此安全解構釋放！

    // 【第二階段：無鎖或依需獲取寫鎖批次執行拓撲異動】
    for (const auto &child : to_remove) {
        root->RemoveChild(child); // 安全！內部獨占寫鎖不會與讀鎖衝突
    }
}
```

---

## 8. 外掛 Heap 追蹤與清空檢驗 (Heap Tracker & Plugin Heap)

為防範外掛（Plugin / MODULE）在結束或卸載前遺留記憶體洩漏，OuroKore 提供雙軌並行的 Heap 追蹤與檢驗系統，並支援在編譯期彈性選擇 **方案 A** 或 **方案 B**。

### 8.1 編譯期方案選擇 (Compile-Time Policy)

| 方案 | 識別巨集 | Debug 行為 | Release 行為 | 適用情境 |
| :--- | :--- | :--- | :--- | :--- |
| **方案 A (預設)** | `ORK_HEAP_POLICY_A` | 詳細診斷 (記錄檔名/行號/序號) | 完全關閉 (零開銷直通，無鎖無記帳) | 追求發布版極致原生速度 |
| **方案 B** | `ORK_HEAP_POLICY_B` | 詳細診斷 (記錄檔名/行號/序號) | 輕量原子無鎖計數 (記錄區塊與大小) | 發布版仍需驗收 Heap 是否清空 |

#### CMake 編譯指定方式：
```bash
# 選擇方案 A (Debug 詳細 / Release 零開銷關閉)
cmake -B build -DOUROKORE_HEAP_POLICY=A

# 選擇方案 B (Debug 詳細 / Release 輕量無鎖原子計數)
cmake -B build -DOUROKORE_HEAP_POLICY=B
```

亦可在程式碼或 Target 編譯選項中手動定義：
```cpp
#define ORK_HEAP_POLICY ORK_HEAP_POLICY_A // 或 ORK_HEAP_POLICY_B
#include <ourokore/base/HeapTracker.hpp>
```

### 8.2 全域透明運算子重載 (Global Overload Mode)

在外掛 MODULE 動態庫的任一主實作檔（如 `PluginMain.cpp`）中宣告：
```cpp
#include <ourokore/base/PluginHeap.hpp>

// 一行啟動該外掛模組全域 operator new/delete/new[]/delete[] 重載
ORK_ENABLE_PLUGIN_HEAP_TRACKING()
```
* **效果**：該外掛模組內部所有的 `new`、`delete` 以及 STL 容器（如 `std::vector`、`std::string` 等）之堆配置全部透明導向受管追蹤，業務程式碼無需修改任何一行。在方案 A 的 Release 組態下自動展開為空實作，零額外開銷。

### 8.3 顯式受管 new / delete 巨集 (Explicit Tracked Mode)

若需在原始碼中精確標記檔案與行號位置：
```cpp
#include <ourokore/base/TrackedNewDelete.hpp>

// 1. 單一物件建立與釋放 (自動於編譯期捕捉 __FILE__ 與 __LINE__)
Monster* m = ORK_NEW(Monster, "Goblin", 100);
ORK_DELETE(m);

// 2. 陣列建立與釋放
int* buffer = ORK_NEW_ARRAY(int, 256);
ORK_DELETE_ARRAY(buffer, 256);

// 3. STL 容器整合 (TrackedAllocator)
std::vector<int, ork::TrackedAllocator<int>> my_vec;
```

### 8.4 外掛結束前清空判定與洩漏診斷 (Zero-Leak Verification)

外掛在 `PluginShutdown()` 或 DLL 卸載前檢驗 Heap 狀態：
```cpp
extern "C" PLUGIN_EXPORT int32_t PluginShutdown() {
    // 1. 查詢是否已完全清空 (無任何殘留配置)
    if (!ork::PluginHeap::is_clean()) {
        // 2. 輸出格式化 UTF-8 洩漏清單 (含序號、位址、大小、檔名行號)
        std::cerr << ork::PluginHeap::dump_leaks_to_string("MyPlugin");
        return -1; // 告知宿主尚有未釋放資源
    }
    return 0; // 成功清空
}

// 嚴格斷言：若未清空立即印出報告並拋出 std::runtime_error
ork::PluginHeap::assert_clean("MyPlugin");

// RAII 守衛：離開作用域時自動檢查，若有洩漏自動輸出至 stderr
{
    ork::PluginHeapGuard guard("PluginScope");
    // 執行外掛邏輯...
}
```

### 8.5 跨語言純 C ABI (`ourokore/base/heap_api.h`)

底層提供純 C ABI，供 C#、Rust、Python 進行記憶體檢查與 FFI 對接：
* `ork_heap_allocate(size, file, line)` / `ork_heap_deallocate(ptr)`
* `ork_heap_is_clean()` -> 傳回 `1`（已清空）或 `0`（未清空）
* `ork_heap_get_active_allocations()` / `ork_heap_get_active_bytes()`
* `ork_heap_dump_leaks(out_buf, buf_size)`
* `ork_heap_assert_clean(context_name)`
''', encoding="utf-8")

    print("✅ specs/manual/ 全套 8 份說明書手冊生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_manual_specs(specs_dir)

