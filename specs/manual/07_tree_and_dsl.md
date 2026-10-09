# 07. 樹狀物件節點與文字 DSL 指南 (Tree & TreeIO)

本章節介紹 OuroKore 基礎工具庫（`ourokore_base`）中的現代高效能階層物件節點 `TreeNode<T>`（`TreeNodeBase<Derived>`）與文字 DSL 串流工具 `TreeIO`。

---

## 🧭 1. 設計哲學與心智模型

### 1.1 物件節點本體論（The Node IS The Object）── 擺脫「容器」思維
* **樹不是容器，節點自身即為物件本體**：
  在傳統思維中，許多開發者習慣將樹狀結構視為類似 `std::vector` 或 `std::map` 那樣被動裝載資料的「容器（Container）」。然而在 OuroKore 的現代設計中，**我們不要再稱呼 Tree 為容器，而要將其視為實體「物件節點（Object Node）」**。
* **CRTP 樹狀拓撲能力注入基底（Tree Topology Mixin）**：
  在 CRTP 架構下，衍生類別 `D` 自身就是具備實體記憶體佈局、成員變數與業務邏輯的 C++ 領域物件。`TreeNodeBase<D>` 是作為物件節點的「樹狀拓撲能力注入基底」，為領域物件賦予父子階層（Parent / Children）、具名索引（Name Map）、整樹讀寫鎖（Tree Shared Mutex）與防爆棧析構能力。
* **物件始終為核心主體**：
  整棵樹（Tree）本質上就是由一組具備拓撲關係的領域物件節點所構成的**階層體系（Hierarchy / Object Node Tree）**，不存在脫離物件之外的皮囊容器幻象。

### 1.2 異質物件節點階層（Heterogeneous Object Nodes Architecture）
* **原生支援異質物件節點**：
  OuroKore 的樹狀結構原生支援**異質物件（Heterogeneous Objects）**！
* **共通基底與多型階層**：
  透過定義共通的基底物件節點（例如 `class BaseEntity : public TreeNodeBase<BaseEntity>`），應用端可以衍生出各類不同結構、不同業務屬性的具體領域物件節點（如 `MonsterEntity`、`ItemEntity`、`LightSourceEntity`、`TriggerVolumeEntity` 等）。
* **同樹並存與統一排程**：
  在同一棵樹中、甚至同一個父物件節點的子節點序列裡，可以同時容納並管理這些完全不同的異質衍生型別。
* **C++20 `std::derived_from` 強型別零手動轉型直出（Zero-Casting）**：
  透過 C++20 Concept 約束，應用端呼叫 `AddChild<SubT>`、`PushElement<SubT>` 等樣板介面時，編譯期自動驗證型別繼承關係，並將建構子參數完美轉發至衍生類別，直接回傳 `std::shared_ptr<SubT>` 強型別智慧指標！呼叫端立即可存取衍生欄位，完全無需任何手動型別轉換（`dynamic_cast` / `static_cast`）。

### 1.3 徹底終結「陣列 vs 物件」假性劃分
不論是屬性目錄還是元素清單，在樹的本質上**全都統一為子物件節點（Child Nodes）**：
* 底層統一由連續向量（`std::vector`）保序管理，享有 CPU 快取極速連續讀取。
* 具名字節點由雜湊表提供 $O(1)$ 名稱尋址。
* 下標存取（`node[0]`）與鍵名存取（`node[u8"HP"]`）100% 互通。
* **極簡人體工學**：統一使用 `CreateRoot()` 作為唯一根節點入口，由父節點 `PushElement()` 原地追加匿名元素節點，查詢統一使用 `ChildCount()` 或 `Size()`。

### 1.4 節點三維度正交模型
每個物件節點均具備 3 個獨立維度（可任意組合）：
1. **名稱（Name）**：具名節點（`[Name]`）或 匿名節點。
2. **資料（Data / Value）**：帶有本體資料（`"Data"`）或 無資料。
3. **子節點（Children）**：擁有子節點區塊（`{ ... }`）或 葉節點。

### 1.5 百萬層深樹顯式堆疊迭代防爆棧析構與反序列化
* **析構防爆棧**：內建顯式堆疊展平析構機制，巨型深樹解構時將遞迴展開為堆積迴圈以 $O(1)$ 呼叫深度安全釋放，杜絕 Stack Overflow。
* **反序列化防爆棧**：狀態機由 Heap 上的顯式堆疊 `std::vector<ParseFrame>` 驅動，呼叫棧深度恆為 $O(1)$。

---

## 📝 2. 文字 DSL 語法與界定符

OuroKore 文字 DSL 語法規則極致精簡、自洽且無歧義：

| 語法 Token | 角色 | 語意說明 |
| :--- | :--- | :--- |
| `[` ... `]` | 節點名稱標識 | 定義具名物件節點標記，跳脫字元支援 `\]` 與 `\\` |
| **`[]`** | **顯式匿名容器標頭** | **定義沒有名稱之容器物件節點標頭（Allman 排版獨立一行）** |
| **`=`** | **賦值關鍵字** | **將後續引號內容賦值予該節點（具名賦值或匿名容器賦值必然使用）** |
| `"` ... `"` | 節點資料內容 | 原始位元組直接傳遞（0~255 二進位安全），跳脫字元支援 `\\` 與 `\"` |
| `{` ... `}` | 子節點區塊 | 進入 / 退出子節點層級（全面統一為大括號，Allman 風格獨立換行） |

### 賦值運算子 `=`、標籤與匿名節點形態規則
* **具名賦值**：`[HP] = "100"`（必須有 `=`）。
* **純標籤（無值節點）**：`[IsAdmin]`（無等號）。
* **顯式純匿名容器**：`[]` 換行 `{ ... }`（Allman 排版），緊湊模式為 `[]{...}`。
* **顯式帶值匿名容器**：`[] = "Data"` 換行 `{ ... }`，緊湊模式為 `[]="Data"{...}`。
* **匿名純字串葉節點**：直接以引號表示 `"草藥"`。
* **無名無值無子（空葉節點）**：以空字串引號表示 `""`。

#### 匿名節點四種形態 DSL 表現對照：
| 節點狀態 | 名稱（Name） | 資料（Data） | 子節點（Children） | DSL 表現型態 | 範例 |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **空葉節點** | ❌ | ❌ | ❌ | **`""`**（空字串引號） | `""` |
| **純值葉節點** | ❌ | ✔️ | ❌ | **`"資料"`** | `"Hello"` |
| **純匿名容器** | ❌ | ❌ | ✔️ | **`[]\n{\n...\n}`** | `[] { "A" "B" }` |
| **帶值匿名容器** | ❌ | ✔️ | ✔️ | **`[] = "資料"\n{\n...\n}`** | `[] = "Data" { "A" }` |

### 語法消歧義保證 (Disambiguation Guarantee)
當同層出現平級的「純字串葉節點」緊接「獨立匿名容器」時：
```dsl
[]
{
  "ItemData"
  []
  {
    "Sub1"
    "Sub2"
  }
}
```
* **語法界線分明**：`"ItemData"` 為獨立字串葉節點，絕不貪婪搶佔後續容器；`[]` 作為下一個平級容器的顯式標頭，兩者互為兄弟，結構清晰工整。
* **容器標頭鐵律（Header Invariant & Fail-Fast）**：只要是容器（含有子節點），**都必須由具名標頭 [Name] 或匿名標頭 [] 帶頭**（大括號 { ... } 絕不可作為無標頭的孤立實體出現）。若文本中出現任何未帶標頭的裸大括號（包含頂層或任何子階層），剖析器將判定為格式毀損並終止解析（回傳 
ullptr）。
ullptr）。
* **無歧義保證**：當出現 `[IsAdmin]` 後緊接 `"草藥"`，狀態機能 100% 確定 `IsAdmin` 為無值標籤完成，而 `"草藥"` 為下一個獨立的匿名子節點！

### 註解語法原生支援
* **`//` 單行註解**：忽略至行尾。
* **`/* ... */` 區塊註解**：忽略至閉合符號 `*/`。
* **`#` 腳本風格單行註解**：忽略至行尾。
* 狀態機在關鍵標記以外的地方寬容無視所有雜訊；若欲加入說明文字，強烈建議使用註解符號避免干擾。


---

## 💻 3. 基礎物件節點使用範例

```cpp
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using ork::base::Tree; // 即 ork::base::StringTreeNode
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根物件節點
auto player = Tree::CreateRoot(u8"Player");
player->SetData("英雄角色");

// 2. 建立具名屬性子節點
auto hp = player->AddChild(u8"HP");
hp->SetData("100");

// 3. 建立純標籤節點
player->AddChild(u8"IsActive");

// 4. 建立匿名子節點序列 (PushElement 原地構造匿名物件節點)
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

## 🧬 6. 異質物件節點與多型衍生階層實戰 (Heterogeneous Object Nodes)

在現代應用程式與遊戲架構中，樹狀層次往往需要承載不同類型的業務物件。OuroKore 的 `TreeNodeBase<D>` 透過 CRTP 與 C++20 模板特性，提供了一流的**異質物件階層支援**。

### 6.1 CRTP 領域基底節點設計
應用端首先定義一個繼承自 `TreeNodeBase<BaseNode>` 的共通多型基底節點：

```cpp
#include <ourokore/base/Tree.hpp>

class BaseEntity : public ork::base::TreeNodeBase<BaseEntity> {
public:
    using Base = ork::base::TreeNodeBase<BaseEntity>;

    virtual ~BaseEntity() = default;
    virtual std::string GetCategory() const { return "BaseEntity"; }

protected:
    // 基底建構子宣告為 protected，僅供衍生類別於初始化時調用
    explicit BaseEntity(std::u8string name = u8"")
        : Base(std::move(name)) {}

    template <typename D> friend class ork::base::TreeNodeBase;
};
```

> [!IMPORTANT]
> **衍生類別職責邊界與嚴格私有封裝（Encapsulation Invariant）**：
> 1. **延伸類別專注於資料處理**：`TreeNodeBase` 的延伸類別職責為封裝業務資料與領域行為，**絕不負責底層節點與拓撲結構的直接操作**。
> 2. **拓撲狀態全面私有化（`private`）**：所有底層成員變數（`m_elements`、`m_nameMap`、`m_parent`、`m_self`、`m_treeMutex` 等）與內部同步方法（`PropagateTreeMutex`、`SetParentAndSelf` 等）均為 `private`，嚴格禁止衍生類別直接碰觸，杜絕繞過讀寫鎖直接改動結構導致的資料競爭與索引損壞。
> 3. **節點操作一律透過公開 API**：若衍生類別業務方法需要存取或操作子節點，**一律使用公開的操作功能**（如 `AddChild()`、`PushElement()`、`operator[]`、`GetName()`、`GetParent()`、`GetTreeMutex()` 等），享有整樹讀寫鎖與快取索引的完整保護。
> 4. **受保護基底建構子（`protected`）**：`explicit TreeNodeBase(name)` 宣告為 `protected`，僅供衍生類別在自身建構子中調用初始化；外部禁止直接實例化裸 `TreeNodeBase<D>`，確保必須透過衍生類別或 `CreateRoot()` 建立實體。

### 6.2 異質衍生領域物件節點
基於此共通基底，應用端可派生出不同型別、攜帶不同屬性與業務邏輯的具體物件節點：

```cpp
// 怪物實體物件節點
class MonsterEntity : public BaseEntity {
public:
    int hp{100};
    int atk{20};

    MonsterEntity(std::u8string name, int in_hp, int in_atk)
        : BaseEntity(std::move(name)), hp(in_hp), atk(in_atk) {}

    std::string GetCategory() const override { return "Monster"; }
};

// 道具實體物件節點
class ItemEntity : public BaseEntity {
public:
    int price{0};

    explicit ItemEntity(int in_price)
        : BaseEntity(u8""), price(in_price) {}

    std::string GetCategory() const override { return "Item"; }
};
```

### 6.3 強型別零手動轉型直出（Zero-Casting via C++20 `std::derived_from`）
在樹狀物件節點中，所有新增與插入介面（`CreateRoot`、`AddChild`、`PrependChild`、`PushElement`、`InsertBefore`、`InsertAfter`）均為 C++20 樣板方法：
* **嚴格編譯期約束**：`template <typename SubT = D, typename... Args> requires std::derived_from<SubT, D>`，非衍生類別於編譯期嚴格報錯。
* **完美轉發建構子參數**：無論建構子是 `(name, args...)` 還是自訂 `(args...)`，均自動推導並就地構造。
* **強型別直出**：直接回傳 `std::shared_ptr<SubT>`，呼叫端**無需型別轉換（Zero-Casting）**即可直接讀寫衍生欄位！

```cpp
// 1. 建立根物件節點（可為基底或具體衍生型別）
auto dungeon = BaseEntity::CreateRoot(u8"Dungeon");

// 2. 新增具名異質物件節點 (回傳 std::shared_ptr<MonsterEntity>)
std::shared_ptr<MonsterEntity> boss = dungeon->AddChild<MonsterEntity>(u8"BossDragon", 5000, 350);
boss->hp -= 200; // 直接操作衍生屬性，無需 dynamic_cast！

// 3. 原地構造並推入匿名異質物件節點 (回傳 std::shared_ptr<ItemEntity>)
std::shared_ptr<ItemEntity> potion = dungeon->PushElement<ItemEntity>(50);
potion->price = 45; // 直接存取道具屬性！

// 4. 精準指定位置插入異質衍生節點
std::shared_ptr<MonsterEntity> minion = dungeon->InsertBefore<MonsterEntity>(boss, u8"Goblin", 100, 15);
std::shared_ptr<ItemEntity> sword = dungeon->InsertAfter<ItemEntity>(potion, u8"Excalibur", 9999);

// 5. 驗證同樹異質並存性
assert(dungeon->ChildCount() == 4);
assert((*dungeon)[0] == minion);
assert((*dungeon)[1] == boss);
assert((*dungeon)[2] == potion);
assert((*dungeon)[3] == sword);

// 6. 多型走訪與型別識別
for (const auto &child : *dungeon) {
    if (child) {
        std::cout << "節點名稱: " << ork::utf8::to_string(child->GetName())
                  << ", 類別: " << child->GetCategory() << std::endl;
    }
}
```

### 6.4 外部工廠反序列化與 TreeIO 特權拓撲掛載（支援異質樹動態還原）
文字 DSL 是純字串串流，如何還原出具體的異質物件節點？`TreeIO` 支援「反轉控制（Inversion of Control）」外部工廠模式：
* **反序列化不預先構造節點**：解析時不盲目實例化預設節點，而是將節點名稱與字串內容送給應用端註冊的工廠：`factory(const std::u8string &name, const std::string &data) -> std::shared_ptr<BaseEntity>`。
* **內部特權拓撲掛載（`AttachChild`）**：由工廠構造完成的異質物件實體，由 `TreeIO` 透過內部特權（`AttachChild` 為 `private`，宣告 `friend class TreeIO;`）安全掛載至父節點。一般外部程式碼無法直接調用 `AttachChild`，確保平時所有節點一律嚴格由父節點原地延伸構造。
* **嚴格資料毀損中斷（Fail-Fast / All-or-Nothing）**：
  1. 若工廠無法識別資料（回傳 `nullptr`），視為資料毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  2. 若回傳物件與同層名稱重複，同樣視為毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  3. 若節點名稱包含非法或非 UTF-8 位元組序列（依據 RFC 3629），視為資料毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  4. 若容器缺失標頭（任何未帶具名 [Name] 或匿名 [] 標頭的孤立裸大括號 { ... }），視為語法殘缺與資料毀損，**立即中止全體解析並向呼叫端回傳 
ullptr**。

```cpp
auto hetero_factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<BaseEntity> {
    if (data.rfind("MONSTER:", 0) == 0) {
        // 解析資料並動態實例化 MonsterEntity
        return BaseEntity::CreateRoot<MonsterEntity>(name, 200, 30);
    }
    if (data.rfind("ITEM:", 0) == 0) {
        // 解析資料並動態實例化 ItemEntity
        return BaseEntity::CreateRoot<ItemEntity>(100);
    }
    return nullptr; // 無法識別之無效資料 -> 視為資料毀損，中止全體解析！
};

// 若字串合法且無同層同名衝突，回傳完整異質物件樹；若資料損壞或衝突，回傳 nullptr
auto restored_scene = TreeIO::DeserializeFromString<BaseEntity>(dsl_text, hetero_factory);
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
