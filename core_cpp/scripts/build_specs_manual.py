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
  * `UnboundHandle<T>`：無繫結句柄，不佔用物件圖入邊（In-degree = 0），專為**動態模組/DLL 插件非同步熱卸載防釘死、生命週期解耦與旁路觀察**設計。支援安全原子提升（`LockAndAcquire()`），具備惰性修剪（Lazy Pruning）機制，徹底杜絕野指標與 UAF。
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

所有託管物件必須繼承自 `ork::OuroObject`，禁止外部直接 `new`：

```cpp
#include <ourokore/component/OuroCore.hpp>
#include <string>

class Player : public ork::OuroObject {
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
player->SetName("亞瑟王");
player->AddScore(100);

// 2. 存檔至儲存驅動
bool save_ok = ork::Save(player);
assert(save_ok == true);

// 3. 取得物件全域唯一識別碼
ork::HandleID pid = player.GetTargetID();
std::cout << "玩家建立成功，HandleID: " << pid << std::endl;
```
''', encoding="utf-8")

    # 03_domain_object_design.md
    (manual_dir / "03_domain_object_design.md").write_text('''# 03. 領域物件設計規範 (Domain Object Design)

本章節介紹如何遵循 OuroKore 規範設計高效能、多執行緒安全的領域模型物件。

---

## 🔒 1. 執行緒安全與自動 Dirty 標記（重要！）

在多執行緒併發環境下，手動維護「物件是否被修改（Dirty 狀態）」非常容易遺漏或產生 Data Race。OuroKore 採用 **RAII 獨占寫鎖與原子標髒** 的一體化設計：

```cpp
class Character : public ork::OuroObject {
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

在傳統 `std::shared_ptr` 下，雙向互指會造成嚴重的記憶體洩漏；但在 OuroKore 中：
* **業務圖內部雙向互指請直接使用 `OwningHandle`！**
* 當整個互指圖的外部根引用（`OuroPtr`）皆消失時，底層 `CycleCollector` 會在背景非同步偵測到循環孤島，自動執行外科手術斷鏈並安全回收，開發者無須承擔心理負擔。

---

## 3. `UnboundHandle<T>`：解耦弱引用與非同步熱卸載防釘死保護

若物件需要關聯一個「隨時可能被卸載、摧毀或替換」的外部服務或外掛模組，使用 `UnboundHandle`：

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

## ⚙️ 3. 配置儲存驅動與 LRU 自動脫水器

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
host->SetAutoDehydrator(lru);
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
host->FlushStorage();

// 2. 延遲銷毀排空：等待延遲隊列清空
host->FlushDeferredDeletions();

// 3. 即時循環回收：強制觸發一輪循環孤島偵測
host->CollectCycles();

// 4. 設定延遲銷毀模式（sync: 同步即時；async: 背景平行）
host->SetDeferredDeleteMode(false);

// 5. 調度緊急記憶體自救脫水
size_t freed = host->TriggerDehydrationRescue(1024 * 1024); // 嘗試騰出 1MB

// 6. 優雅終止核心（解構時亦會自動執行）
host->Shutdown();
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

---

## 📦 2. 領域物件基底：`ork::OuroObject`
* **標頭檔**：`ourokore/component/OuroObject.hpp`
* **方法**：
  * `HandleID GetObjectID() const`：取得物件之全域唯一識別碼。
  * `StorageState GetStorageState() const`：取得物件當前儲存狀態（Clean/Dirty/Dehydrated/UnsavedNew）。
  * `virtual void SerializePayload(OuroStream &stream) const`：純資料屬性序列化介面。
  * `virtual void DeserializePayload(OuroStream &stream)`：純資料屬性反序列化介面。

---

## 🔗 3. 智慧 Handle 系統
* **標頭檔**：`ourokore/component/Handles.hpp`
* **類別**：
  * `OwningHandle<T>`：強持有槽位，宣告為物件成員。方法：`Set()`, `Get()`, `Release()`, `GetTargetID()`。
  * `OwningContainerHandle`：動態強持有容器，方法：`AddTarget()`, `RemoveTarget()`, `GetTargetIDs()`。
  * `UnboundHandle<T>`：無繫結非擁有型引用，方法：`LockAndAcquire()`, `GetTargetID()`, `IsAlive()`, `Release()`。
  * `OuroPtr<T>`：棧上活躍根指標守衛，支援 `operator->`, `operator*`, `GetTargetID()`, `Release()`。

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
  * `Dehydrate(id)` / `Rehydrate<T>(id)`：手動脫水與復水。
  * `SaveBatch(...)` / `LoadBatch(...)`：多核心平行批次操作。
''', encoding="utf-8")
    print("✅ specs/manual/ 全套 7 份說明書手冊生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_manual_specs(specs_dir)
