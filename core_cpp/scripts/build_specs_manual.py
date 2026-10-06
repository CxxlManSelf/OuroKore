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

---

## 📚 使用手冊導引索引 (Manual Index)

1. [01. 系統概述與架構哲學](01_introduction.md) - 心智模型、四大基石與三層邊界隔離哲學
2. [02. 5 分鐘快速上手](02_quickstart.md) - 宿主初始化、自訂領域物件、屬性存取與存檔
3. [03. 領域物件設計規範](03_domain_object_design.md) - Getter/Setter、OuroReadLock/OuroWriteLock、原子標髒
4. [04. Handle 拓撲管理系統](04_handles_and_topology.md) - OwningHandle、UnboundHandle、OwningContainerHandle、OuroPtr
5. [05. 自動換頁脫水與儲存驅動](05_dehydration_and_storage.md) - 記憶體脫水、透明按需復水、LRU 策略配置
6. [06. 宿主生命週期與特權管理](06_host_lifecycle.md) - HostContext 獨佔特權、插件隔離防護、優雅退出
7. [07. 樹狀結構容器與文字 DSL 指南](07_tree_and_dsl.md) - TreeNode 雙模態容器、顯式堆疊走訪與正交無等號 DSL
8. [08. 基礎工具庫指南](08_base_utilities.md) - DynamicLibrary 生命週期反向錨定、constexpr Hash、並行同步與 UTF-8
9. [09. 外掛 Heap 追蹤與記憶體防禦指南](09_heap_and_memory.md) - 編譯期 A/B 方案、全域重載、外掛結束前洩漏檢驗與純 C ABI
10. [10. 公開 C++ API 參照手冊](10_api_reference.md) - 完整公開 API 清單與核心型別定義（終端速查字典附錄）
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

    # 07_tree_and_dsl.md
    (manual_dir / "07_tree_and_dsl.md").write_text(r'''# 07. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)

本章節介紹 OuroKore 基礎工具庫（`ourokore_base`）中的現代高效能階層容器 `TreeNode<T>`（`TreeNodeBase<Derived>`）與文字 DSL 串流工具 `TreeIO`。

---

## 🧭 1. 設計哲學與心智模型

### 1.1 純粹樹狀結構模型（Pure Tree Model）與物件本體論
* **節點自身即為物件本體（The Node IS The Object）**：
  在 CRTP 架構下，衍生類別 `D` 自身就是具備實體記憶體佈局與業務屬性的 C++ 物件。`TreeNodeBase<D>` 是作為**「樹狀拓撲能力注入基底（Tree Topology Mixin）」**，為領域物件賦予父子層級、具名索引、整樹讀寫鎖與防爆棧析構能力。因此在概念上，**物件始終存在且為核心主體**，不存在「物件可有可無」的皮囊容器幻象。
* **徹底終結「陣列 vs 非陣列」假性劃分**：
  不論是屬性目錄還是元素清單，在樹的本質上**全都統一為子節點（Children）**。
  - 底層統一由連續向量（`std::vector`）保序管理，享有快取極速連續讀取。
  - 具名字節點由雜湊表提供 $O(1)$ 名稱尋址。
  - 下標存取（`node[0]`）與鍵名存取（`node[u8"HP"]`）100% 互通。
  - **人體工學便利別名保留**：`CreateArray()`、`PushElement()`、`ElementCount()` 依舊完好保留，供使用者依習慣自由選用。

### 1.2 節點三維度正交模型
每個樹節點均具備 3 個獨立維度（可任意組合）：
1. **名稱（Name）**：具名節點（`[Name]`）或 匿名節點。
2. **資料（Data / Value）**：帶有本體字串資料（`"Data"`）或 無資料。
3. **子節點（Children）**：擁有子節點區塊（`{ ... }`）或 葉節點。

### 1.3 百萬層深樹顯式堆疊迭代防爆棧析構與反序列化
* **析構防爆棧**：內建顯式堆疊展平析構機制，巨型深樹解構時將遞迴展開為堆積迴圈以 $O(1)$ 呼叫深度安全釋放，杜絕 Stack Overflow。
* **反序列化防爆棧**：狀態機由 Heap 上的顯式堆疊 `std::vector<ParseFrame>` 驅動，呼叫棧深度恆為 $O(1)$。

---

## 📝 2. 文字 DSL 語法與界定符

OuroKore 文字 DSL 語法規則極致精簡、自洽且無歧義：

| 語法 Token | 角色 | 語意說明 |
| :--- | :--- | :--- |
| `[` ... `]` | 節點名稱標識 | 定義具名節點標記，跳脫字元支援 `\]` 與 `\\` |
| **`=`** | **具名賦值關鍵字** | **將後續引號內容賦值予該具名節點（具名節點有值時必然使用）** |
| `"` ... `"` | 節點資料內容 | 原始位元組直接傳遞（0~255 二進位安全），跳脫字元支援 `\\` 與 `\"` |
| `{` ... `}` | 子節點容器區塊 | 進入 / 退出子節點層級（全面統一為大括號，Allman 風格獨立換行） |

### 賦值運算子 `=` 與純標籤規則
* **具名賦值**：`[HP] = "100"`（必須有 `=`）。
* **純標籤（無值節點）**：`[IsAdmin]`（無等號）。
* **匿名子節點**：直接以引號開頭 `"草藥"`。
* **無歧義保證**：當出現 `[IsAdmin]` 後緊接 `"草藥"`，狀態機能 100% 確定 `IsAdmin` 為無值標籤完成，而 `"草藥"` 為下一個獨立的匿名子節點！

### 註解語法原生支援
* **`//` 單行註解**：忽略至行尾。
* **`/* ... */` 區塊註解**：忽略至閉合符號 `*/`。
* **`#` 腳本風格單行註解**：忽略至行尾。
* 狀態機在關鍵標記以外的地方寬容無視所有雜訊；若欲加入說明文字，強烈建議使用註解符號避免干擾。

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

// 2. 建立具名屬性
auto hp = player->AddChild(u8"HP");
hp->SetData("100");

// 3. 建立純標籤
player->AddChild(u8"IsActive");

// 4. 建立子清單 (PushElement 建立匿名子節點)
auto inventory = player->AddChild(u8"Inventory");
inventory->PushElement()->SetData("草藥");
inventory->PushElement()->SetData("黃金盔甲");

// 5. 互通性驗證
assert(inventory->ElementCount() == 2);
assert((*inventory)[0]->GetData() == "草藥");     // O(1) 連續向量下標存取
assert((*player)[0] == hp);                      // 具名節點亦可透過下標 0 存取！
assert((*player)[u8"HP"] == hp);
```

---

## 🗜️ 4. 序列化與排版模式 (Pretty vs Compact)

`TreeIO` 提供兩種核心序列化模式：

### 4.1 格式化排版模式 (CompactMode::Pretty - Allman 風格)
大括號 `{` 獨立換行，縮排層次清晰，人類可讀性極高：
```dsl
[Player] = "英雄角色"
{
  [HP] = "100"
  [IsActive]
  [Inventory]
  {
    "草藥"
    "黃金盔甲"
  }
}
```
呼叫方式：
```cpp
// 格式化輸出至串流
TreeIO::Serialize(std::cout, player, CompactMode::Pretty);
```

### 4.2 緊湊模式 (CompactMode::Compact)
無縮排與換行，去除所有多餘空白，但**必然保留關鍵字等號 `=`**，體積最小、傳輸效率最高：
```dsl
[Player]="英雄角色"{[HP]="100"[IsActive][Inventory]{"草藥""黃金盔甲"}}
```
呼叫方式：
```cpp
std::string compact_dsl = TreeIO::SerializeToString(player, CompactMode::Compact);
```

---

## 🔄 5. 寬容型反序列化與二進位安全

寬容型有限狀態機（FSM）自動略過非預期雜訊，並完整支援串流與字串解析：

```cpp
std::string dsl_text = R"(
    // 伺服器角色存檔
    [Player] = "英雄角色" // 主角摘要
    {
        [HP] = "100" # 生命值
        [IsAdmin]   // 純旗標標籤
        [Inventory]
        {
            "草藥"
            /* 暫時排除裝備："生鏽鐵劍" */
            "黃金盔甲"
        }
    }
)";

// 支援從 std::istream 或 std::string_view 直接解析
auto restored = TreeIO::DeserializeFromString(dsl_text);

assert(restored->GetName() == u8"Player");
assert((*restored)[u8"HP"]->GetData() == "100");
assert((*restored)[u8"IsAdmin"]->GetData().empty()); // 純標籤無資料
assert((*(*restored)[u8"Inventory"])[0]->GetData() == "草藥");
```

### 二進位安全保證
字串引號 `""` 內部支援 0~255 全位元組原始數值直接傳遞（二進位安全零膨脹），序列化時僅針對 `\\` 與 `\"` 進行必要跳脫，反序列化時原生支援 `\0`、`\n`、`\r`、`\t` 等常見跳脫字元。

---

## 🧬 6. CRTP 自定義衍生領域節點與多型階層

### 6.1 CRTP 領域節點與反序列化自訂轉化
應用端可透過 CRTP 繼承 `TreeNodeBase<Derived>` 打造專屬領域實體物件，享有整樹拓撲與型別安全：

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
    [](const std::shared_ptr<HeroNode> &node, const std::string &raw_val) {
        node->role_title = raw_val;
    }
);

static_assert(std::is_same_v<decltype(hero), std::shared_ptr<HeroNode>>);
assert(hero->role_title == "英雄角色");
```

### 6.2 多型衍生階層模板支援（C++20 Concepts 約束與零轉型直出）
當領域節點存在進一步繼承階層（例如 `BaseEntity` 衍生出 `MonsterEntity`、`ItemEntity`）時，`TreeNodeBase<D>` 為所有新增與工廠介面（`CreateRoot`、`CreateArray`、`MakeNode`、`AddChild`、`AddBackChild`、`PushElement`、`InsertBefore`、`InsertAfter`）提供了現代化 C++20 模板多載：
* **嚴格概念約束**：`template <typename SubT = D, typename... Args> requires std::derived_from<SubT, D>`，非衍生類別於編譯期嚴格拒絕。
* **完美轉發構造**：支援直接轉發建構子參數至 `SubT`，無論是以 `(name, args...)` 或是自訂 `(args...)` 均自動推導相容。
* **強型別零手動轉型（Zero-Casting）**：直接回傳 `std::shared_ptr<SubT>`，呼叫端無須進行任何 `std::static_pointer_cast` 或 `std::dynamic_pointer_cast` 即可直接存取衍生類別成員。
* **容器自動向上轉型（Upcasting）**：以 `std::shared_ptr<SubT>` 存入底層容器，享有連續快取友善保序管理。
* **100% 向後相容**：未指定模板參數時預設為 `SubT = D`，對現有程式碼完全零衝擊。

```cpp
class BaseEntity : public ork::base::TreeNodeBase<BaseEntity> {
public:
    virtual ~BaseEntity() = default;
    virtual std::string GetType() const { return "BaseEntity"; }
protected:
    explicit BaseEntity(std::u8string name = u8"") : Base(std::move(name)) {}
    template <typename D> friend class TreeNodeBase;
};

class MonsterEntity : public BaseEntity {
public:
    int hp{100};
    int atk{20};
    MonsterEntity(std::u8string name, int in_hp, int in_atk)
        : BaseEntity(std::move(name)), hp(in_hp), atk(in_atk) {}
    std::string GetType() const override { return "Monster"; }
};

class ItemEntity : public BaseEntity {
public:
    int price{0};
    explicit ItemEntity(int in_price) : BaseEntity(u8""), price(in_price) {}
    std::string GetType() const override { return "Item"; }
};

// 1. 建立根節點
auto root = BaseEntity::CreateRoot(u8"Dungeon");

// 2. 零轉型直接新增強型別衍生節點 (回傳 std::shared_ptr<MonsterEntity>)
std::shared_ptr<MonsterEntity> boss = root->AddChild<MonsterEntity>(u8"BossDragon", 5000, 350);
boss->hp -= 200; // 直接存取衍生屬性，無需型別轉換！

// 3. 原地構造並推入陣列元素
std::shared_ptr<ItemEntity> potion = root->PushElement<ItemEntity>(50);

// 4. 精準指定位置插入衍生節點
std::shared_ptr<MonsterEntity> minion = root->InsertBefore<MonsterEntity>(boss, u8"Goblin", 100, 15);
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
''', encoding="utf-8")

    # 08_base_utilities.md
    (manual_dir / "08_base_utilities.md").write_text(r'''# 08. 基礎工具庫指南 (Base Foundation & Utilities)

本章節介紹 OuroKore 基礎模組（`ourokore_base`）中提供的通用現代基礎設施與工具庫。這些工具零依賴上層核心邏輯（`ourokore_core`），遵循現代 ISO C++20 標準，具備高效能、跨平台與極致執行緒安全特性。

---

## 🧭 基礎工具庫總覽

`ourokore_base` 模組包含以下關鍵子系統：

```
+-----------------------------------------------------------------------------------+
|                           ourokore_base 基礎工具庫                                |
+-----------------------------------------+-----------------------------------------+
| 1. 動態模組載入器 (DynamicLibrary)       | 2. 現代編譯期雜湊模組 (Hash)            |
|    - 生命週期反向錨定 (Life-Bound)      |    - C++20 constexpr 編譯期計算         |
|    - 弱引用晉升 (WeakDynamicLibrary)    |    - FNV-1a (TypeID 唯一標準)           |
|    - 純存活權杖 (LifetimeToken)         |    - CRC32 / MurmurHash3 / HashCombine  |
|    - 後置卸載通知與非同步離棧延遲卸載   |    - 使用者自訂字面量 (_fnv64, _crc32)  |
+-----------------------------------------+-----------------------------------------+
| 3. 並行與多執行緒排程 (Concurrency)     | 4. 全域文字與字串標準 (UTF-8)            |
|    - 固定執行緒池 (FixedThreadPool)     |    - 跨平台零拷貝視圖轉換 (as_view)     |
|    - 動態彈性伸縮池 (DynamicThreadPool) |    - to_string / to_u8string            |
|    - 執行緒安全佇列 (ThreadSafeQueue)   |    - Windows Unicode W 邊界隔離         |
|    - 計數信號量與事件 (Semaphore/Event) |                                         |
+-----------------------------------------+-----------------------------------------+
| 5. 樹狀結構容器與文字 DSL (Tree & TreeIO) ── 詳見《07. 樹狀結構容器與文字 DSL 指南》|
+-----------------------------------------------------------------------------------+
```

---

## 🧩 1. 動態模組載入器 (DynamicLibrary & WeakDynamicLibrary)

* **標頭檔**：`<ourokore/base/DynamicLibrary.hpp>`
* **命名空間**：`ork`（相容於 `ork::base::DynamicLibrary` 別名）
* **目標情境**：動態擴充外掛（Plugin / Component）、熱載入邏輯模組、跨平台符號解析、跨模組生命週期自動安全管理。

---

### 1.1 核心設計哲學：生命週期反向錨定與禁絕手動卸載

傳統動態庫載入器通常提供顯式的 `unload()` 函式，但在多執行緒或複雜物件圖中，提前手動卸載動態庫是導致致命崩潰（Access Violation / SIGSEGV）的首要元兇——當外部執行緒或背景佇列仍在執行物件的虛擬函式時，其虛擬函式表（vtable）與程式碼段已被作業系統解除映射（Unmap），立即引發 UAF。

OuroKore 徹底顛覆手動卸載思維，確立以下三大鐵律：
1. **禁絕手動卸載 (No Manual Unload)**：`DynamicLibrary` 刻意不提供任何手動 `unload()` 介面。
2. **生命週期反向錨定 (Life-Bound Retention)**：應用端將動態庫「所產生的物件」與動態庫建立生命週期綁定（透過 `bind_lifecycle()`、工廠 Deleter 閉包或存活權杖）。**只有當由該動態庫產生的所有活體物件全部解構銷毀後，底層動態庫才會在引用計數歸零時自動且安全地由底層卸載（`FreeLibrary` / `dlclose`）**。
3. ⚠️ **關鍵約束：`load()` 回傳值之生命週期持有與放棄**：
   `DynamicLibrary::load()` 的回傳值本身「持有一份動態庫引用（use_count >= 1）」。
   **若呼叫端長存此回傳值變數（例如存為全域變數、類別長存成員，或未離開作用域/未呼叫 `reset()`），動態庫就永遠不會被卸載！**
   應用端若希望實現「物件全數銷毀後 DLL 自動卸載」，必須在完成物件建立與綁定後，主動呼叫 `lib.reset()` 或讓局部句柄離開作用域，將存活權杖全權移交給受管物件持有。

---

### 1.2 多重載入快取與首度載入單次初始化 (Cache & Initialization)

當進程內多個子系統或模組在不同時機請求載入同一個動態庫時，`DynamicLibrary` 內部會以標準化絕對路徑進行弱引用快取共享。

* **`is_first_loaded()`**：查詢當前實例是否為動態庫於進程中的**首次載入（引用計數 0 -> 1）**。
  * 若傳回 `true`：代表該庫剛被載入進程，呼叫端應執行模組級全域初始化。
  * 若傳回 `false`：代表此庫先前已由其他模組載入且仍在記憶體中存活（引用計數 1 -> 2），呼叫端應避免重複初始化以防止狀態衝突。
* **`initialize_once<FuncT>(symbol_name, args...)`**：便捷的單次初始化樣板函式。**僅在 `is_first_loaded() == true` 時呼叫指定符號函式**；若為重複載入則自動安全略過並回傳 `false`。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/physics_engine.dll");
if (lib.is_loaded()) {
    // 僅在首次載入進程時執行一次 PhysicsInit(gravity=9.8f)；若已被其他模組載入過則自動略過
    lib.initialize_once<void(float)>("PhysicsInit", 9.8f);
}
```

---

### 1.3 兩階段卸載回呼系統 (Two-Stage Unload Hooks)

為了在動態庫生命週期走向終結時進行安全清理與狀態通知，`DynamicLibrary` 提供了兩階段、不同時機的卸載掛鉤：

```
主程式執行緒 (Main Thread)        DynamicLibrary 背景等待執行緒         外掛 DLL (Plugin)
        │                                  │                                   │
  1. 釋放最後引用 (如 lib.reset())         │                                   │
        │ ── 觸發卸載 (非同步交棒) ───────> │                                   │
  2. 立即返回繼續主程式工作！              │ ── 調用非同步善後函式 ──────────> │ 3. 執行冗長善後...
     (主程式 0ms 延遲、完全零卡頓)          │    (附帶 on_ready_to_unload 回呼) │    - 快取與資料落盤
        │                                  │                                   │    - 釋放 GPU/緩衝區
        │                                  │ ── 背景阻塞等待握手通知 ────      │    - 關閉連線或背景執行緒
        │                                  │                            │      │
        │                                  │ <── 呼叫 on_ready_to_unload() ────│ 4. 善後徹底完畢！
        │                                  │     (握手喚醒背景線程)            │    (外掛不再執行任何代碼)
        │                                  │                                  
        │                                  │ 5. 收到確認，呼叫 FreeLibrary() 物理卸載 DLL
        │                                  │ 6. 觸發 post_unload_hooks 通知主程式
```

#### 1. 第一階段：卸載前收尾與非同步握手協定 (Pre-Unload & Async Handshake)
* **同步收尾：`add_cleanup_hook(std::function<void()> hook)`**：註冊在動態庫卸載前執行的同步收尾回呼（保證代碼段與 vtable 依然完整有效，LIFO 順序執行）。
* **同步符號：`register_shutdown_symbol(std::string_view symbol_name)`**：依據符號名稱自動註冊無參 `void()` 函式為收尾回呼。
* 🌟 **非同步握手收尾：`add_async_cleanup_hook(AsyncCleanupHook hook)`**：
  * **設計目的**：解決外掛 shutdown 冗長善後導致主程式卡頓問題。
  * **握手運作**：主程式觸發卸載後**立即返回繼續運行（0ms 延遲）**；`DynamicLibrary` 在背景等待執行緒中調用 hook，外掛在完成所有耗時工作後主動呼叫傳入的 `on_ready_to_unload()`。背景執行緒收到通知被喚醒後，才執行 `FreeLibrary` 物理卸載 DLL！
* 🌟 **純 C 非同步符號：`register_async_shutdown_symbol(std::string_view symbol_name)`**：
  * 支援跨語言 C ABI：外掛導出 `void PluginAsyncShutdown(void (*on_ready)(void*), void* user_data)`。
* **逾時保護：`set_async_shutdown_timeout(std::chrono::milliseconds timeout)`**：設定非同步善後最大等待逾時（預設 30 秒），防範外掛死鎖。

#### 2. 第二階段：卸載完成通知 (Post-Unload Hook)
* **`add_post_unload_hook(std::function<void()> hook)`**：註冊在動態庫完成作業系統物理卸載後執行的通知回呼。
  * **目的**：宿主被動接收「外掛已完全死透、資源已全數釋放」事件，無需輪詢。
  * **高壓警戒**：此時動態庫程式碼段已解除映射，回呼閉包內部**絕對嚴禁**存取動態庫中的任何指標或呼叫其函式！

```cpp
auto lib = ork::DynamicLibrary::load("plugins/render_system.dll");

// 1. 【同步模式】註冊卸載前收尾：在 FreeLibrary 前同步清理
lib.add_cleanup_hook([]() {
    std::cout << "[Pre-Unload] 正在清理外掛內部 GPU 緩衝區..." << std::endl;
});
lib.register_shutdown_symbol("RenderShutdown");

// 2. 🌟【非同步握手模式】註冊非同步善後（主程式 0ms 立即返回，外掛背景耗時善後完畢後握手卸載）
lib.add_async_cleanup_hook([](ork::DynamicLibrary::ReadyToUnloadCallback on_ready) {
    std::thread([on_ready = std::move(on_ready)]() {
        std::cout << "[外掛背景] 正在非同步落盤大型存檔與中斷網絡...
";
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 耗時善後
        std::cout << "[外掛背景] 善後全數完畢！通知 DynamicLibrary 可以 FreeLibrary 了。
";
        
        // 握手確認：外掛保證絕不再執行任何代碼，喚醒卸載等待線程
        on_ready();
    }).detach();
});

// 3. 註冊卸載後通知：僅更新宿主狀態，絕不碰觸外掛代碼
lib.add_post_unload_hook([]() {
    std::cout << "[Post-Unload] 渲染插件已完全從記憶體卸載！宿主切換為軟體渲染模式。" << std::endl;
});
```

---

### 1.4 非同步離棧延遲卸載防護 (Deferred Stack-Decoupled Unload)

#### 崩潰陷阱：自解構呼叫棧陷阱 (Self-Unload Stack Trap)
考慮以下極端但常見的場景：
外掛定義了一個類別 `class PluginNode`，其虛擬解構式 `virtual ~PluginNode()` 編譯在外掛 DLL 內部代碼段中。當應用端銷毀最後一個 `PluginNode` 實例時：
1. 呼叫 `PluginNode` 的虛擬解構函式（此時當前執行緒的 Call Stack 頂層正處於 DLL 內部代碼段）。
2. 在該解構函式內部或 Deleter 中，最後一個動態庫引用歸零，觸發同步呼叫 `FreeLibrary(hDll)`。
3. **作業系統立即將 DLL 代碼段從記憶體中抹除！**
4. 呼叫棧嘗試從虛擬解構函式返回至呼叫端——但返回位址所在的代碼段已經消失，瞬間引發不可挽回的 `0xC0000005: Access Violation` 崩潰！

#### 解決方案：`enable_deferred_unload(true)`
* **用法**：呼叫 `lib.enable_deferred_unload(true);` 啟用離棧延遲卸載保護。
* **機制**：當最後一個引用歸零時，動態庫卸載動作會自動移交給**獨立的背景執行緒**執行，確保當前物件的解構呼叫棧完全退出後才卸載代碼段，達成 100% 絕對安全的自毀與卸載。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/node_system.dll");

// 啟用非同步離棧卸載防護
lib.enable_deferred_unload(true);

// 即使最後一個節點在外掛自身的代碼段中觸發解構，呼叫棧也能全身而退！
```

---

### 1.5 純生命週期存活權杖 (Pure Lifetime Token)

在傳統模式中，我們透過 `bind_lifecycle(raw_ptr, deleter)` 綁定單一裸指標。但對於複雜的樹狀結構（如 `TreeNodeBase` 百萬節點群）、非同步工作任務（Worker Tasks）或會話物件（Sessions），沒有單一裸指標適合承擔整個 DLL 的生命週期。

* **`create_lifetime_token()`**：產生一個型別擦除的純權杖（`std::shared_ptr<const void>`）。
* **特性**：
  * 該 Token 內部持有一份動態庫存活引用。
  * 樹狀結構的所有節點或多個非同步閉包均可複製並持有此 Token。
  * 只要宇宙中尚有任一節點存活，DLL 代碼段便長存有效；最後一個節點解構使 Token 計數歸零時，自動觸發底層動態庫安全卸載。

```cpp
auto lib = ork::DynamicLibrary::load("plugins/tree_module.dll");

// 產生純存活權杖
std::shared_ptr<const void> token = lib.create_lifetime_token();

// 宿主主動放棄強引用
lib.reset();

// 建立樹節點，所有節點共享持有此 Token
struct MyNode {
    std::string name;
    std::shared_ptr<const void> dll_token;
};

auto root = std::make_shared<MyNode>("Root", token);
auto child1 = std::make_shared<MyNode>("Child1", token);

// 即使 root 被釋放，只要 child1 仍存活，DLL 就絕不會被卸載！
root.reset();
assert(child1 != nullptr); // DLL 依然存活

// 當最後一個節點釋放，DLL 安全自動卸載
child1.reset();
```

---

### 1.6 弱引用觀察與晉升重獲 (WeakDynamicLibrary)

當主程式為了實現自動卸載而呼叫 `lib.reset()` 或讓局部變數離開作用域時，主程式原本的 `DynamicLibrary` 變數已歸零。如果日後主程式又需要使用該動態庫（例如再次解析符號、創建物件或檢查外掛存活狀態），該怎麼辦？

`WeakDynamicLibrary` 提供了類似 `std::weak_ptr` 的無所有權觀察與安全重獲機制：

```cpp
auto lib = ork::DynamicLibrary::load("plugins/ai_module.dll");

// 1. 取得弱引用觀察者（不增加強引用計數，不阻止自動卸載）
ork::WeakDynamicLibrary weak_lib = lib.to_weak();

// 2. 建立業務物件並綁定生命週期
auto entity = lib.bind_lifecycle(CreateRawAI(), &DestroyRawAI);

// 3. 宿主主動放棄強引用句柄
lib.reset();
assert(!lib.is_loaded()); // 宿主句柄為空

// 4. 此時 entity 依然存活，DLL 尚未卸載
assert(!weak_lib.expired());
assert(weak_lib.use_count() == 1); // entity 仍持有 1 份

// 5. 核心：日後主程式再次需要使用時，透過 lock() 零開銷安全晉升重獲強引用！
if (auto locked = weak_lib.lock()) {
    // 成功重獲有效 DynamicLibrary（無需調用作業系統 LoadLibrary，零 I/O）
    auto fn = locked.get_symbol<void(*)()>("GlobalAIStep");
    if (fn) fn();
}

// 6. 業務物件全數解構
entity.reset();

// 7. DLL 已物理卸載，弱引用安全過期
assert(weak_lib.expired());
auto failed_lock = weak_lib.lock();
assert(!failed_lock.is_loaded()); // 安全傳回無效實例，絕不崩潰
```

---

### 1.7 跨平台檔名格式化與載入旗標

* **`format_filename(base_name)`**：依據當前作業系統規範自動產生動態庫檔名：
  * Windows：`"my_plugin.dll"`
  * Linux：`"libmy_plugin.so"`
  * macOS：`"libmy_plugin.dylib"`
* **`LibraryLoadFlags` 載入旗標**：
  * `LibraryLoadFlags::ResolveNow`：立即解析所有符號（POSIX: `RTLD_NOW`，Windows 預設）。
  * `LibraryLoadFlags::ResolveLazy`：延遲按需解析符號（POSIX: `RTLD_LAZY`）。
  * `LibraryLoadFlags::ScopeLocal`：符號私有隔離，不外洩給其他模組（POSIX: `RTLD_LOCAL`，預設）。
  * `LibraryLoadFlags::ScopeGlobal`：符號全域可見（POSIX: `RTLD_GLOBAL`）。
  * `LibraryLoadFlags::SearchDllDir`：Windows 專用，優先搜尋 DLL 所在目錄與其相依項。
* **全域 UTF-8 路徑支援**：
  * `load("路徑/插件.dll")` 內部一律轉換為 Unicode UTF-16 呼叫 `LoadLibraryW`，徹底解決多語系與繁體中文路徑亂碼失敗問題。
  * `get_path_utf8()` 保證傳回 100% 規範化的 UTF-8 絕對路徑。


## ⚙️ 2. 現代編譯期雜湊模組 (Hash.hpp)

* **標頭檔**：`<ourokore/base/Hash.hpp>`
* **命名空間**：`ork::base`，字面量命名空間：`ork::base::literals`
* **目標情境**：型別識別碼（TypeID）、字串鍵雜湊比對、封包校驗、資料完整性驗證。

### 特性與演算法

1. **C++20 constexpr 編譯期零開銷**：
   - 演算法全面支援編譯期求值，零執行期開銷。
2. **演算法矩陣**：
   * **FNV-1a (64-bit / 32-bit)**：OuroKore 系統中 `TypeID` 與全域識別碼的唯一標準演算法，計算速度極快、分佈優良。
   * **CRC32 (IEEE 802.3)**：標準循環冗餘校驗，廣泛用於二進位串流、藍圖存檔與通訊封包防竄改檢查。
   * **MurmurHash3 (32-bit)**：高品質通用雜湊演算法，具備極強的雪崩效應（Avalanche Effect），適合哈希表尋址。
   * **HashCombine**：變參雜湊組合函式，適用於多欄位複合鍵雜湊。

### 範例程式碼

```cpp
#include <ourokore/base/Hash.hpp>
#include <iostream>
#include <cassert>

using namespace ork::base::literals;

void HashDemo() {
    // 1. 編譯期常數計算（使用字面量運算子）
    constexpr uint64_t type_id = "PlayerCharacter"_fnv64;
    constexpr uint32_t type_id32 = "PlayerCharacter"_fnv32;
    constexpr uint32_t crc = "BLUEPRINT_HEADER"_crc32;

    // 2. 執行期字串與記憶體區塊雜湊
    std::string player_name = "Arthur";
    uint64_t name_hash = ork::base::Fnv1a64(player_name);
    assert(name_hash == "Arthur"_fnv64);

    // 3. CRC32 資料完整性校驗
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t checksum = ork::base::Crc32(payload);

    // 4. MurmurHash3 (32-bit) 帶種子雜湊
    uint32_t seed = 0x9747b28c;
    uint32_t murmur = ork::base::MurmurHash3("SampleKey", seed);

    // 5. 複合屬性鍵組合 (HashCombine)
    size_t combined = 0;
    ork::base::HashCombine(combined, type_id, name_hash, checksum);
    std::cout << "複合雜湊值: " << combined << std::endl;
}
```

---

## ⚡ 3. 並行與多執行緒排程 (ThreadPool, Queue, Semaphore, Event)

* **標頭檔**：
  * `<ourokore/base/ThreadPool.hpp>`
  * `<ourokore/base/ThreadSafeQueue.hpp>`
  * `<ourokore/base/Semaphore.hpp>`
* **命名空間**：`ork::base`

### 3.1 固定數量執行緒池：`FixedThreadPool`

建立恆定數量的 Worker 執行緒，適用於 CPU 密集型運算或任務數量穩定之場景。

* **核心特性**：
  * 支援 `submit`：提交任務並回傳 `std::future<ReturnType>`，支援任意函式與參數完美轉發。
  * 支援 `submit_detached`：Fire-and-Forget 任務排程，避免 `packaged_task` 內部堆積配置。
  * `wait_idle()`：阻塞等待直到所有排隊任務與執行中任務全數完成。
  * **Worker 防自我死鎖**：Worker 執行緒內部調用 `wait_idle()` 時自動安全略過，防止自我等待死鎖。
  * RAII 優雅關閉：解構時自動呼叫 `stop()` 並等待所有已排隊任務處理完畢。

```cpp
#include <ourokore/base/ThreadPool.hpp>
#include <iostream>

void TestFixedPool() {
    // 建立 4 個 Worker 的固定執行緒池（傳入 0 則預設為 CPU 核心數）
    ork::base::FixedThreadPool pool(4);

    // 1. 提交有回傳值的任務 (Future)
    std::future<int> result = pool.submit([](int a, int b) {
        return a + b;
    }, 10, 20);

    std::cout << "計算結果: " << result.get() << std::endl; // 輸出 30

    // 2. 提交 Fire-and-Forget 輕量任務
    pool.submit_detached([]() {
        std::cout << "背景日誌處理完成" << std::endl;
    });

    // 3. 等待所有任務執行完畢
    pool.wait_idle();
}
```

### 3.2 彈性動態伸縮執行緒池：`DynamicThreadPool`

依據即時任務負載量自動增減 Worker 執行緒，兼顧尖峰並發能力與離峰資源節能。

* **動態擴展**：當排隊任務數超過目前空閒 Worker 且未達 `max_threads` 時，即刻動態生成新 Worker。
* **空閒縮容回收**：Worker 空閒等待超過指定逾時時間（`idle_timeout`，預設 3000ms）時，自動終止並回收執行緒，直至保留核心常駐數量（`min_threads`）。

```cpp
#include <ourokore/base/ThreadPool.hpp>

void TestDynamicPool() {
    // min_threads=2, max_threads=8, idle_timeout=2000ms
    ork::base::DynamicThreadPool dynamic_pool(2, 8, std::chrono::milliseconds(2000));

    for (int i = 0; i < 20; ++i) {
        dynamic_pool.submit_detached([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }

    // 負載突增時，Worker 數量動態擴增至最高 8 個
    std::cout << "當前 Worker 數: " << dynamic_pool.get_current_worker_count() << std::endl;

    dynamic_pool.wait_idle();
    // 待任務清空且逾時 2 秒後，Worker 數量自動縮容回核心 2 個
}
```

### 3.3 執行緒安全阻塞佇列：`ThreadSafeQueue<T>`

多生產者-多消費者（MPMC）阻塞佇列，具備逾時取出與優雅關閉喚醒。

* `bool push(T item)` / `template <typename... Args> bool emplace(Args&&... args)`：推入元素，成功回傳 true；若佇列已停止則回傳 false。
* `bool pop(T &out_val)`：阻塞等待取出隊首元素。若佇列已停止且為空則傳回 false。
* `bool pop_for(T &out_val, rel_time)`：在指定逾時內等待取出隊首元素。
* `bool try_pop(T &out_val)`：非阻塞立即取出。
* `void stop()`：停止佇列並喚醒所有等待中的執行緒。
* `void clear()`：清空佇列，**於鎖外安全析構殘留元素**，避免持有鎖時解構複雜物件引發死鎖。

```cpp
#include <ourokore/base/ThreadSafeQueue.hpp>

ork::base::ThreadSafeQueue<std::string> message_queue;

// 生產者執行緒
message_queue.push("Task #1");

// 消費者執行緒
std::string msg;
if (message_queue.pop_for(msg, std::chrono::milliseconds(500))) {
    std::cout << "收到訊息: " << msg << std::endl;
}
```

### 3.4 計數信號量：`Semaphore`

跨平台計數信號量，支援阻塞獲取、逾時獲取與批次釋放。

* `acquire()`：阻塞等待可用資源計數大於 0 並遞減。
* `try_acquire()`：非阻塞嘗試獲取。
* `try_acquire_for(rel_time)`：逾時等待獲取。
* `release(ptrdiff_t update = 1)`：釋放並增加資源計數。
* `available()`：查詢目前可用資源快照。

```cpp
#include <ourokore/base/Semaphore.hpp>

ork::base::Semaphore sem(0); // 初始可用計數為 0

std::thread worker([&sem]() {
    sem.acquire(); // 阻塞等待信號
    std::cout << "Worker 開始執行" << std::endl;
});

// 主執行緒通知 Worker
sem.release();
worker.join();
```

### 3.5 事件通知同步原語：`Event`

跨平台事件通知原語，支援自動重設（`AutoReset`）與手動廣播（`ManualReset`）。

* **`EventResetMode::AutoReset`**：單一等待執行緒被喚醒後，自動重設為未觸發狀態（類似 Windows Auto-Reset Event）。
* **`EventResetMode::ManualReset`**：所有等待執行緒均被喚醒，需顯式呼叫 `reset()` 才會回到未觸發狀態（廣播模式）。
* 方法：`set()`, `reset()`, `wait()`, `wait_for(rel_time)`, `is_set()`。

```cpp
#include <ourokore/base/Semaphore.hpp>

// 建立手動重設廣播事件
ork::base::Event ready_event(ork::base::EventResetMode::ManualReset, false);

// 多個 Worker 執行緒等待初始化完成
// worker: ready_event.wait();

// 主執行緒廣播完成通知
ready_event.set();
```

---

## 🌐 4. 全域 UTF-8 零拷貝文字轉換輔助 (utf8.hpp)

* **標頭檔**：`<ourokore/base/utf8.hpp>`
* **命名空間**：`ork::utf8`

依據 OuroKore 全域字串規範（UTF-8 Standard Invariant），系統內部一律使用 UTF-8 編碼。`utf8.hpp` 提供方便的零拷貝轉換與概念萃取：

* `ork::utf8::as_view(str)`：將 `std::string`、`std::u8string`、`std::string_view`、`std::u8string_view`、`const char*`、`const char8_t*` 零拷貝轉為 `std::string_view`。
* `ork::utf8::to_string(str)`：統一轉換為標準 `std::string`。
* `ork::utf8::to_u8string(view)`：轉換為 C++20 原生 `std::u8string`。
* `ork::utf8::is_string_like_v<T>`：編譯期萃取，判斷是否為類字串型別。

---

## 🌳 5. 現代樹狀結構容器與文字 DSL 串流 (Tree & TreeIO)

適用於階層資料、設定檔、屬性樹、文字 DSL 串流存取與遊戲腳本配置。
詳細深入指南請參閱專章：
👉 **[08. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)](07_tree_and_dsl.md)**
''', encoding="utf-8")

    # 09_heap_and_memory.md
    (manual_dir / "09_heap_and_memory.md").write_text(r'''# 09. 外掛 Heap 追蹤與記憶體防禦指南 (Heap Tracker & Plugin Heap)

本章節介紹 OuroKore 基礎模組（`ourokore_base`）中的外掛 Heap 追蹤與記憶體洩漏防禦設施（`HeapTracker`、`PluginHeap`、`TrackedNewDelete` 與 `heap_api.h`）。

---

## 🧭 1. 設計哲學與心智模型

1. **外掛動態庫（MODULE）記憶體洩漏與 CRT 邊界防禦**：
   - 在微核心與外掛架構中，動態載入的外掛模組（`MODULE` / DLL）在生命週期結束或熱重載卸載前，若遺留任何未釋放的堆配置（Heap Allocation），其程式碼段與虛擬函式表卸載後將引發嚴重的記憶體洩漏甚至懸空崩潰。
   - OuroKore 提供雙軌並行的 Heap 追蹤與檢驗系統，支援在編譯期彈性選擇 **方案 A** 或 **方案 B**。

2. **防重入分配器保護 (RawSystemAllocator Guard)**：
   - 追蹤器內部維護配置記錄表時，其本身的容器（如雜湊表或陣列）嚴格使用 `RawSystemAllocator`（直接繞過重載的 `operator new`，直通底層系統 API），徹底杜絕內部記帳容器觸發遞迴死鎖與爆棧。

3. **C++17 對齊記憶體原生相容**：
   - 追蹤器全面相容 C++17 對齊配置要求（Windows 平台調用 `_aligned_malloc` / `_aligned_free`，POSIX 平台調用 `posix_memalign` / `free`）。

---

## ⚙️ 2. 編譯期雙策略選擇 (Compile-Time Policy)

| 方案 | 識別巨集 | Debug 行為 | Release 行為 | 適用情境 |
| :--- | :--- | :--- | :--- | :--- |
| **方案 A (預設)** | `ORK_HEAP_POLICY_A` | 詳細診斷 (記錄檔名/行號/序號) | 完全關閉 (零開銷直通系統 malloc，無鎖無記帳) | 追求發布版極致原生速度 |
| **方案 B** | `ORK_HEAP_POLICY_B` | 詳細診斷 (記錄檔名/行號/序號) | 輕量原子無鎖計數 (記錄區塊與大小) | 發布版仍需驗收 Heap 是否清空 |

### CMake 編譯指定方式：
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

---

## 🚀 3. 全域透明運算子重載 (Global Overload Mode)

在外掛 MODULE 動態庫的任一主實作檔（如 `PluginMain.cpp`）中宣告：
```cpp
#include <ourokore/base/PluginHeap.hpp>

// 一行啟動該外掛模組全域 operator new/delete/new[]/delete[] 重載
ORK_ENABLE_PLUGIN_HEAP_TRACKING()
```
* **效果**：該外掛模組內部所有的 `new`、`delete` 以及 STL 容器（如 `std::vector`、`std::string` 等）之堆配置全部透明導向受管追蹤，業務程式碼無需修改任何一行。在方案 A 的 Release 組態下自動展開為空實作，零額外開銷。

---

## 🔍 4. 顯式受管 new / delete 巨集 (Explicit Tracked Mode)

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

---

## 🛡️ 5. 外掛結束前清空判定與洩漏診斷 (Zero-Leak Verification)

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

---

## 🌐 6. 跨語言純 C ABI (`ourokore/base/heap_api.h`)

底層提供純 C ABI，供 C#、Rust、Python 進行記憶體檢查與 FFI 對接，保證跨語言邊界零例外外洩：
* `ork_heap_allocate(size, file, line)` / `ork_heap_deallocate(ptr)`
* `ork_heap_is_clean()` -> 傳回 `1`（已清空）或 `0`（未清空）
* `ork_heap_get_active_allocations()` / `ork_heap_get_active_bytes()`
* `ork_heap_dump_leaks(out_buf, buf_size)`
* `ork_heap_assert_clean(context_name)`

---

## ⚠️ 7. 外掛開發避坑指南與高壓線禁忌

1. **嚴格禁止跨動態庫混用配置與釋放**：
   - 由外掛模組內部配置之記憶體，必須由該模組自行釋放，嚴禁在宿主或其他外掛中以標準 `free`/`delete` 釋放，以防不同 CRT 實例導致堆損壞。
2. **動態庫卸載前必須 100% 驗收清空**：
   - 配合 `DynamicLibrary` 的生命週期反向錨定或兩階段卸載掛鉤，在外掛退出前務必調用 `PluginHeap::is_clean()` 或 `PluginHeap::assert_clean()`，確保零洩漏再允許卸載。
3. **CMake 構建規範**：
   - 動態外掛必須以 `add_library(<name> MODULE ...)` 構建，嚴禁宣告為 `SHARED`，確保獨立動態加載與乾淨卸載能力。
''', encoding="utf-8")

    # 10_api_reference.md (永遠排在最後一本作為終端字典附錄)
    (manual_dir / "10_api_reference.md").write_text(r'''# 10. 公開 C++ API 參照手冊 (API Reference)

本手冊彙整 OuroKore 面向應用開發者、外掛實作者與宿主主程式之所有公開核心類別、工具與介面，作為全套手冊之終端速查字典附錄。

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
* **設計哲學與卸載原則**：
  * **禁絕手動卸載 (No Manual Unload)**：動態庫不提供手動 `unload()`，杜絕野指針與 vtable 懸空崩溃。
  * **生命週期反向錨定 (Life-Bound Retention)**：物件全數銷毀後自動安全卸載。呼叫端若長存 `load()` 回傳之初始句柄則永遠不卸載，完成綁定後應主動 `reset()` 或移交權杖。
* **方法**：
  * `static DynamicLibrary load(std::string_view utf8_path, LibraryLoadFlags flags = Default)`：自 UTF-8 路徑載入動態庫。
  * `void reset() noexcept`：放棄句柄持有（扣減引用計數）。
  * `bool is_loaded() const noexcept`：查詢是否載入成功。
  * `const std::string &get_last_error() const noexcept`：取得 UTF-8 系統錯誤訊息。
  * `std::string get_path_utf8() const noexcept`：取得規範路徑。
  * `size_t use_count() const noexcept`：取得存活引用計數。
  * `template <typename FuncT> auto get_symbol(std::string_view name) const noexcept`：解析導出符號。
  * `bool is_first_loaded() const noexcept`：查詢是否為進程內首次載入（0 -> 1）。
  * `template <typename FuncT, typename... Args> bool initialize_once(std::string_view symbol, Args&&... args)`：首次載入單次初始化。
  * `std::shared_ptr<const void> create_lifetime_token() const noexcept`：建立純生命週期權杖（輕量保活）。
  * `void add_cleanup_hook(std::function<void()> hook)`：註冊同步收尾回呼（LIFO 順序執行）。
  * `bool register_shutdown_symbol(std::string_view symbol)`：註冊符號為同步收尾回呼。
  * `bool register_terminal_shutdown_symbol(std::string_view symbol)`：註冊符號為外掛終端收尾回呼（最後執行；若外掛回傳非零則拒絕卸載轉為常駐模式）。
  * `void add_async_cleanup_hook(AsyncCleanupHook hook)`：註冊非同步握手收尾回呼（主程式 0ms 立即返回，外掛善後完畢主動調用 `on_ready()` 喚醒物理卸載）。
  * `void add_post_unload_hook(std::function<void(std::string_view)> hook)`：註冊 DLL 物理卸載後全域通知。
  * `void enable_deferred_unload(bool enable = true)`：啟用非同步離棧延遲卸載防護（杜絕呼叫棧內自毀引發崩潰）。

* **類別 `ork::WeakDynamicLibrary`**：
  * `DynamicLibrary lock() const noexcept`：嘗試晉升為強引用。
  * `bool expired() const noexcept`：查詢動態庫是否已卸載。
  * `size_t use_count() const noexcept`：查詢存活強引用總數。

---

## 🌐 7. 全域 UTF-8 零拷貝輔助工具：`ork::utf8`
* **標頭檔**：`ourokore/base/utf8.hpp`
* **函式**：
  * `as_view(str)`：將各類字串零拷貝轉換為 `std::string_view`。
  * `to_string(str)`：統一轉換為 `std::string`。
  * `to_u8string(view)`：轉換為 C++20 原生 `std::u8string`。
  * `is_string_like_v<T>`：編譯期型別特徵萃取，判斷是否為字串型別。

---

## ⚙️ 8. 現代高效能雜湊工具模組：`ork::base::Hash`
* **標頭檔**：`ourokore/base/Hash.hpp`
* **演算法與運算子**：
  * `Fnv1a64(data)`：FNV-1a 64-bit 雜湊（全域 TypeID 與字串 ID 唯一標準）。
  * `Fnv1a32(data)`：FNV-1a 32-bit 雜湊。
  * `Crc32(data)`：CRC32 (IEEE 802.3) 校驗碼。
  * `MurmurHash3(data, seed)`：MurmurHash3 32-bit 高品質雜湊。
  * `HashCombine(seed, v1, v2, ...)`：變參組合雜湊。
  * 使用者自訂字面量（`using namespace ork::base::literals;`）：
    * `""_fnv64`：編譯期計算 64 位元常數。
    * `""_fnv32`：編譯期計算 32 位元常數。
    * `""_crc32`：編譯期計算 CRC32 校驗碼。

---

## 🌳 9. 樹狀結構節點與文字 DSL 串流：`ork::base::TreeNode<T>` / `ork::base::TreeIO`
* **標頭檔**：`ourokore/base/Tree.hpp`、`ourokore/base/TreeIO.hpp`
* **樣板基底 `TreeNodeBase<Derived>` 方法**：
  * `CreateRoot<SubT = D>(name, args...)` / `CreateArray<SubT = D>(name, args...)`：建立樹之根節點（支援 C++20 `std::derived_from<SubT, D>` 約束與轉發建構參數，直出 `std::shared_ptr<SubT>`）。
  * `MakeNode<SubT = D>(name, args...)`：底層工廠函式（支援衍生多型與建構鉤子）。
  * `PushElement<SubT = D>(args...)` / `PushElement(element)`：原地構造匿名元素或推入既有節點指標（$O(1)$）。
  * `ElementCount()` / `Size()` / `ChildCount()`：子元素數量查詢（$O(1)$）。
  * `GetElementAt(index)` / `operator[](size_t index)`：隨機下標存取（$O(1)$）。
  * `FindChildByName(name)` / `operator[](const std::u8string &name)`：名稱尋址（$O(1)$）。
  * `AddChild<SubT = D>(name, args...)` / `AddBackChild<SubT = D>(name, args...)`：新增具名或匿名子節點（直出強型別 `std::shared_ptr<SubT>`，零手動轉型）。
  * `InsertBefore<SubT = D>(child, name, args...)` / `InsertAfter<SubT = D>(child, name, args...)`：指定位置精準插入衍生節點。
  * `RemoveElementAt()` / `RemoveChild()` / `ClearChildren()`：子節點移除。
  * `Reversed()`：零拷貝反向走訪視圖糖衣。
  * `GetTreeMutex()`：取得樹級讀寫鎖（整棵樹共享同一個鎖）。
  * ⚠️ **高壓線禁忌**：走訪期間只能進行純資料讀取，**絕對禁止調用任何結構異動介面**（如 `AddChild`/`RemoveChild`），否則引發不可重入讀寫鎖重複加鎖死鎖！
  * `DetachFromParent()`：斷開父節點雙向弱關聯自立為新樹。
* **具體節點 `TreeNode<T>`（`StringTreeNode`）方法**：
  * `T GetData()` / `void SetData(const T &)` / `void SetData(T &&)`：資料鎖保護之存取。
* **文字 DSL 串流 `TreeIO`**：
  * `Serialize(ostream, root, ...)` / `SerializeCompact(...)` / `SerializeToString(...)`
  * `Deserialize(istream, ...)` / `DeserializeFromString(...)`
  * 模式列舉：`CompactMode::Pretty`（Allman 風格排版）/ `CompactMode::Compact`（緊湊模式，保留關鍵字等號 `=`）。

---

## ⚡ 10. 並行排程與同步設施：`ork::base::concurrency`
* **標頭檔**：
  * `<ourokore/base/FixedThreadPool.hpp>`
  * `<ourokore/base/DynamicThreadPool.hpp>`
  * `<ourokore/base/ThreadSafeQueue.hpp>`
  * `<ourokore/base/Semaphore.hpp>`
  * `<ourokore/base/Event.hpp>`
* **固定執行緒池 `ork::base::FixedThreadPool`**：
  * `explicit FixedThreadPool(size_t thread_count = hardware_concurrency())`
  * `template <typename F, typename... Args> auto Submit(F&&, Args&&...) -> std::future<...>`：非同步提交任務並取得 Future。
  * `void WaitForAll()`：同步阻塞等待當前佇列與執行中之任務全數完成。
  * `void Shutdown()`：優雅等待排隊任務完成後關閉執行緒池。
  * `size_t GetWorkerCount() const` / `size_t GetActiveCount() const` / `size_t GetPendingCount() const`
* **動態彈性伸縮池 `ork::base::DynamicThreadPool`**：
  * `DynamicThreadPool(min_threads, max_threads, idle_timeout)`
  * `Submit(F&&, Args&&...)`：自適應工作量自動擴充執行緒，閒置逾時自動縮容銷毀。
* **執行緒安全佇列 `ork::base::ThreadSafeQueue<T>`**：
  * `void Push(T item)` / `void Push(T&& item)`：執行緒安全寫入元素並喚醒等待者。
  * `bool TryPop(T &item)`：非阻塞嘗試取出元素（佇列為空時立即返回 false）。
  * `T WaitAndPop()` / `bool WaitAndPop(T &item, duration timeout)`：阻塞或限時等待取出元素。
  * `bool Empty() const` / `size_t Size() const` / `void Clear()`
* **計數信號量 `ork::base::Semaphore`**：
  * `explicit Semaphore(ptrdiff_t initial_count)`
  * `void Acquire()` / `void Release(ptrdiff_t update = 1)` / `bool TryAcquire()` / `bool TryAcquireFor(timeout)`
* **同步事件 `ork::base::Event`**：
  * `explicit Event(bool manual_reset = false, bool initially_signaled = false)`
  * `void Signal()` / `void Reset()` / `void Wait()` / `bool WaitFor(timeout)` / `bool IsSignaled() const`

---

## 🛡️ 11. 外掛 Heap 追蹤與記憶體防禦：`ork::base::HeapTracker` / `ork::PluginHeap`
* **標頭檔**：
  * `<ourokore/base/HeapTracker.hpp>`
  * `<ourokore/base/PluginHeap.hpp>`
  * `<ourokore/base/TrackedNewDelete.hpp>`
  * `<ourokore/base/heap_api.h>`
* **編譯期雙策略**：
  * `ORK_HEAP_POLICY_A`：Debug 詳細診斷 / Release 零開銷關閉（原生速度）。
  * `ORK_HEAP_POLICY_B`：Debug 詳細診斷 / Release 輕量無鎖原子計數（發布版仍可清空驗收）。
* **全域透明重載巨集**：
  * `ORK_ENABLE_PLUGIN_HEAP_TRACKING()`：一行透明攔截外掛模組內所有 `new`/`delete` 及 STL 容器配置。
* **顯式巨集與 STL 配置器**：
  * `ORK_NEW(Type, args...)` / `ORK_DELETE(ptr)`：編譯期自動捕捉 `__FILE__` 與 `__LINE__`。
  * `ORK_NEW_ARRAY(Type, count)` / `ORK_DELETE_ARRAY(ptr, count)`
  * `TrackedAllocator<T>`：相容 STL 容器之受管分配器。
* **清空判定與洩漏診斷**：
  * `bool PluginHeap::is_clean()`：查詢當前模組是否 100% 清空。
  * `std::string PluginHeap::dump_leaks_to_string(context_name)`：輸出格式化 UTF-8 洩漏診斷清單。
  * `void PluginHeap::assert_clean(context_name)`：未清空立即印出報告並拋出例外。
  * `PluginHeapGuard`：RAII 作用域洩漏檢測守衛。
* **純 C ABI 介面**：
  * `ork_heap_allocate(size, file, line)` / `ork_heap_deallocate(ptr)`
  * `ork_heap_is_clean()` / `ork_heap_dump_leaks(buf, len)` / `ork_heap_assert_clean(name)`

---

## 🌐 12. 底層純 C ABI（Cross-Language FFI）分類索引
所有底層操作保證跨 DLL 邊界零例外逃逸，完整清單與參數規格請參見《[03. 純 C ABI 規格與記憶體佈局規範](../technical/03_c_abi_and_memory.md)》：
* **類別 A：物件生命週期與工廠**（`ork_create_object`、`ork_retain_object`、`ork_release_object`、`ork_acquire_object_pointer`、`ork_release_object_pointer`）
* **類別 B：狀態查詢與圖拓撲**（`ork_is_alive`、`ork_get_storage_state`、`ork_get_type_id`、`ork_is_instance_of`、`ork_read_lock`、`ork_write_lock` 等）
* **類別 C：宿主全域特權**（`ork_host_initialize`、`ork_host_shutdown`、`ork_host_set_storage_driver`、`ork_host_trigger_dehydration_rescue` 等）
* **類別 D：外掛 Heap 記憶體檢查**（`ork_heap_allocate`、`ork_heap_deallocate`、`ork_heap_is_clean`、`ork_heap_dump_leaks`、`ork_heap_assert_clean`）
''', encoding="utf-8")

    print("✅ specs/manual/ 全套 10 份說明書手冊生成完畢（API Reference 永遠置於末位）！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_manual_specs(specs_dir)
