# 07. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)

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
  - **極簡人體工學**：統一使用 `CreateRoot()` 作為唯一根節點入口，由父節點 `PushElement()` 原地追加匿名元素，查詢統一使用 `ChildCount()` 或 `Size()`。

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
assert(inventory->ChildCount() == 2);
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
當領域節點存在進一步繼承階層（例如 `BaseEntity` 衍生出 `MonsterEntity`、`ItemEntity`）時，`TreeNodeBase<D>` 為所有新增與工廠介面（`CreateRoot`、`AddChild`、`PrependChild`、`PushElement`、`InsertBefore`、`InsertAfter`）提供了現代化 C++20 模板多載：
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

### 6.3 外部工廠反序列化與 TreeIO 特權拓撲掛載（支援異質樹與資料毀損判定）
為支援多型異質樹（Heterogeneous Tree）與嚴格物件構造不變量，`TreeIO` 支援「反轉控制（Inversion of Control）」：
* **反序列化不預先構造節點**：解析時不盲目實例化預設節點，而是將字串內容（與節點名稱）送給外部工廠：`factory(const std::u8string &name, const std::string &data) -> NodePtr` 或 `factory(const std::string &data) -> NodePtr`。
* **內部特權拓撲掛載（`AttachChild`）**：由外部工廠構造完成之物件由 `TreeIO` 透過內部特權（`AttachChild` 為 `private`，宣告 `friend class TreeIO;`）掛載至父節點。若回傳物件名稱與父節點同層既有具名節點重複，或傳入空指針，`AttachChild` 拒絕掛載並立即回傳 `false`。一般外部程式碼無法直接調用 `AttachChild`，確保平時所有節點一律嚴格由父節點原地延伸構造。
* **嚴格資料毀損中斷（Fail-Fast / All-or-Nothing）**：
  1. 若工廠造不出物件（回傳 `nullptr`），視為資料毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  2. 若回傳物件與同層名稱重複，同樣視為毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。

```cpp
auto hetero_factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<BaseEntity> {
    if (data == "MONSTER") return BaseEntity::CreateRoot<MonsterEntity>(name, 200, 30);
    if (data == "ITEM")    return BaseEntity::CreateRoot<ItemEntity>(100);
    return nullptr; // 無法識別之無效資料 -> 視為資料毀損，中止全體解析！
};

// 若字串合法且無同層同名衝突，回傳完整樹；若資料損壞或衝突，回傳 nullptr
auto scene = TreeIO::DeserializeFromString<BaseEntity>(dsl_text, hetero_factory);
```

---

## 🔒 7. 整樹走訪安全範式與死鎖防禦指南 (Tree Traversal & Deadlock Prevention)

OuroKore 的樹狀結構採用**「整棵樹（Root 與所有子孫節點）共享同一個讀寫鎖（`std::shared_mutex`）」**之架構，確保跨節點操作之原子性與跨樹獨立性。

由於 `std::shared_mutex` 為**不可重入鎖（Non-recursive Mutex）**，在進行整樹或子樹遍歷時，必須誓死遵守以下黃金法則：

### ⚠️ 高壓線禁忌：走訪期間「只能做資料存取，絕不能操作節點拓撲」

> [!CAUTION]
> **嚴禁在持讀鎖走訪期間調用節點拓撲修改介面！**
> 在持共享讀鎖（`std::shared_lock`）的保護區塊內，若調用 `AddChild()`、`PrependChild()`、`RemoveChild()`、`PushElement()`、`ClearChildren()` 等會索取獨占寫鎖（`std::unique_lock`）的函式，**當前執行緒會立即引發不可重入的重複鎖死鎖（Deadlock）！**

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

## 🧹 8. 節點清理狀態檢查與 TreeCleanupTracker (O(1) 弱引用共享鎖)

在複雜樹狀結構或外掛生命週期管理中，使用者經常需要確認「整棵樹的所有節點是否均已全數清除釋放（包含子節點無懸空與記憶體洩漏）」。

### 8.1 核心機制：弱引用樹級共享鎖 (Zero-Traversal O(1) Check)
* **共享鎖特性**：整棵樹（Root 與所有子孫節點）共享同一個讀寫鎖 `m_treeMutex`（`std::shared_ptr<std::shared_mutex>`）。
* **極致輕量**：`TreeCleanupTracker` 僅弱引用該共享鎖（`std::weak_ptr<std::shared_mutex>`），完全不增加節點引用計數，亦不干預或阻礙節點的正常解構釋放。
* **O(1) 判定**：當且僅當整棵樹的所有節點均已析構銷毀時，該共享鎖才會隨之解構銷毀（`expired`）。呼叫端無須遍歷整棵樹收集成千上萬個節點的 weak_ptr，直接以 $O(1)$ 時間判定整棵樹所有節點是否全數清除乾淨！

```cpp
#include <ourokore/base/Tree.hpp>

// 1. 建立根節點並取得清理追蹤器
TreeCleanupTracker tracker;
{
    auto root = StringTreeNode::CreateRoot(u8"Scene");
    tracker = root->GetCleanupTracker();

    assert(tracker.IsAlive()); // 樹與節點存活中
    assert(!tracker.AreAllNodesCleanedUp());

    // 新增深層子節點
    auto child = root->AddChild(u8"Child");
    child->AddChild(u8"SubChild");

    // 亦支援子節點斷開自立新樹 (DetachFromParent)
    // 斷開之子節點將分配專屬共享鎖自立門戶，不影響原樹之追蹤
} // 離開作用域，root 與所有子孫節點均已安全析構

// 2. 驗證所有節點均已全數清除釋放
assert(tracker.AreAllNodesCleanedUp()); // true！所有節點均已清除
assert(tracker.IsCleanedUp());          // true！
assert(!tracker.IsAlive());             // false！
```
