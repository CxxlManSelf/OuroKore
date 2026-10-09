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

## 🌲 5. 樹狀結構階層與文字 DSL 串流協議 (Hierarchical Tree & Text DSL Wire Format RFC)

本節定義 OuroKore 階層式容器 `TreeNode` 與文字 DSL 串流 `TreeIO` 的資料交換規範。

### 5.1 資料純度自動推導雙模態 (Data-Driven Morphism)
樹節點本身不儲存形態列舉，形態完全由子節點結構純度於執行期自動推導：
* **具名物件模式（Object Mode）**：子節點全體均為具名節點（`child_count == named_child_count`），文字 DSL 採用 `{}` 大括號區塊。
* **匿名陣列模式（Array Mode）**：混入任何無名（匿名）節點（`child_count > named_child_count`），文字 DSL 亦全面統一採用 `{}` 大括號區塊封裝匿名元素。

### 5.2 三大正交界定符與具名賦值關鍵字 (Orthogonal Delimiters & Assignment)
文字 DSL 採用三大正交語法 Token，容器區塊全面統一為大括號 `{}`，等號 `=` 為具名賦值關鍵字：
* `[節點名稱]`：名稱標記（跳脫字元支援 `\]` 與 `\\`）。
* `=`：具名賦值關鍵字（具名節點賦值時必然使用）。
* `"字串內容"`：Payload 資料（支援 0~255 二進位位元組與轉義字元 `\"`、`\\`、`\n`、`\xHH`）。
* `{子節點成員}`：容器區塊（全面統一為大括號，Allman 風格獨立換行）。

#### 兩種緊湊傳輸編碼模式 (CompactMode Wire Styles)：
1. **模式 1：格式化排版 (CompactMode::Pretty)**：含標準縮排、空白與換行，供人類閱讀（Allman 風格）。
2. **模式 2：緊湊傳輸 (CompactMode::Compact)**：`[Key]="Value"{[Child]="1"}`，無多餘空格與換行，具名賦值保留等號 `=`。
   - 規範保證：連續具名空節點（如 `[A][B]`）、匿名空元素、物件區塊均 100% 精準對稱還原，單元素反序列化時拓撲身分永不降級脫殼。

### 5.2.1 顯式匿名容器標頭與平級消歧義協議 (Explicit Anonymous Headers & Disambiguation)
為消除同層匿名資料與容器並存時的語法二義性，協議強制規範：
1. **顯式匿名容器標頭 `[]`**：凡無名稱且具備子節點成員之容器，序列化輸出時一律強制帶有 `[]` 標頭（Allman 排版獨立一行），緊湊模式為 `[]{...}`。
2. **顯式帶資料匿名容器 `[] = "Data"`**：若無名稱容器自身持有 Payload 資料，一律以 `[] = "Data"` 標明，緊湊模式為 `[]="Data"{...}`。
3. **純字串葉節點隔離**：`"Data"` 結束後狀態機立即重設活躍子節點，絕不貪婪搶佔後續之大括號容器。同層平級書寫 `"Data"` 與 `[] { ... }` 保證 100% 判定為平級兄弟節點。
4. **空葉節點標記**：無名稱、無資料、無子節點之空節點以空字串引號 "" 表達。
5. **未引導裸大括號之雜訊判定**：未以合法標頭（[名稱] 或 []）引導之孤立裸大括號 { ... }，其前後大括號符號純屬文字雜訊逕行過濾（不建立子容器層級）；區塊內部合規之純字串等葉節點直接平級納入當前容器。

### 5.3 註解語法與界定符遮蔽
狀態機原生支援三種風格註解：
* `// 單行註解`（跳至行尾）
* `/* 區塊註解 */`（跳至閉合符 `*/`）
* `# 腳本註解`（跳至行尾）
* **遮蔽保證**：註解內部包含的引號與括號均被狀態機嚴格忽略，不得觸發任何狀態轉移。

### 5.4 顯式堆疊非遞迴 FSM 反序列化演算法 (Non-recursive FSM Deserialization)
反序列化演算法以堆積（Heap）顯式堆疊 `Stack<ParseFrame>` 驅動，呼叫棧（Call Stack）深度恆為 $O(1)$，數學證明巨深文字 DSL 免疫呼叫堆疊溢位（Stack Overflow）：

```text
Structure ParseFrame:
    current_node: NodeHandle
    state: ParserState
    accumulated_name: String
    accumulated_data: String
End Structure

Function DeserializeFromString(dsl_text: String) -> NodeHandle:
    Let root = CreateRootNode()
    Let stack = DynamicStack<ParseFrame>()
    stack.Push(ParseFrame(root, STATE_SEEK_NODE))
    
    Let cursor = 0
    While cursor < dsl_text.Length Do
        Let ch = dsl_text[cursor]
        
        // 略過空白字元與三種註解
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
