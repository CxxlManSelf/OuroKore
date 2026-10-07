# -*- coding: utf-8 -*-
"""
生成與同步應用端專用 AI 技能手冊 (skills/ourokore-app/SKILL.md)
"""
from pathlib import Path

def generate_app_skill(root_dir: Path):
    app_skill_dir = root_dir / "skills" / "ourokore-app"
    app_skill_dir.mkdir(parents=True, exist_ok=True)
    skill_file = app_skill_dir / "SKILL.md"

    content = '''---
name: ourokore-app
description: "OuroKore 應用端與第三方外掛開發指南、領域物件設計、記憶體 Heap 追蹤清空檢驗與生命週期安全錨定最佳實踐。"
---

# OuroKore 應用端開發與外掛架構技能手冊 (OuroKore App Developer Skill)

本技能為 AI 助理與應用端/外掛開發者提供對 **OuroKore** 核心框架的使用準則、領域物件設計模式、生命週期管理以及記憶體安全檢驗指南。

---

## 🧭 1. 核心開發心智模型

1. **零裸指標與 ControlBlock 託管**：
   - 領域物件嚴格繼承 `ork::Subclass<T, Base = ork::OuroObject>`，嚴禁直接裸 new。
   - 物件生命週期由全域唯一 `HandleID` 與 ControlBlock 託管。
   - 拓撲持有：雙向關聯 100% 使用 `OwningHandle<T>`，背景 `CycleCollector` 自動消化破環。
   - 旁路觀察：外掛隨時卸載防卡死使用 `UnboundHandle<T>`，絕不釘死對方。
   - 棧上呼叫：一律使用 `OuroPtr<T>` 根引用守衛，透過成員指標存取，杜絕指標逃逸。
2. **自動脫水與透明復水**：
   - 物件可在記憶體壓力下自動脫水換頁落盤；再次存取時透明復水，純 ControlBlock 查詢（`Is<T>()`、`IsAlive()`）零 I/O。

---

## 🛠️ 2. 外掛 Heap 取代與卸載前清空檢驗 (Plugin Heap Tracking)

為保證外掛（Plugin / MODULE）在結束卸載前無任何記憶體洩漏，OuroKore 在 `ourokore_base` 提供雙軌並行的 Heap 追蹤與清空檢驗機制，並支援在編譯期選擇 **方案 A** 或 **方案 B**。

### 2.1 編譯期方案選擇 (Compile-Time Policy)

| 方案 | 標識巨集 | Debug 模式行為 | Release 模式行為 | 核心目標 |
| :--- | :--- | :--- | :--- | :--- |
| **方案 A (預設)** | `ORK_HEAP_POLICY_A` | 詳細診斷 (記錄檔名/行號/序號) | 完全關閉 (零開銷直通系統 malloc) | 追求發布版極致原生速度 |
| **方案 B** | `ORK_HEAP_POLICY_B` | 詳細診斷 (記錄檔名/行號/序號) | 輕量原子無鎖計數 (`std::atomic<size_t>`) | 發布版仍需驗收 Heap 是否清空 |

#### CMake 編譯指定方式：
```bash
# 選擇方案 A (Debug 詳細 / Release 零開銷關閉)
cmake -B build -DOUROKORE_HEAP_POLICY=A

# 選擇方案 B (Debug 詳細 / Release 輕量無鎖原子計數)
cmake -B build -DOUROKORE_HEAP_POLICY=B
```

亦可於原始碼中手動宣告：
```cpp
#define ORK_HEAP_POLICY ORK_HEAP_POLICY_A // 或 ORK_HEAP_POLICY_B
#include <ourokore/base/HeapTracker.hpp>
```

### 2.2 全域透明運算子重載 (Global Overload Mode)

在外掛 MODULE 動態庫的任一主實作檔（如 `PluginMain.cpp`）中宣告：
```cpp
#include <ourokore/base/PluginHeap.hpp>

// 一行啟動該外掛模組全域 operator new/delete/new[]/delete[] 重載
ORK_ENABLE_PLUGIN_HEAP_TRACKING()
```
* **效果**：該外掛模組內部所有的 `new`、`delete` 以及 STL 容器（如 `std::vector`、`std::string` 等）之堆配置全部透明導向受管追蹤，業務程式碼無需修改任何一行。

### 2.3 顯式受管 new / delete 巨集 (Explicit Tracked Mode)

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

### 2.4 外掛結束前清空判定與洩漏診斷 (Zero-Leak Verification)

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

## 📦 3. 定義受管物件與藍圖序列化

```cpp
#include <ourokore/component/OuroCore.hpp>

class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    ork::OwningHandle<Monster>          m_minion{"MinionSlot"};
    ork::UnboundHandle<ork::OuroObject> m_plugin_module;

    Monster() = default;
    explicit Monster(int32_t hp) : m_hp(hp) {}

    int32_t GetHp() const { ork::OuroReadLock lock(*this); return m_hp; }
    void SetHp(int32_t hp) { ork::OuroWriteLock lock(*this); m_hp = hp; }

    void SerializePayload(ork::OuroStream &stream) const override {
        stream.WriteProperty("hp", m_hp);
    }

    void DeserializePayload(ork::OuroStream &stream) override {
        stream.ReadProperty("hp", m_hp);
    }

private:
    int32_t m_hp{100};
};
```

---

## 🔌 4. 動態庫生命週期反向錨定 (DynamicLibrary Auto-Unload)

1. **外掛 Target 宣告鐵律**：CMake 中必須使用 `add_library(<name> MODULE ...)`，嚴禁宣告為 `SHARED`。
2. **生命週期反向錨定**：透過 `lib.bind_lifecycle(raw_ptr, deleter_fn)` 或自訂 Deleter 閉包持有 `DynamicLibrary`。
3. **主程式放棄 initial handle**：主程式完成工廠建構後必須放棄 `load()` 回傳的 `DynamicLibrary` 句柄（隨作用域結束或呼叫 `reset()`），使得 DLL 的存活權杖全權交給產生的物件持有；最後一個物件析構時動態庫自動安全卸載。

---

## 🌳 5. 樹狀物件節點與異質多型衍生階層 (Tree & TreeIO)

> 💡 **核心心智模型**：樹並非被動裝載資料的容器，節點自身即為物件本體（The Node IS The Object），天然支援異質物件階層。

在 `<ourokore/base/Tree.hpp>` 與 `<ourokore/base/TreeIO.hpp>` 中，提供了現代樹狀物件節點 `TreeNodeBase<D>` / `TreeNode<T>` 與文字 DSL 工具：

### 5.1 CRTP 領域節點與多型衍生階層 (C++20 std::derived_from)
```cpp
#include <ourokore/base/Tree.hpp>

class BaseEntity : public ork::base::TreeNodeBase<BaseEntity> {
public:
    virtual ~BaseEntity() = default;
    virtual std::string GetType() const { return "BaseEntity"; }
protected:
    explicit BaseEntity(std::u8string name = u8"") : TreeNodeBase(std::move(name)) {}
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

// 1. 建立根節點
auto root = BaseEntity::CreateRoot(u8"Scene");

// 2. 零轉型直接新增強型別衍生節點 (C++20 std::derived_from 約束，直出 std::shared_ptr<SubT>)
std::shared_ptr<MonsterEntity> boss = root->AddChild<MonsterEntity>(u8"BossDragon", 5000, 350);
boss->hp -= 100; // 直接存取衍生屬性，無需 dynamic_cast！

// 3. 原地構造並推入陣列元素
std::shared_ptr<MonsterEntity> minion = root->PushElement<MonsterEntity>(u8"Goblin", 100, 15);

// 4. 開頭插入衍生子節點 (PrependChild)
std::shared_ptr<MonsterEntity> guard = root->PrependChild<MonsterEntity>(u8"Guard", 1200, 80);
```

### 5.2 外部工廠反序列化與 TreeIO 特權拓撲掛載 (Factory-First Deserialization)
為支援異質樹（Heterogeneous Tree）與嚴格物件構造不變量，`TreeIO` 支援「反轉控制（Inversion of Control）」：反序列化時**不預先製造 node**，僅將字串內容（與節點名稱）送至外部工廠，成型後由 `TreeIO` 透過內部特權（`AttachChild`）掛載至父節點：
* **工廠簽名**：`factory(const std::u8string &name, const std::string &data) -> NodePtr` 或 `factory(const std::string &data) -> NodePtr`。
* **資料毀損嚴格中止 (Fail-Fast)**：
  1. 若工廠造不出物件（回傳 `nullptr`），視為資料毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  2. 若回傳物件掛載時發現名稱與同層具名節點重複，同樣視為毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。

```cpp
auto factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<BaseEntity> {
    if (data == "MONSTER") return BaseEntity::CreateRoot<MonsterEntity>(name, 100, 20);
    if (data == "ITEM")    return BaseEntity::CreateRoot<ItemEntity>(50);
    return nullptr; // 無法識別之無效型別 -> 視為資料毀損，中止全體解析！
};

// 若字串合法且無同層同名衝突，回傳完整樹；若資料損壞或衝突，回傳 nullptr
auto scene = TreeIO::DeserializeFromString<BaseEntity>(dsl_text, factory);
```

### 5.3 文字 DSL 序列化與輸出模式 (CompactMode)
支援顯式堆疊走訪輸出文字 DSL，提供兩種輸出模式：
* `CompactMode::Pretty`：格式化排版模式（Allman 風格：獨立換行與縮排，具名賦值使用 ` = `）。
* `CompactMode::Compact`：緊湊模式（無縮排與換行，具名賦值保留關鍵字 `=`）。

```cpp
// 1. 格式化排版輸出
TreeIO::Serialize(std::cout, root, CompactMode::Pretty);

// 2. 緊湊模式字串匯出
std::string compact_dsl = TreeIO::SerializeToString(root, CompactMode::Compact);
```

### 5.4 整樹走訪黃金法則（死鎖防禦）
* ⚠️ **高壓線禁忌**：整棵樹共享同一個 `std::shared_mutex`（不可重入）。在持讀鎖走訪期間（`for (auto &child : *node)`），**絕對嚴禁調用 `AddChild`、`PrependChild`、`PushElement`、`RemoveChild` 等異動結構介面**，否則立即引發不可重入死鎖！
* 異動需求請遵循「第一階段持讀鎖收集目標 -> 釋放讀鎖 -> 第二階段持寫鎖批次修改」之安全範式。

### 5.5 節點清理狀態驗證 (TreeCleanupTracker)
* 欲檢查「整棵樹的所有節點是否均已全數清除釋放」，透過 `root->GetCleanupTracker()` 取得輕量弱引用共享鎖追蹤器：
* `tracker.AreAllNodesCleanedUp()` / `tracker.IsCleanedUp()`：以 $O(1)$ 常數時間精準判定整棵樹是否已全數銷毀（鎖已 expired 即代表所有持有該鎖的節點均已析構），零遍歷開銷。
'''
    skill_file.write_text(content, encoding="utf-8")
    print("✅ 應用端 AI 技能檔 ../skills/ourokore-app/SKILL.md 生成完畢！")

if __name__ == "__main__":
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent.parent
    generate_app_skill(root_dir)
