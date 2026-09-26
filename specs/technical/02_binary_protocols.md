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
* **數值屬性值**：直接以原生 Little-Endian 位元組寫入對應大小（如 `Int32` 佔 4 位元組）。
* **防禦性約束**：
  1. **重複鍵阻斷 (Duplicate Key Guard)**：在同一物件序列化串流中，若出現相同之 Key 名稱，解析端必須立即中斷（Fail-Fast 拋出重複鍵異常）。
  2. **鍵名序列匹配驗證 (Key Mismatch Guard)**：反序列化讀取時，讀出的 Key 必須與期望名稱完全一致，否則視為串流版本失配或損毀。

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
