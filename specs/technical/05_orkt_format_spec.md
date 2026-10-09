# 05. ORKT 樹狀結構文字交換格式規格書 (ORKT Text Format RFC & Grammar Specification)

本文件定義 **ORKT（OuroKore Tree Text Format，副檔名 `.orkt`）** 的正式資料模型、字元編碼、詞法與語法文法、狀態機解析語意及序列化標準。
本規範為**程式語言無關（Programming-Language Agnostic）**之開放式格式標準，適用於任何程式語言（如 C++、Rust、C#、Python、Go、Java、TypeScript 等）的剖析器（Parser）與序列化器（Serializer）實現，確保不同平台與工具鏈之間的 100% 雙向互通性。

---

## 📑 目錄

1. [標記與術語定義](#1-標記與術語定義)
2. [抽象資料模型 (Abstract Data Model)](#2-抽象資料模型-abstract-data-model)
3. [字元編碼與字節流規範 (Encoding & Byte Stream)](#3-字元編碼與字節流規範-encoding--byte-stream)
4. [詞法結構與標記定義 (Lexical Grammar)](#4-詞法結構與標記定義-lexical-grammar)
5. [形式語法文法 (Formal Syntax Grammar - EBNF)](#5-形式語法文法-formal-syntax-grammar---ebnf)
6. [節點拓撲形態與語法映射 (Node Morphologies)](#6-節點拓撲形態與語法映射-node-morphologies)
7. [解析狀態機語意與消歧義鐵律 (Disambiguation Invariants)](#7-解析狀態機語意與消歧義鐵律-disambiguation-invariants)
8. [輸出模式與序列化設定 (Serialization Profiles)](#8-輸出模式與序列化設定-serialization-profiles)
9. [錯誤處理與驗證原則 (Validation & Fail-Fast Semantics)](#9-錯誤處理與驗證原則-validation--fail-fast-semantics)
10. [綜合參考範例 (Comprehensive Examples)](#10-綜合參考範例-comprehensive-examples)

---

## 1. 標記與術語定義

依據 RFC 2119 規範，本文件中所使用的關鍵字定義如下：
* **必須（MUST / SHALL）**：絕對必要遵守的規範。
* **嚴禁（MUST NOT / SHALL NOT）**：絕對禁止的行為。
* **應當（SHOULD / RECOMMENDED）**：在有正當理由的情況下方可忽略，但強烈建議遵守。
* **不應（SHOULD NOT / NOT RECOMMENDED）**：通常不建議進行的行為。
* **可以（MAY / OPTIONAL）**：完全由實作端自行決定的選擇性行為。

### 核心術語
* **ORKT**：OuroKore Tree 文字交換格式之簡稱，推薦標準副檔名為 `.orkt`，推薦 MIME 類型為 `text/x-orkt` 或 `application/x-orkt`。
* **物件節點（Object Node）**：ORKT 格式中的基本建構單元。節點不是被動裝載資料的容器，其自身即為資料實體。
* **有效負載（Payload / Data）**：儲存於節點本體的純文字或二進位資料串流。
* **具名字節點（Named Child）**：擁有非空 UTF-8 名稱標籤的子節點。
* **匿名子節點（Anonymous Child）**：名稱為 `[]` 的子節點。
* **純標籤（Tag）**：擁有名稱但有效負載為空且無子節點的節點。

---

## 2. 抽象資料模型 (Abstract Data Model)

ORKT 表現的是一個**階層式樹狀物件節點圖（Hierarchical Object Node Tree）**。在概念模型中，每個節點 $N$ 抽象定義為一個三元組：

$$N = \langle \text{Name}, \text{Data}, \text{Children} \rangle$$

### 2.1 三維度正交模型 (Three Orthogonal Dimensions)
每個物件節點擁有三個互相獨立、正交的維度，可任意排列組合：

| 維度名稱 | 類型 | 描述 |
| :--- | :--- | :--- |
| **名稱（Name）** | `UTF-8 String` | 節點的字串識別碼（嚴格必須為合法 UTF-8 編碼）。若為匿名節點，則為 `[]`。 |
| **資料負載（Data）** | `String` / `Binary Stream` | 節點承載的本體資料內容。若無資料，則為空字串 `""`。 |
| **子節點序列（Children）** | `Ordered List<Node>` | 該節點所擁有的子節點循序列表，長度大於等於 0。 |

### 2.2 雙模態統合語意 (Unified Dual-Mode Semantics)
在傳統格式（如 JSON）中，「物件/字典（Map）」與「陣列/列表（List）」被嚴格區分為兩種互不相容的資料結構。**ORKT 徹底消除此假性二分法，將序列與字典深度統合於單一節點模型**：

1. **嚴格循序性（Ordered Sequence）**：
   * 所有子節點（無論具名或匿名）均儲存於有序線性列表中。
   * 子節點在文檔中出現的先後順序**必須被完整保留**。
   * 所有子節點均可透過 $0$-based 整數下標（Index）進行循序存取。
2. **同層具名索引（Key-Value Addressing）**：
   * 所有具名字節點在其父節點的作用域（Scope）內同時具備鍵值定址能力。
   * **具名鍵值唯一性鐵律**：在同一父節點的直接子節點集合中，任何兩個具名字節點的 `Name` **嚴禁重複**。
   * 具名字節點可透過名稱鍵值進行檢索。
3. **完全互通性（Interoperability）**：
   * 同一個具名子節點，按下標檢索與按鍵名檢索指向**完全相同的節點實體**。
   * 允許同一父節點下同時混排具名字節點與匿名子節點。

---

## 3. 字元編碼與字節流規範 (Encoding & Byte Stream)

1. **字元集標準與名稱編碼鐵律**：
   * ORKT 文件**必須**使用 **UTF-8** 編碼（RFC 3629）。
   * **節點名稱必須為 UTF-8**：所有具名節點的名稱標籤（Name）**必須且只能為合法 UTF-8 字串**，嚴禁包含畸形或非法的 UTF-8 位元組序列。
   * 嚴禁在 ORKT 核心傳輸或檔案儲存中使用任何非 UTF-8 本地編碼（如 UTF-16、GBK、Big5、Windows-1252 等）。
2. **Byte Order Mark (BOM)**：
   * 檔案開頭**不應（SHOULD NOT）**包含 UTF-8 BOM（位元組序列 `0xEF 0xBB 0xBF`）。
   * 相容剖析器在讀取到 UTF-8 BOM 時，**應當**將其作為可選的前導標記自動忽略，不得將其解讀為語法錯誤或節點名稱的一部分。
3. **行尾換行符號（Newline）**：
   * 剖析器**必須**相容兩種行尾格式：Unix 風格的 `LF`（`\n`, `0x0A`）與 Windows 風格的 `CRLF`（`\r\n`, `0x0D 0x0A`）。
   * 獨立的 Carriage Return `CR`（`\r`）在非字串字面量區域中應視為一般空白字元。

---

## 4. 詞法結構與標記定義 (Lexical Grammar)

ORKT 剖析器將原始 UTF-8 字串流拆解為以下基本詞法單元（Tokens）：

### 4.1 空白字元 (Whitespace)
空白字元用於分隔標記，除字串或名稱字面量內部外，空白字元本身不具備語法語意，解析時應當忽略。
* 空白字元包含：空格（Space, `0x20`）、水平定位字元（Horizontal Tab, `0x09`）、換行字元（Line Feed, `0x0A`）與歸位字元（Carriage Return, `0x0D`）。

### 4.2 註解語法 (Comments)
ORKT 原生支援三種風格的註解。註解可在任何允許空白字元的位置出現，剖析器在詞法分析階段必須將其作為空白過濾：
1. **雙斜線單行註解**：以 `//` 起始，至當前行尾結束（`\n` 或 `\r`）。
2. **井號單行註解**：以 `#` 起始，至當前行尾結束（`\n` 或 `\r`）。
3. **區塊多行註解**：以 `/*` 起始，至最近的 `*/` 結束。**不支援巢狀區塊註解**。

> [!IMPORTANT]
> 註解符號若出現在雙引號 `"..."` 或中括號 `[...]` 的內部字面量中，必須被解讀為字串內文字元，絕不可觸發註解過濾。

### 4.3 標點與運算元標記 (Punctuators & Operators)
* `[`：名稱標頭起始界定符。
* `]`：名稱標頭結束界定符。
* `=`：鍵值賦值運算元（Assignment Operator）。
* `{`：容器區塊起始界定符（Block Open）。
* `}`：容器區塊結束界定符（Block Close）。

> [!CAUTION]
> **小括號 `(` 與 `)` 嚴禁作為容器界定符**：
> 在 ORKT 正式規格中，所有容器層級**唯一採用大括號 `{ ... }`**。任何非字面量中的小括號 `(` 或 `)` 均被視為無效文字雜訊或語法錯誤。

### 4.4 轉義字元序列 (Escape Sequences)
在名稱標記 `[...]` 與字串字面量 `"..."` 內部，使用反斜線 `\` 作為轉義引導符。

| 轉義序列 | 替換字元 | 說明 | 適用區域 |
| :--- | :--- | :--- | :--- |
| `\\` | `\` (`0x5C`) | 反斜線本體 | 名稱 `[...]` 與 字串 `"..."` |
| `\"` | `"` (`0x22`) | 雙引號 | 字串 `"..."` |
| `\]` | `]` (`0x5D`) | 右中括號 | 名稱 `[...]` |
| `\n` | `\n` (`0x0A`) | 換行符號 (Line Feed) | 名稱 `[...]` 與 字串 `"..."` |
| `\r` | `\r` (`0x0D`) | 歸位符號 (Carriage Return) | 名稱 `[...]` 與 字串 `"..."` |
| `\t` | `\t` (`0x09`) | 水平定位字元 (Tab) | 名稱 `[...]` 與 字串 `"..."` |
| `\0` | NUL (`0x00`) | 空字節（二進位負載支援） | 字串 `"..."` |

若出現未定義的轉義序列（如 `\x`），剖析器**應當**保留反斜線後方的原生字元 `x`，或回報非法的轉義錯誤。

---

## 5. 形式語法文法 (Formal Syntax Grammar - EBNF)

本節以 ISO/IEC 14977 擴充巴科斯-諾爾範式（EBNF）定義 ORKT 之完整語法結構：

```ebnf
(* ============================================================ *)
(* ORKT 頂層文法 (Document Level)                               *)
(* ============================================================ *)

Document            = Whitespace , [ NodeList ] , Whitespace ;

NodeList            = Node , { Whitespace , Node } ;

Node                = NamedNode
                    | AnonContainer
                    | ValueLeaf
                    | EmptyLeaf ;

(* ============================================================ *)
(* 具名節點文法 (Named Node Productions)                         *)
(* ============================================================ *)

NamedNode           = NodeHeader , [ Whitespace , ValueAssignment ] , [ Whitespace , Block ] ;

NodeHeader          = "[" , NodeName , "]" ;

NodeName            = { EscapedCharInName | ValidNameChar } ;

ValueAssignment     = "=" , Whitespace , StringLiteral ;

(* ============================================================ *)
(* 匿名節點文法 (Anonymous Node Productions)                     *)
(* ============================================================ *)

(* 顯式標頭匿名容器：例如 [] { ... } 或 [] = "Data" { ... } *)
AnonContainer = "[]" , [ Whitespace , ValueAssignment ] , Whitespace , Block ;


(* 純值葉節點：例如 "Data" *)
ValueLeaf           = StringLiteral ;

(* 空葉節點：例如 "" *)
EmptyLeaf           = '""' ;

(* ============================================================ *)
(* 容器區塊文法 (Container Block Productions)                    *)
(* ============================================================ *)

Block               = "{" , Whitespace , [ NodeList ] , Whitespace , "}" ;

(* ============================================================ *)
(* 詞法基元 (Lexical Terminals)                                  *)
(* ============================================================ *)

StringLiteral       = '"' , { EscapedCharInString | ValidStringChar } , '"' ;

ValidNameChar       = ? 任何非 ']'、非 '\' 之 UTF-8 字元 ? ;
EscapedCharInName   = "\" , ( "]" | "\" | "n" | "r" | "t" ) ;

ValidStringChar     = ? 任何非 '"'、非 '\' 之 UTF-8 字元 ? ;
EscapedCharInString = "\" , ( '"' | "\" | "0" | "n" | "r" | "t" ) ;

Whitespace          = { SpaceChar | Comment } ;
SpaceChar           = ? ' ' (0x20) | '\t' (0x09) | '\n' (0x0A) | '\r' (0x0D) ? ;

Comment             = LineCommentSlash | LineCommentHash | BlockComment ;
LineCommentSlash    = "//" , { ? 任何非換行之 UTF-8 字元 ? } , ( '\n' | '\r' | ? 檔案結尾 ? ) ;
LineCommentHash     = "#"  , { ? 任何非換行之 UTF-8 字元 ? } , ( '\n' | '\r' | ? 檔案結尾 ? ) ;
BlockComment        = "/*" , { ? 任何字元，直至遇到 "*/" ? } , "*/" ;
```

---

## 6. 節點拓撲形態與語法映射 (Node Morphologies)

ORKT 的三維度正交模型衍生出以下幾種標準節點形態，每種形態皆有明確的語法表現：

### 6.1 節點形態全體矩陣對照表

| 形態名稱 | 名稱 (Name) | 資料 (Data) | 子節點 (Children) | 標準格式化表現 (Pretty) | 緊湊表現 (Compact) | 範例語法 |
| :--- | :---: | :---: | :---: | :--- | :--- | :--- |
| **空葉節點 (Empty Leaf)** | ❌ (`[]`) | ❌ (`""`) | ❌ ($0$) | `""` | `""` | `""` |
| **純值葉節點 (Value Leaf)** | ❌ (`[]`) | ✔️ | ❌ ($0$) | `"Data"` | `"Data"` | `"草藥"` |
| **純匿名容器 (Anon Container)** | ❌ (`[]`) | ❌ (`""`) | ✔️ ($>0$) | `[]`<br>`{`<br>&nbsp;&nbsp;`...`<br>`}` | `[]{...}` | `[] { "A" "B" }` |
| **帶值匿名容器 (Anon with Data)** | ❌ (`[]`) | ✔️ | ✔️ ($>0$) | `[] = "Data"`<br>`{`<br>&nbsp;&nbsp;`...`<br>`}` | `[]="Data"{...}` | `[] = "Header" { "Sub" }` |
| **具名純標籤 (Named Tag)** | ✔️ | ❌ (`""`) | ❌ ($0$) | `[Name]` | `[Name]` | `[IsActive]` |
| **具名屬性賦值 (Named Property)** | ✔️ | ✔️ | ❌ ($0$) | `[Name] = "Data"` | `[Name]="Data"` | `[HP] = "100"` |
| **具名純容器 (Named Container)** | ✔️ | ❌ (`""`) | ✔️ ($>0$) | `[Name]`<br>`{`<br>&nbsp;&nbsp;`...`<br>`}` | `[Name]{...}` | `[Inventory] { "草藥" }` |
| **具名帶值容器 (Named with Data)** | ✔️ | ✔️ | ✔️ ($>0$) | `[Name] = "Data"`<br>`{`<br>&nbsp;&nbsp;`...`<br>`}` | `[Name]="Data"{...}` | `[Player] = "Hero" { [HP]="100" }` |

---

## 7. 解析狀態機語意與消歧義鐵律 (Disambiguation Invariants)

為確保文法具備嚴格的無歧義確定性（Unambiguous Determinism）與單遍無回溯線性掃描特性，剖析器**必須**遵循以下消歧義語意規則：

### 7.1 等號不可或缺鐵律 (Assignment Invariant)
在 ORKT 語法中，等號 `=` 是**鍵值賦值（Key-Value Assignment）的唯一語法標誌**。
* **具名屬性賦值**：`[Key] = "Val"` 成功將資料 `"Val"` 賦值予具名節點 `Key`。
* **⚠️ 省略等號的嚴格消歧義**：
  若輸入為 `[Key] "Val"`（省略了等號）：
  剖析器**嚴禁**將其合成為賦值關係！
  狀態機**必須**嚴格判定為兩個平級兄弟節點：
  1. 索引 $i$：純具名標籤節點 `[Key]`（無資料負載）。
  2. 索引 $i+1$：獨立的純值葉節點 `"Val"`（無名稱）。

```
輸入文本: [Tag] "NextItem"
                    │
                    ▼ 剖析器解析結果
父容器 (Children Count = 2)
 ├── [0] Name=[Tag],      Data="",         Children=[] (純標籤)
 └── [1] Name=[],         Data="NextItem", Children=[] (平級匿名葉節點)
```

---

### 7.2 容器節點起頭標頭 [] 結構規範與非引導區塊雜訊判定 (Container Header Invariant & Non-Header Blocks)
為確保語法模型嚴謹性並維持寬容狀態機的一致哲學，ORKT 規範定義以下結構與剖析規則：

1. **容器節點起頭標頭鐵律（Container Header Invariant）**：
   凡是具有子節點區塊 { ... } 的容器節點，**結構起頭必然要有一個方括號標頭**：
   - **具名容器節點**：以 [名稱] 作為起頭標頭（例如 [Root] { ... }）。
   - **匿名容器節點**：以 [] 作為起頭標頭（例如 [] { ... } 或帶有負載的 [] = "Data" { ... }）。
   - **語意釐清（非間隔符號）**：[] 是匿名容器節點本身的固有結構起頭宣告，代表「此處開始一個未命名的容器節點」，**絕非用於兄弟節點之間的排版間隔或混排區隔符**。

2. **獨立純值葉節點非貪婪閉合（Non-Greedy Leaf Rule）**：
   當剖析器完成雙引號純值葉節點 "Data" 的讀取時，該節點**立即關閉（Committed）**並作為完整葉節點加入當前父節點的子清單中，其作用域當即完結。純值葉節點無子階層，亦不需標頭，更**絕不貪婪搶佔**後續遇到的任何結構。

`orkt
// 合規結構範例：
[]
{
    "ItemA"
    []
    {
        "Sub1"
        "Sub2"
    }
}
`

* **合規解析流程序列**：
  1. 讀取頂層 []：開啟頂層匿名容器節點。
  2. 讀取頂層 {：進入頂層容器區塊範疇。
  3. 讀取 "ItemA"：建立純值子葉節點 $[0]$，值為 "ItemA"，立即關閉。
  4. 讀取內部 []：偵測到匿名容器起頭標頭，宣告新的未命名容器節點 $[1]$。
  5. 讀取內部 {：將其子節點掛載至節點 $[1]$。
  6. **保證**："ItemA" 與子容器為 100% 平級兄弟關係，結構清晰且無歧義。

3. **非引導大括號符號之雜訊判定原則 (Lenient Noise Rule)**：
   若文本中出現未依附於合法標頭（[名稱] 或 []）的孤立裸大括號 { ... }：
`orkt
// 包含無標頭裸大括號區塊之文本範例：
[]
{
    "ItemA"
    {
        "Sub1"
        "Sub2"
    }
    "ItemB"
}
`
   * **括號字元視為雜訊，內部葉節點平級保留**：依據 ORKT 寬容狀態機哲學，缺少合法標頭引導的前後大括號 { 與 } 字符本身純屬非結構文字雜訊，狀態機將其安全忽視（絕不建立子容器層級）；而區塊內部合法的純字串 "Sub1" 與 "Sub2" 依然作為無名葉節點，直接平級收錄於當前父容器中（上述範例將成功解析為依序包含 "ItemA"、"Sub1"、"Sub2"、"ItemB" 共四個平級無名葉節點之容器）。
   * **容器結構保證**：唯有經由合法方括號標頭（[名稱] 或 []）正式引導的大括號區塊，才會被狀態機識別為樹狀結構的子容器範疇。
---

### 7.3 頂層節點拆箱規範 (Root Unboxing Semantics)
ORKT 支援根節點單體與多節點流的雙模態表現：
1. **單一根節點自動拆箱**：
   若整個文檔的有效內容僅由一個頂層具名節點或帶標頭容器構成（例如 `[Player] = "Hero" { ... }`），剖析器應當將該頂層節點直接視為傳回的根節點（Root Node）。
2. **多頂層節點保全（Multi-Root Wrap）**：
   若文檔頂層包含多個平級節點（例如 `[A]="1" [B]="2"` 或多個平級葉節點），剖析器**必須**保留隱式虛擬根節點（Virtual Root，名稱為空），將文檔頂層全體節點掛載為該虛擬根節點的子節點，嚴禁遺失任何同層資料。

---

## 8. 輸出模式與序列化設定 (Serialization Profiles)

序列化器**必須**支援以下兩種標準設定檔（Profiles）：

### 8.1 格式化排版模式 (Pretty Mode)
專為人類檢視、版本控制（Git Diff）與手動編輯設計：
* **縮排規則**：推薦使用 4 個空格（Space）或 1 個定位字元（Tab），依階層深度累加。
* **Allman 區塊風格**：大括號 `{` 與 `}` 各自獨立成行。
* **賦值空格**：等號兩側保留單一空格（` = `）。
* **顯式匿名標頭**：匿名容器輸出顯式標頭 `[]` 並獨立換行。
* **空葉節點**：輸出獨立成行的空雙引號 `""`。

### 8.2 緊湊傳輸模式 (Compact Mode)
專為網路傳輸、快顯儲存與磁碟空間極小化設計：
* **消除冗餘空白**：去除所有非必要的空格、換行符號與縮排。
* **等號強制保留**：具名屬性賦值必然寫為 `[Name]="Data"`，嚴禁省略等號。
* **匿名容器標頭**：純匿名容器輸出為 `[]{...}`，帶值匿名容器輸出為 `[]="Data"{...}`。
* **無縫流式輸出**：相鄰標記緊密相連，例如：
  `[Root]="V"{[A]="1"[B]{"X""Y"}}`。

---

## 9. 錯誤處理與驗證原則 (Validation & Fail-Fast Semantics)

為保障資料完整性並杜絕半成品狀態，相容的 ORKT 剖析器**必須**遵循 **Fail-Fast（快速失敗）** 安全原則：

1. **同層具名重複鍵檢測 (Duplicate Key Error)**：
   在同一個父節點的直接子節點序列中，若發現兩名具名字節點具有完全相同的 UTF-8 名稱字串，剖析器**必須立即終止解析並拋出錯誤**，嚴禁靜默覆蓋（Overwriting）或遺棄前值。
2. **未閉合界定符 (Unterminated Delimiters)**：
   遇到未閉合的雙引號 `"`、中括號 `]`、大括號 `}` 或多行區塊註解 `*/`，剖析器**必須立即回報錯誤**。
3. **無效字元與非法結構 (Malformed Syntax)**：
   如在頂層或非賦值位置出現孤立的等號 `=`、未定義的保留字元，剖析器應當拒絕解析。
4. **巢狀深度防護 (Nesting Depth Guard)**：
   為防止惡意構造的畸形深層巢狀文件耗盡系統資源或引發堆疊溢位（Stack Overflow），實作端應當提供可設定的最大巢狀深度限制（建議安全上限為 1,000 層）或採用非遞迴方式處理。
5. **非法 UTF-8 名稱編驗 (Invalid UTF-8 Name Error)**：
   節點名稱（Name）必須符合標準 UTF-8 編碼。若剖析器在讀取名稱標頭 `[...]` 時偵測到無效或畸形的 UTF-8 位元組序列，**必須立即終止解析並拋出編碼錯誤**。
6. **容器缺失標頭錯誤 (Missing Container Header Error)**：
   只要是容器（含有子節點），**都必須以具名標頭 `[Name]` 或匿名標頭 `[]` 引導**（可選帶值賦值 `=`）。大括號 `{ ... }` 嚴格僅作為容器結構的區塊本體，**絕不可作為無標頭的孤立實體出現**。若文本中出現任何未帶標頭的裸大括號 `{ ... }`（包含頂層或任何子階層），剖析器**必須立即終止解析並回報檔案毀損與語法錯誤（Parse Error）**，嚴禁靜默相容。

---

## 10. 綜合參考範例 (Comprehensive Examples)

### 10.1 遊戲實體綜合設定檔 (Pretty Mode)
展示具名根節點、純標籤、具名鍵值、匿名陣列清單與多型子物件：

```orkt
// ==========================================
// 玩家角色設定檔 (PlayerConfig.orkt)
// ==========================================

[Player] = "Hero"
{
    [IsActive]              // 具名純標籤 (Tag)
    [Level] = "75"          // 具名整數屬性
    [AttackPower] = "1250.5" # 浮點數屬性
    
    /* 
       裝備庫存清單 (Inventory)
       示範同層混排字串道具與複雜物件
    */
    [Inventory]
    {
        "小型治療藥水"
        "傳送卷軸"
        
        [Equipment] = "傳奇雙手劍"
        {
            [Durability] = "100"
            [Enchantment] = "FireDamage+50"
            [SocketGems]
            {
                "紅寶石"
                "鑽石"
            }
        }
        
        "金幣 x500"
    }
    
    // 顯式匿名子容器（例如：技能冷卻序列）
    [Skills]
    {
        [] = "旋風斬"
        {
            [Cooldown] = "8.0"
            [ManaCost] = "25"
        }
        [] = "破甲衝鋒"
        {
            [Cooldown] = "12.0"
            [ManaCost] = "40"
        }
    }
}
```

---

### 10.2 相同資料之緊湊模式輸出 (Compact Mode)
展示同一棵物件節點樹在緊湊傳輸模式下的位元組表現：

```orkt
[Player]="Hero"{[IsActive][Level]="75"[AttackPower]="1250.5"[Inventory]{"小型治療藥水""傳送卷軸"[Equipment]="傳奇雙手劍"{[Durability]="100"[Enchantment]="FireDamage+50"[SocketGems]{"紅寶石""鑽石"}}"金幣 x500"}[Skills]{[]="旋風斬"{[Cooldown]="8.0"[ManaCost]="25"}[]="破甲衝鋒"{[Cooldown]="12.0"[ManaCost]="40"}}}
```

---

### 10.3 邊界案例與消歧義展示 (Edge Cases)

#### 範例 A：同層純字串葉節點與匿名容器消歧義
```orkt
[]
{
    "HeroPotion"
    []
    {
        "SubItem1"
        "SubItem2"
    }
    "Elixir"
}
```
* **解析模型**：
  * 外層匿名容器包含 3 個平級子項目：
    * `[0]`：字串葉節點 `"HeroPotion"`。
    * `[1]`：子容器，包含兩個子字串 `"SubItem1"` 與 `"SubItem2"`。
    * `[2]`：字串葉節點 `"Elixir"`。

#### 範例 B：空字串葉節點與特殊轉義字元
```orkt
[SpecialTokens]
{
    ""                              // 空葉節點 (Empty Leaf)
    "Line 1\nLine 2\tTabbed"        // 換行與定位字元
    "Escaped Quote: \"Hello\""      // 轉義引號
    "Backslash: \\"                 // 轉義反斜線
    [Bracket\]Name] = "BracketVal"  // 名稱內包含右中括號轉義
}
```

---

*規格版本：RFC-ORKT-1.0 | 標準修訂：2026-10-08 | 適用於 OuroKore 全生態系多語言實現*
