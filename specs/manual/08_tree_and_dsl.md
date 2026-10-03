# 08. 樹狀結構容器與文字 DSL 指南 (Tree & TreeIO)

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

4. **百萬層深樹非同步防爆棧析構（Async Stack-Overflow Defense）**：
   - 內建 `AsyncNodeDeletor`，巨型深樹解構時由非同步隊列安全排空釋放，徹底杜絕遞迴析構引發呼叫堆疊溢位（Stack Overflow）。

---

## 📝 2. 文字 DSL 語法與界定符

OuroKore 文字 DSL 採用四個互不干擾的正交界定符：
* `[名稱]`：節點名稱標記。
* `"資料"`：節點資料內容（支援 0~255 二進位位元組與完整脫字元轉義 `\"`、`\\`、`\n`、`\xHH`）。
* `{物件}`：具名子節點群集（大括號）。
* `(陣列)`：陣列元素清單（小括號）。

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

using ork::base::StringTreeNode;
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根節點
auto player = StringTreeNode::CreateRoot(u8"Player");
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
