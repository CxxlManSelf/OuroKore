# 02. 二進位串流與藍圖打包協議 (Binary Protocols & Wire Format RFC)

本文件詳細定義 OuroKore 的**實體二進位資料串流佈局（Binary Wire Format）**。
任何第三方工具、其他程式語言（C#、Rust、Go、Python）實現的 OuroKore 引擎或解析器，只要嚴格遵循本規格，即可與 C++ 實現版本產生的二進位藍圖或脫水存檔 **100% 互通與雙向讀寫**。

---

## 📐 1. 基礎編碼規範與字節序鐵律 (Endianness Invariant)

1. **字節序 (Endianness)**：
   - 全系統二進位資料流中的所有多位元組整數與浮點數，**一律強制採用 Little-Endian（小端序）**。
   - 若在 Big-Endian 平台上讀寫，實作層必須主動執行位元組反轉轉換。
2. **基元資料型別大小與編碼表**：

| 型別名稱 | 二進位大小 | 編碼與佈局規範 |
| :--- | :--- | :--- |
| `Boolean` | 1 Byte | `0x00`: False，`0x01`: True（非零值均解讀為 True） |
| `Int8 / UInt8` | 1 Byte | 8 位元有號/無號整數 |
| `Int16 / UInt16` | 2 Bytes | 16 位元有號/無號整數，Little-Endian |
| `Int32 / UInt32` | 4 Bytes | 32 位元有號/無號整數，Little-Endian |
| `Int64 / UInt64` | 8 Bytes | 64 位元有號/無號整數，Little-Endian |
| `HandleID` | 8 Bytes | 等同 `UInt64`，全域唯一物件識別碼，Little-Endian |
| `Float32` | 4 Bytes | IEEE 754-2008 單精度浮點數，Little-Endian |
| `Float64` | 8 Bytes | IEEE 754-2008 雙精度浮點數，Little-Endian |
| `String` | 動態 (4 + N) | **長度前綴 UTF-8 字串**：先寫入 4 位元組 `UInt32 len`，接續 `len` 個位元組的 UTF-8 資料，**無** null 結尾字元。若 `len == 0`，僅佔用 4 位元組且無後續負載。 |

---

## 📦 2. 兩段式藍圖二進位資料流格式 (Two-Segment Wire Layout)

每一個 OuroKore 物件的二進位藍圖串流，嚴格依序由**兩大段落**組成：

```
+-------------------------------------------------------------------------+
|                  段落一：純屬性區 (Pure Payload KV Segment)              |
|  [Property 1: Key + Value] [Property 2: Key + Value] ... [Property M]  |
+-------------------------------------------------------------------------+
                                    │
                                    ▼
+-------------------------------------------------------------------------+
|                 段落二：拓撲邊緣名冊區 (Edge Roster Segment)             |
|  [edge_count: UInt32]                                                   |
|    - Slot 1: [slot_name: String] [target_count: UInt32] [HandleID * N]  |
|    - Slot 2: [slot_name: String] [target_count: UInt32] [HandleID * K]  |
|    ...                                                                  |
+-------------------------------------------------------------------------+
```

### 2.1 段落一：純屬性資料區 (Pure Payload KV Segment)
物件內部依序寫入自訂欄位。每個屬性的二進位封裝結構如下：

```
+-------------------------------+-----------------------------------+
| 鍵名長度 (UInt32, 4 Bytes)    | 鍵名內容 (UTF-8 bytes, key_len B) |
+-------------------------------+-----------------------------------+
| 屬性數值二進位負載 (Value Payload, 長度視型別而定)                |
+-------------------------------------------------------------------+
```

* **字串屬性值**：以長度前綴格式寫入（`UInt32 val_len` + UTF-8 bytes）。
  * 支援跨語言標準字串與現代 C++20 原生 `std::u8string`、`char8_t*`、`string_view`。
* **數值屬性值**：直接以原生 Little-Endian 位元組寫入對應大小（如 `Int32` 佔 4 位元組）。
* **防禦性約束**：
  1. **重複鍵阻斷 (Duplicate Key Guard)**：在同一物件序列化串流中，若出現相同之 Key 名稱，解析端必須立即中斷（Fail-Fast 拋出重複鍵異常）。
  2. **鍵名序列匹配驗證 (Key Mismatch Guard)**：反序列化讀取時，讀出的 Key 必須與期望名稱完全一致，否則視為串流版本失配或損毀。
  3. **裸指標位址封鎖 (Raw Pointer Prohibition)**：二進位資料流嚴格禁止將任何記憶體指標位址（如物件指標或裸指標）以 POD 形式寫入串流；實作端必須在編譯期（例如透過 `static_assert`）或執行期檢測並強制拒絕，防止反序列化時產生懸空指標崩潰。

### 2.2 段落二：拓撲邊緣名冊區 (Edge Roster Segment)
接續在純屬性資料區之後，由核心自動遍歷物件的插槽（Slot）名冊並寫入：

```
[edge_count: UInt32] (4 Bytes)
  │
  ├─► Slot 0:
  │     [name_len: UInt32] (4 Bytes)
  │     [slot_name: UTF-8] (name_len Bytes)
  │     [target_count: UInt32] (4 Bytes)
  │     [target_ids: UInt64 * target_count] (8 * target_count Bytes)
  │
  ├─► Slot 1:
  │     [name_len: UInt32] ...
  ...
```

* `edge_count`：物件內持有的總插槽數量。
* 對於每一個 Slot：
  * `slot_name`：插槽名稱（長度前綴 UTF-8 字串）。
  * `target_count`：該插槽所關聯的目標物件數量。
  * `target_ids`：連續的 `UInt64` HandleID 陣列。

---

## 🛡️ 3. 反序列化安全邊界常數與兩階段防線 (Safety Invariants)

為防止惡意構造的二進位串流引發記憶體耗盡（OOM）或整數溢位攻擊，所有解析實作必須實施以下門禁：

| 安全常數 | 上限值 | 目的與防禦機制 |
| :--- | :--- | :--- |
| `kMaxEdgeCount` | `100,000` | 單一物件所允許的最大插槽數量，防止巨量 edge_count 迴圈。 |
| `kMaxTargetsPerSlot` | `100,000` | 單一插槽所允許的最大目標 ID 數量，防止容器爆量配置。 |
| `kMaxSlotNameLength` | `1024` Bytes | 單一插槽名稱之最大字元長度。 |

### 兩階段驗證套用原則 (Two-Phase Apply)
在反序列化任何資料串流時，實作端必須遵循：
1. **第一階段（驗證與暫存）**：先在暫存結構中完整解析、驗證串流長度與所有防禦約束。若串流意外截斷（Truncated）或驗證失敗，立即終止並拋出例外。
2. **第二階段（原子套用）**：所有資料驗證無誤後，才正式套用至物件的記憶體屬性與拓撲關係中。保證強例外安全（Strong Exception Safety），絕不留下半套損毀的物件狀態。

---

## 💾 4. 儲存驅動落盤協議 (Storage Driver Wire Contract)

持久化儲存介面是 OuroKore 與實體儲存媒體溝通的二進位契約。以**中性介面描述語言（Neutral IDL）**定義如下：

```text
Interface StorageDriver:
    // 落盤儲存操作：將物件資料以 HandleID 為鍵寫入介質
    Function Save(handle_id: UInt64, data: ByteSequence, size: UInt64) -> Boolean

    // 讀取復水操作：依據 HandleID 讀取物件原始二進位資料
    Function Load(handle_id: UInt64, out_buffer: MutableByteSequence, buffer_size: UInt64, out_actual_size: MutableRef<UInt64>) -> Boolean

    // 刪除實體操作：自儲存介質中永久移除物件資料
    Function Delete(handle_id: UInt64) -> Boolean
End Interface
```

* **Key-Value 格式**：儲存驅動以 `HandleID (UInt64)` 為唯一 Key，以本規範定義的兩段式二進位串流為 Value。
* **跨語言相容性**：無論底層介質採用本機檔案（File Storage）、記憶體（InMemoryStorage）、SQLite 或分散式 KV 資料庫，其儲存的資料二進位 Payload 均完全相同，可直接被不同語言之 OuroKore 核心交換讀取。


---

## 🌲 5. 樹狀物件節點與文字 DSL 串流協議 (Hierarchical Object Nodes & Text DSL Wire Format RFC)

本節定義 OuroKore 樹狀物件節點 `TreeNode`（`TreeNodeBase<D>`）與文字 DSL 串流 `TreeIO` 的資料交換規範。

### 5.1 物件節點本體論與異質多型階層 (Object Nodes & Heterogeneous Polymorphism RFC)
* **節點即為物件本體（The Node IS The Object）── 徹底廢除「容器」概念**：
  樹狀結構並非被動裝載資料的容器（Container），而是實體的**物件節點（Object Node）**。在 CRTP 架構下，衍生類別 `D` 自身就是具備實體欄位與業務邏輯的 C++ 物件，`TreeNodeBase<D>` 作為樹狀拓撲能力注入基底（Tree Topology Mixin）。整棵樹是由一組具備父子拓撲關係的領域物件節點所構成的階層體系。
* **原生異質物件節點階層（Heterogeneous Object Nodes）**：
  由共通多型基底節點類別（例如 `class BaseEntity : public TreeNodeBase<BaseEntity>`）派生之不同具體業務物件節點（如 `MonsterEntity`、`ItemEntity`、`LightSourceEntity` 等），可在同一個父節點的子節點序列中共存管理。
* **C++20 `std::derived_from` 強型別零手動轉型直出（Zero-Casting）**：
  所有新增與插入介面（`CreateRoot`、`AddChild`、`PrependChild`、`PushElement`、`InsertBefore`、`InsertAfter`）均受 C++20 Concept 編譯期約束，完美轉發建構參數並直接回傳 `std::shared_ptr<SubT>`，呼叫端享有零手動轉型（Zero-Casting）便利。
* **衍生類別職責邊界與嚴格私有封裝（Strict Encapsulation Invariant）**：
  1. **延伸類別專注於資料處理**：延伸類別的職責為領域屬性與業務行為，**絕不直接碰觸底層內部拓撲**。
  2. **拓撲狀態全面私有化 (`private`)**：所有底層成員變數（`m_elements`、`m_nameMap`、`m_parent`、`m_self`、`m_treeMutex` 等）與內部同步方法全面收斂為 `private`，嚴格禁止衍生類別直接碰觸，杜絕繞過讀寫鎖篡改資料引發競態。
  3. **節點操作一律使用公開 API**：若延伸類別業務方法需要存取或操作子節點，**一律調用公開操作功能**（如 `AddChild()`、`PushElement()`、`operator[]`、`GetName()`、`GetParent()`、`GetTreeMutex()` 等），享有整樹讀寫鎖與快取索引的完備保護。
  4. **受保護基底建構子 (`protected`)**：`explicit TreeNodeBase(name)` 宣告為 `protected`，僅供延伸類別於初始化自身時調用；外部禁止直接實例化裸 `TreeNodeBase<D>`。

### 5.2 資料純度自動推導雙模態 (Data-Driven Morphism)
樹節點本身不儲存形態列舉，形態完全由子節點結構純度於執行期自動推導：
* **物件模式（Object Mode，DSL 界定符 `{}`）**：子節點全體均為具名節點（`child_count == named_child_count`）。
* **陣列模式（Array Mode，DSL 界定符 `{}` 或相容 `()`）**：混入任何無名（匿名）節點（`child_count > named_child_count`）。

### 5.3 四大正交界定符與語法規範 (Orthogonal Delimiters)
文字 DSL 採用正交之語法 Token，等號 `=` 為具名賦值關鍵字：
* `[節點名稱]`：名稱標記。
* **`=`**：**具名賦值關鍵字**（將後續引號內容賦值予該具名節點，具名節點有值時必然使用；無值純標籤如 `[IsAdmin]` 則無等號）。
* `"字串內容"`：Payload 資料（支援 0~255 二進位位元組與轉義字元 `\"`、`\\`、`\n`、`\xHH`）。
* `{子節點區塊}`：子節點階層區塊（全面統一為大括號，Allman 風格獨立換行；解析時相容舊式 `()`）。

#### 三種緊湊傳輸編碼模式 (CompactMode Wire Styles)：
1. **模式 1：標準排版 (CompactMode::Pretty)**：含標準縮排、空白與換行，供人類閱讀。
2. **模式 2：含等號緊湊 (CompactMode::WithEqual)**：`[Key]="Value"{[Child]="1"}`。
3. **模式 3：極致緊湊 (CompactMode::Compact)**：保留等號但移除所有多餘空白 `[Player]="Hero"{[HP]="100"}`。
   - 規範保證：連續具名空節點（如 `[A][B]`）、匿名空元素、物件陣列均 100% 精準對稱還原，單元素陣列節點（如 `{"Item"}`）反序列化時拓撲身分永不降級脫殼。

### 5.4 外部工廠反序列化與特權掛載協定 (Factory Inversion of Control & Fail-Fast RFC)
文字 DSL 串流還原為異質物件樹時，`TreeIO` 支援「反轉控制（Inversion of Control）」外部工廠模式：
* **反序列化不預先構造節點**：解析時不盲目實例化預設節點，而是將節點名稱與字串內容送給應用端註冊的工廠：`factory(const std::u8string &name, const std::string &data) -> std::shared_ptr<BaseEntity>`。
* **內部特權拓撲掛載（`AttachChild`）**：由工廠構造完成的異質物件實體，由 `TreeIO` 透過內部特權（`AttachChild` 為 `private`，宣告 `friend class TreeIO;`）安全掛載至父節點。一般外部程式碼無法直接調用 `AttachChild`，確保平時所有節點一律嚴格由父節點原地延伸構造。
* **嚴格資料毀損中斷（Fail-Fast / All-or-Nothing）**：
  1. 若工廠無法識別資料（回傳 `nullptr`），視為資料毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。
  2. 若回傳物件與同層名稱重複，同樣視為毀損，**立即中止全體解析並向呼叫端回傳 `nullptr`**。

### 5.5 顯式堆疊非遞迴 FSM 反序列化演算法 (Non-recursive FSM Deserialization)
反序列化演算法以堆積（Heap）顯式堆疊 `Stack<ParseFrame>` 驅動，呼叫棧（Call Stack）深度恆為 $O(1)$，數學證明巨深文字 DSL 免疫呼叫堆疊溢位（Stack Overflow）：

```text
Structure ParseFrame:
    current_node: NodeHandle
    state: ParserState
    accumulated_name: String
    accumulated_data: String
End Structure

Function DeserializeFromString(dsl_text: String, factory: Optional<FactoryFunction>) -> NodeHandle:
    Let root = CreateRootNode()
    Let stack = DynamicStack<ParseFrame>()
    stack.Push(ParseFrame(root, STATE_SEEK_NODE))
    
    Let cursor = 0
    While cursor < dsl_text.Length Do
        Let ch = dsl_text[cursor]
        
        // 略過空白字元與三種註解 (//, /* */, #)
        If IsCommentOrWhitespace(ch, dsl_text, cursor) Then
            cursor = SkipCommentOrWhitespace(dsl_text, cursor)
            Continue
        End If
        
        // 狀態機基於 stack.Top() 轉移：
        Match stack.Top().state With
            Case STATE_SEEK_NODE:
                If ch == '[' Then
                    stack.Top().state = STATE_READ_NAME
                Else If ch == '{' Or ch == '(' Then
                    Let child = stack.Top().current_node.AddChild()
                    stack.Push(ParseFrame(child, STATE_SEEK_NODE))
                Else If ch == '}' Or ch == ')' Then
                    stack.Pop() // 顯式出棧，零遞迴返回！
                End If
            Case STATE_READ_NAME:
                // 解析至閉合中括號 ']' 並填入 accumulated_name
            Case STATE_READ_DATA:
                // 解析至閉合雙引號 '"' 並填入 accumulated_data
        End Match
        cursor = cursor + 1
    End While
    
    Return root
End Function
```
