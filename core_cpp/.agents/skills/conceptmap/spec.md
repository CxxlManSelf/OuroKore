# ConceptMap 技術規範

> **完整的 Map 檔生成技術參考手冊**
>
> 版本：5.8 | 更新日期：2026-02-22

---

## 🚀 快速開始

### Map 檔是什麼？
- **獨立的 HTML 文件**，包含完整的查看器和數據
- **自包含式**：無需外部依賴，可直接在瀏覽器開啟
- **三大元素**：節點（Nodes）、框架（Frames）、連接線（Connections）
- **⭐ 框架連接線**：連接線可以連接到框架，簡化複雜圖表的連接數量

### 核心要求
1. **ID 格式**：所有 ID 必須是字符串 `'node-' + Date.now()`
2. **JSON 格式**：數字和布林值不可用引號
3. **Favicon**：`concept-crafter_icon.png` 必須與 Map 檔同目錄
4. **CSS**：必須包含 `* { box-sizing: border-box; }`

---

## 🛡️ 安全的 JSON 生成方法（必讀！）

### 為什麼會產生「數字後多雙引號」錯誤？

這個錯誤通常是因為：
1. **手動編輯 JSON 時打字錯誤**（最常見）
2. 複製貼上時格式混亂
3. 字符串拼接時邏輯錯誤

### ✅ 推薦做法：使用 JavaScript 對象 + JSON.stringify

**不要直接手寫 JSON 字符串**，而是：

1. 先創建 JavaScript 對象（自動處理格式）
2. 使用 `JSON.stringify()` 轉換（確保格式正確）
3. 嵌入到 HTML 模板中

**完整範例**：

```javascript
// 步驟 1：創建 JavaScript 對象（不會有引號錯誤）
const mapData = {
    title: "時間管理",
    data: {
        nodes: [
            {
                id: "node-" + Date.now(),
                x: 800,        // ← 自動處理為數字
                y: 100,        // ← 不會多雙引號
                width: 240,
                height: 90,
                text: "<b>核心概念</b>",
                color: "#3498db",
                textAlign: "center",
                completed: false,    // ⚠️ 必須包含
                abandoned: false,    // ⚠️ 必須包含
                link: "",            // ⚠️ 必須包含（即使為空）
                isLabel: false       // ⚠️ 所有節點必填（一般節點固定為 false，不可省略）
            }
        ],
        frames: [],
        connections: []
    }
};

// 步驟 2：轉換為 JSON 字符串（自動正確格式化）
const jsonString = JSON.stringify(mapData, null, 4);

// 步驟 3：嵌入到 HTML 模板
const htmlContent = `
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <script id="map-data" type="application/json">
    ${jsonString}
    </script>
</head>
...
</html>
`;

// 步驟 4：寫入檔案
fs.writeFileSync('map.html', htmlContent, 'utf8');
```

### ❌ 危險做法：手動編輯 JSON 字符串

```javascript
// ❌ 不推薦：直接寫 JSON 字符串（容易打錯）
const json = `{
    "x": 100,
    "y": 200",    // ← 容易多打引號！
    "width": 300
}`;
```

### ⚠️ 重要原則

**生成 Map 檔時必須遵守**：
- ✅ **優先使用 JavaScript 對象 + `JSON.stringify()`**
- ✅ **生成後立即執行正則表達式檢查**（見下方「驗證方法」）
- ❌ **避免手動編輯 JSON 區塊**（除非必要，且檢查兩次）

---

## ⚠️ JSON 格式規則（極重要！）

### 最常見的致命錯誤

**數字後多雙引號**會導致整個 Map 無法顯示：

```json
// ❌ 錯誤 - 會導致整個地圖無法顯示
{
    "id": "node-123",
    "x": 120,
    "y": 440",    // ← 數字後多了雙引號！
    "width": 160
}

// ✅ 正確
{
    "id": "node-123",
    "x": 120,
    "y": 440,     // ← 正確：數字後只有逗號
    "width": 160
}
```

### 數據類型規則

| 屬性類型 | 正確格式 | 錯誤格式 |
|---------|---------|---------|
| **數字** | `"x": 100,` | `"x": 100",` 或 `"x": "100",` |
| **布林** | `"completed": false,` | `"completed": "false",` |
| **字符串** | `"id": "node-123",` | `"id": node-123,` |

### 驗證方法

**必須執行的三層檢查：**

1. **正則表達式檢查**（生成後立即執行）
   - 搜尋 `\d+"` → 必須沒有任何結果
   - 搜尋 `"(true|false)"` → 必須沒有任何結果

2. **執行驗證腳本**
   ```javascript
   // 在瀏覽器 Console 執行
   node json-validation-script.js
   ```

3. **瀏覽器測試**
   - 確認地圖正確顯示
   - 檢查 Console 無錯誤

---

## 📊 數據結構

### TypeScript 定義

```typescript
interface MapData {
    title: string;
    data: {
        nodes: Node[];
        frames: Frame[];
        connections: Connection[];
    };
}

interface Node {
    id: string;                 // ⚠️ 必須：'node-' + Date.now() （字符串）
    x: number;                  // ⚠️ 必須：數字，不可用引號
    y: number;                  // ⚠️ 必須：數字，不可用引號
    width: number;              // ⚠️ 必須：數字，不可用引號
    height: number;             // ⚠️ 必須：數字，不可用引號
    text: string;               // 必須：支援 HTML
    color: string;              // 必須：CSS 顏色值
    textAlign: 'left' | 'center' | 'right';  // 必須
    completed: boolean;         // ⚠️ 必須：布林值，不可用引號（默認 false）
    abandoned: boolean;         // ⚠️ 必須：布林值，不可用引號（默認 false）
    link: string;               // 必須：導航連結（無連結時使用空字符串 ""）
    isLabel: boolean;           // ⚠️ 所有節點必填，不可省略，不可用引號（一般節點 = false／標籤節點 = true）
}

interface Frame {
    id: string;                 // ⚠️ 必須：'frame-' + Date.now()
    x: number;                  // ⚠️ 必須：數字，不可用引號
    y: number;                  // ⚠️ 必須：數字，不可用引號
    width: number;              // ⚠️ 必須：數字，不可用引號
    height: number;             // ⚠️ 必須：數字，不可用引號
    label: string;              // 必須
    borderColor: string;        // 必須：CSS 顏色值
    bgColor: string;            // 必須：建議使用 rgba
    zIndex: number;             // ⚠️ 必須：數字，不可用引號
    completed: boolean;         // ⚠️ 必須：布林值，不可用引號（默認 false）
    abandoned: boolean;         // ⚠️ 必須：布林值，不可用引號（默認 false）
}

interface Connection {
    id: string;                 // ⚠️ 必須：'conn-' + Date.now()
    from: string;               // 必須：Node 或 Frame 的 ID ⚠️ 可以連接框架！
    to: string;                 // 必須：Node 或 Frame 的 ID ⚠️ 可以連接框架！
    arrowType: 'single' | 'double' | 'none' | 'dashed';  // 必須（四種樣式）
    label: string;              // 必須：連接線標籤（無標籤時使用空字符串 ""）
    color: string;              // 必須：CSS 顏色值
}
```

### ⚠️ 重要：所有屬性都必須導出

**為確保第三方製作的概念圖能被 Concept Crafter 正確讀取，所有屬性都必須明確導出，不可省略。**

#### 默認值規範

如果某些屬性沒有實際值，必須使用以下默認值：

| 元素類型 | 屬性 | 默認值 | 說明 |
|---------|------|--------|------|
| **Node** | completed | `false` | 節點未完成 |
| **Node** | abandoned | `false` | 節點未放棄 |
| **Node** | link | `""` | 無導航連結（空字符串） |
| **Node** | isLabel | `false` | **所有節點必填，不可省略**——一般節點必須明確寫出 `isLabel: false`，標籤節點寫 `isLabel: true` |
| **Frame** | completed | `false` | 框架未完成 |
| **Frame** | abandoned | `false` | 框架未放棄 |
| **Connection** | label | `""` | 無標籤（空字符串） |

**範例：必須包含所有屬性**

```javascript
// ✅ 正確：包含所有屬性
{
    id: "node-1234567890",
    x: 100,
    y: 200,
    width: 200,
    height: 100,
    text: "內容",
    color: "#3498db",
    textAlign: "center",
    completed: false,      // 必須明確指定
    abandoned: false,      // 必須明確指定
    link: "",              // 必須明確指定（即使為空）
    isLabel: false         // ⚠️ 所有節點必填（一般節點固定為 false，不可省略）
}

// ❌ 錯誤：缺少屬性（即使是默認值也必須明確寫出）
{
    id: "node-1234567890",
    x: 100,
    y: 200,
    width: 200,
    height: 100,
    text: "內容",
    color: "#3498db",
    textAlign: "center"
    // ❌ 缺少 completed, abandoned, link, isLabel
}
```

### 連接線的四種樣式 ⭐ 重要特性

連接線支援四種不同的樣式，適用於不同的使用場景：

| arrowType | 樣式 | 用途說明 |
|-----------|------|----------|
| `'single'` | 單向箭頭 → | 表示單向關係、因果、流程 |
| `'double'` | 雙向箭頭 ↔ | 表示雙向關係、互相影響 |
| `'none'` | 無箭頭實線 — | 表示關聯、連結（無方向性） |
| `'dashed'` | 無箭頭虛線 ┄ | **專用於註解連接**：連接註解說明或小提示 |

#### 虛線連接的使用場景

**虛線（`'dashed'`）專門用於連接註解說明或小提示**，與主要概念的連接線做視覺區分：

```javascript
// 範例：使用虛線連接註解節點
{
    id: "conn-annotation-1",
    from: "node-main-concept",      // 主要概念
    to: "node-tip-or-note",         // 註解或小提示節點
    arrowType: "dashed",            // ← 使用虛線樣式
    label: "",
    color: "#95a5a6"                // 建議使用灰色，弱化視覺
}
```

### 節點作為註解說明 ⭐ 設計技巧

**節點不僅可以表達主要概念，也可以作為註解說明或小提示使用。**

#### 註解節點的特徵

- **顏色**：建議使用灰色（`#95a5a6`）或白色（`#ffffff`），與主要概念節點區分
- **連接**：使用**虛線（`'dashed'`）**連接到相關的主要概念
- **內容**：補充說明、小提示、備註、例外情況等

#### 範例：主概念 + 註解節點

```javascript
// 主要概念節點
{
    id: "node-main",
    x: 200, y: 100,
    width: 200, height: 100,
    text: "<b>核心概念</b><br>主要內容說明",
    color: "#3498db",        // 藍色 - 主要概念
    textAlign: "center",
    completed: false, abandoned: false, link: "",
    isLabel: false
},
// 註解節點
{
    id: "node-tip",
    x: 450, y: 80,
    width: 160, height: 60,
    text: "💡 小提示：這裡可以補充說明",
    color: "#95a5a6",        // 灰色 - 註解
    textAlign: "left",
    completed: false, abandoned: false, link: "",
    isLabel: false
}

// 使用虛線連接註解
{
    id: "conn-tip",
    from: "node-main",
    to: "node-tip",
    arrowType: "dashed",     // ← 虛線連接
    label: "",
    color: "#95a5a6"
}
```

### isLabel 屬性（⚠️ 所有節點必填）

`isLabel` 是每一個 Node 都必須明確寫出的欄位，**不論是一般節點還是標籤節點，都不可省略**。

> ❌ 常見錯誤：認為 `isLabel` 只有「標籤節點」才需要填，一般節點省略不寫 → 導致 Viewer 行為異常

#### isLabel 兩種值的含義

| 值 | 適用對象 | 樣式效果 |
|---|---------|----------|
| `false` | **所有一般節點**（必須明確寫出，不可省略） | 正常內邊距（padding: 10px）、最小寬高限制 |
| `true` | 標籤節點／拐點節點 | 緊湊內邊距（padding: 4px）、無最小寬高限制、內容貼近邊緣 |

#### CSS 樣式定義

當 `isLabel: true` 時，節點會套用 `.node-label-mode` 樣式：

```css
.node.node-label-mode {
    padding: 4px !important;
    width: auto !important;
    height: auto !important;
    min-width: unset !important;
    min-height: unset !important;
    white-space: nowrap;  /* 強制文字橫向排列，不換行 */
}
```

#### 使用場景

1. **連接線的註解標籤**：將節點放在連接線上，作為關係說明
2. **緊湊型提示**：需要小巧標籤的場合
3. **拐點標籤**：作為連接線的中繼點，同時顯示關係描述

#### 範例：標籤樣式節點

```javascript
// 一般節點
{
    id: "node-main",
    x: 100, y: 100,
    width: 200, height: 100,
    text: "<b>主要概念</b>",
    color: "#3498db",
    textAlign: "center",
    completed: false, abandoned: false, link: "",
    isLabel: false     // ← 一般節點樣式
},
// 標籤樣式節點（用於連接線註解）
{
    id: "node-label",
    x: 300, y: 150,
    width: 60, height: 30,
    text: "導致",
    color: "#e74c3c",
    textAlign: "center",
    completed: false, abandoned: false, link: "",
    isLabel: true      // ← 標籤樣式，緊湊呈現
}
```

#### Viewer 渲染邏輯

在 Viewer 的 `renderNode()` 中，需要檢查 `isLabel` 並套用對應樣式：

```javascript
renderNode(node) {
    const nodeEl = document.createElement('div');
    nodeEl.className = 'node';
    nodeEl.setAttribute('data-id', node.id);

    // 標籤樣式
    if (node.isLabel) {
        nodeEl.classList.add('node-label-mode');
    }

    // ... 其他渲染邏輯
}
```

### 節點作為連接線標籤（拐點技巧）⭐ 進階設計

> ⚠️ **使用前提**：拐點技巧是解決問題的手段，**不是預設選項**。
> 僅在以下情況才使用：
> - 連接線的直線路徑被其他節點阻擋，無法透過調整佈局解決
> - 需要讓連線產生視覺彎折，以提升可讀性
>
> 若調整節點位置即可淨空路徑，優先移動節點，不要加拐點。

**節點可以用來代替連接線的標籤**，這是一種進階的設計技巧。

> 📁 **完整範例**：`~/.claude/ConceptMap/examples/label-node-waypoint-example.html`

#### 使用場景與優點

1. **作為拐點**：連接線只能畫直線，利用標籤節點可以讓連線產生彎曲效果
2. **超連結功能**：節點本身可以設定超連結，若需要額外的功能說明或跳轉頁面，可以利用此特性

#### ⚠️ 設計規範（避免混淆）

為了讓標籤節點不被誤認為一般概念節點，**必須遵守以下規範**：

| 規範項目 | 說明 |
|---------|------|
| **樣式** | 必須設計成像連接線標籤的樣式（小尺寸、簡潔文字） |
| **尺寸** | 推薦 `width: 36, height: 22`（高度 < 40 會自動套用緊湊樣式） |
| **連接數量** | **只能有 2 條主要連接線**連接到此節點 |
| **可選註解** | 可額外增加虛線（`'dashed'`）連接註解節點 |
| **組合樣式** | 兩條連接線必須組合成以下四種標準樣式之一 |

#### CSS 緊湊樣式（自動套用）

當節點尺寸較小（`height < 40` 或 `width < 60`）時，Viewer 會自動套用緊湊樣式：

```css
/* 緊湊型標籤節點 - 用於連接線拐點 */
.node-compact {
    padding: 3px 6px;      /* 較小的內邊距，讓邊緣貼近文字 */
    min-width: unset;      /* 移除最小寬度限制 */
    min-height: unset;     /* 移除最小高度限制 */
    border-radius: 4px;    /* 較小的圓角 */
    font-size: 12px;       /* 較小的字體 */
}
```

#### 四種標準組合樣式（強制對稱）

標籤節點必須透過**兩條連線（一進一出）**來模擬一條帶有標籤或轉折的長連線，且必須遵守以下四種具名樣式之一：

| 樣式名稱 | 路徑 A（起點→標籤） | 路徑 B（標籤→終點） | 視覺效果 |
|---------|----------------|----------------|---------|
| **單向流動** | `arrowType: "none"` | `arrowType: "single"` | A ──── 標籤 ────→ B |
| **反向流動** | `arrowType: "single"` | `arrowType: "none"` | A ←─── 標籤 ──── B |
| **雙向互動** | `arrowType: "single"` | `arrowType: "single"` | A ←─── 標籤 ────→ B |
| **純關聯** | `arrowType: "none"` | `arrowType: "none"` | A ──── 標籤 ──── B |

> **判斷原則**：箭頭永遠在「指向真正目標」的那一側，進入拐點的那側必須是 `none`（雙向互動除外）。

#### 範例：標籤節點作為拐點（單向 A→B）

```javascript
// 節點 A（起點）
{
    id: "node-A",
    x: 100, y: 100,
    width: 150, height: 80,
    text: "<b>概念 A</b>",
    color: "#3498db",
    textAlign: "center",
    completed: false, abandoned: false, link: "",
    isLabel: false
},
// 標籤節點（拐點）- 設計成標籤樣式
{
    id: "node-label",
    x: 300, y: 200,
    width: 36, height: 22,          // ← 緊湊尺寸，貼住文字
    text: "導致",                    // ← 簡潔文字
    color: "#e74c3c",               // ← 紅色，與連接線標籤一致
    textAlign: "center",
    completed: false, abandoned: false,
    link: "./detail.html",          // ← 可選：點擊跳轉到詳細說明
    isLabel: true                   // ← 啟用標籤樣式，緊湊顯示
},
// 節點 B（終點）
{
    id: "node-B",
    x: 500, y: 300,
    width: 150, height: 80,
    text: "<b>概念 B</b>",
    color: "#e74c3c",
    textAlign: "center",
    completed: false, abandoned: false, link: "",
    isLabel: false
}

// 連接線組合：單向 A→B 樣式
// A ─ 標籤（無箭頭，指向標籤）
{
    id: "conn-1",
    from: "node-A",
    to: "node-label",
    arrowType: "none",              // ← 無箭頭
    label: "",
    color: "#2c3e50"
},
// 標籤 → B（單向箭頭，從標籤出發）
{
    id: "conn-2",
    from: "node-label",
    to: "node-B",
    arrowType: "single",            // ← 單向箭頭
    label: "",
    color: "#2c3e50"
}

// 可選：標籤節點的註解（使用虛線，不計入主要連接）
{
    id: "conn-note",
    from: "node-label",
    to: "node-explanation",
    arrowType: "dashed",            // ← 虛線
    label: "",
    color: "#95a5a6"
}
```

#### 視覺效果示意

```
                    ┌─────────┐
                    │ 詳細說明 │ ← 註解節點（可選，虛線連接）
                    └────┬────┘
                         ┊ (dashed)
┌─────────┐         ┌────┴───┐         ┌─────────┐
│ 概念 A  │─────────│  導致  │────────→│ 概念 B  │
└─────────┘  (none) └────────┘ (single) └─────────┘
                     ↑ 標籤節點（可點擊跳轉）
```

### 框架連接線（簡化連接數量）⭐ 重要特性

**連接線可以連接到框架（Frame）**，這樣可以大幅簡化複雜概念圖的連接線數量。

#### 使用場景

當多個節點需要連接到一組相關概念時，可以：
- ❌ **不推薦**：連接到框架內的每個節點（產生大量連接線）
- ✅ **推薦**：直接連接到框架（簡潔清晰）

#### 範例對比

```json
// ❌ 不推薦：連接到框架內的每個節點
"connections": [
  { "from": "external-node", "to": "frame-node-1", ... },
  { "from": "external-node", "to": "frame-node-2", ... },
  { "from": "external-node", "to": "frame-node-3", ... }
  // 需要 3 條連接線！
]

// ✅ 推薦：直接連接到框架
"connections": [
  {
    "from": "external-node",
    "to": "target-frame",     // ← 直接連接框架
    "arrowType": "single",
    "label": "影響整個區塊",
    "color": "#2c3e50"
  }
  // 只需要 1 條連接線！
]
```

#### 技術細節

連接線會自動計算框架的邊緣交點，與連接節點的方式完全相同：

```javascript
// MapViewer 會自動處理框架連接
const from = this.nodes.find(n => n.id === conn.from) ||
             this.frames.find(f => f.id === conn.from);
const to = this.nodes.find(n => n.id === conn.to) ||
           this.frames.find(f => f.id === conn.to);
```

### ⚠️ 連接線佈局規範（強制）

連接線在 Viewer 中永遠以**直線**呈現，無法自動繞路，因此佈局規劃直接決定可讀性。

#### 禁止穿越非目標節點（視線原則）

對於任何一條連接線 L（從節點 A 到節點 B）：

> **視線原則（Line of Sight）**：其直線路徑所經過的區域，不得與除了 A 和 B 以外的任何節點矩形相交。若連線的一部分被其他節點遮擋，或連線從其他節點上方壓過，即為「穿越」，必須消除。

```
❌ 錯誤：A ──────[C]────── B   （連線從節點 C 上方壓過）
✅ 正確：A ───────────────→ B   （視線路徑淨空）
               [C] 移至其他位置
```

**消除穿越的兩種標準手段**：

- **手段 A（偏移）**：調整障礙節點 C 的座標，使其移出 A–B 的視線路徑（優先選擇）
- **手段 B（拐點化）**：在 A 與 B 之間建立一個「標籤節點（Label Node）」作為拐點，將路徑拆解為 A→標籤 與 標籤→B，繞開 C（僅在無法偏移時使用）

##### ⛔ 常見佈局陷阱：A-B-C 共線（The Collinear Trap）

最容易違反「禁止穿越節點」規範的場景，是將節點排成垂直或水平清單，又稱為**「隔山打牛」**效應：

- **情境**：節點 A、B、C 依序排成一直線（如垂直清單）
- **問題**：若 A 直接連接到 C，連接線必然穿過位於中間的 B
- **解決方案**：

| 方案 | 說明 | 適用場景 |
|------|------|---------|
| **偏移佈局** | 將 B 向左／右（或上／下）偏移，讓 A→C 路徑淨空 | 需保留 A-B-C 的層次結構時 |
| **並排佈局** | 將 B 與 C 改為並排，讓 A 分別直接連接 B 與 C（推薦）| 多數情況下更清晰直觀 |

```
❌ 錯誤（隔山打牛）：
  [A]
   |
  [B]   ← A→C 的連接線必然穿越 B
   |
  [C]

✅ 正確（並排佈局）：        ✅ 正確（偏移佈局）：
  [A]                        [A] ────────→ [C]
  / \                         |
[B] [C]                      [B]（向右偏移）
```

> **生成前自我檢查**：對每一條連接線問自己：「起點到終點之間，有沒有其他節點擋在直線上？」

#### 拐點連接線的箭頭規則

使用拐點節點中繼時，**進入拐點的那一側必須是無箭頭直線（`none`）**，箭頭只能出現在離開拐點、指向真正目標的那一側。這樣才能避免拐點被誤認為流程終點。

```
✅ 正確：[A] ────(none)──── [拐點] ──(single)──→ [B]   A 流向 B
✅ 正確：[A] ←──(single)── [拐點] ────(none)──── [B]   B 流向 A
❌ 錯誤：[A] ──(single)──→ [拐點] ──(single)──→ [B]   拐點看起來像 A 的終點
```

> 雙向（`double`）情境例外：兩側各帶一個箭頭向外指，視覺上對稱，不會造成混淆。

#### 禁止連接線重合（交叉可接受，重合不可）

**交叉**（兩條連接線相交於一點）是允許的，複雜概念圖難以完全避免。

**重合**（兩條連接線沿相同路徑並行或完全疊合）是禁止的，閱讀者無法分辨兩條線：

| 情境 | 判斷 | 正確做法 |
|------|------|---------|
| 兩條線在某點相交 | ✅ 交叉，允許 | 無需處理 |
| 兩節點間有兩條連接線 | ❌ 重合，禁止 | 若需雙向，改用 `arrowType: "double"` |
| 兩條連接線走向平行且緊鄰 | ❌ 視覺重合，禁止 | 調整節點位置讓路徑明顯分開 |

#### 佈局規劃流程

1. 先列出所有連接關係（from → to）
2. 依連接關係決定節點的相對位置，讓連線盡可能不交叉
3. 檢查**「隔山打牛」**：確認任意兩連接節點之間沒有「中間人」阻擋直線路徑
4. 確認任意兩節點之間沒有重複連接線、沒有路徑重合
5. 若有無法避免的阻擋，補上拐點節點，並遵守箭頭方向規則

---

### 完整範例

```json
{
  "title": "範例概念圖",
  "data": {
    "nodes": [
      {
        "id": "node-1702345678901",
        "x": 150,
        "y": 100,
        "width": 200,
        "height": 100,
        "text": "<b>核心概念</b><br>這是主要的想法",
        "color": "#3498db",
        "textAlign": "center",
        "completed": false,
        "abandoned": false,
        "link": "",
        "isLabel": false
      }
    ],
    "frames": [
      {
        "id": "frame-1702345678901",
        "x": 100,
        "y": 50,
        "width": 600,
        "height": 200,
        "label": "主要區塊",
        "borderColor": "#95a5a6",
        "bgColor": "rgba(149, 165, 166, 0.1)",
        "zIndex": 0,
        "completed": false,
        "abandoned": false
      }
    ],
    "connections": [
      {
        "id": "conn-1702345678901",
        "from": "node-1702345678901",
        "to": "node-1702345678902",
        "arrowType": "single",
        "label": "導致",
        "color": "#2c3e50"
      },
      {
        "id": "conn-1702345678902",
        "from": "node-1702345678903",
        "to": "frame-1702345678901",
        "arrowType": "single",
        "label": "影響整個區塊",
        "color": "#e74c3c"
      }
    ]
  }
}
```

---

## ⚠️ SVG 連接線處理規範

### 核心原則

**Canvas 和 SVG 的變換處理不同**：
- **Canvas**：使用 CSS `transform` 進行縮放和平移
- **SVG**：**不使用** CSS `transform`，而是在繪製時手動計算座標

### ⚠️ 必須使用 DOM 元素的實際尺寸

**為什麼不能直接使用數據中的 width/height？**

CSS 會影響元素的實際渲染尺寸：
- `padding` 會增加元素實際大小
- `min-width` / `min-height` 會覆蓋設定的尺寸
- `box-sizing` 會影響尺寸計算方式

**正確做法**：
1. 在 `renderNode()` 和 `renderFrame()` 中設置 `data-id` 屬性
2. 在 `updateConnections()` 中透過 `querySelector` 查詢 DOM 元素
3. 使用 `offsetWidth` / `offsetHeight` 獲取實際渲染尺寸

```javascript
// 查詢 DOM 元素
const fromEl = this.canvas.querySelector(`[data-id="${fromNode.id}"]`);

// 使用實際尺寸（若查詢失敗則退回數據尺寸）
const fromWidth = fromEl ? fromEl.offsetWidth : fromNode.width;
const fromHeight = fromEl ? fromEl.offsetHeight : fromNode.height;
```

**效果**：連接線會精準連接到節點的視覺邊緣，而非數據定義的邊緣

### 為什麼不能對 SVG 使用 transform？

當 SVG 層也使用 transform 時，會導致連接線座標被**二次變換**：

```
節點原始座標：(100, 100)
Canvas transform: scale(2) → 節點顯示在 (200, 200) ✓

連接線使用原始座標 (100, 100)
SVG 也 transform: scale(2) → 連接線顯示在 (200, 200) ✓

但當縮放改變時：
Canvas transform: scale(1.5) → 節點在 (150, 150)
SVG transform: scale(1.5) → 連接線在 (150, 150)
但由於座標計算時機不同，連接線會偏移！❌
```

### ❌ 錯誤範例

```javascript
render() {
    this.canvas.style.transform =
        `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
    this.svgLayer.style.transform =
        `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`; // ❌ 錯誤！
}

updateConnections() {
    // 使用原始座標
    line.setAttribute('x1', fromEdge.x);  // ❌ 會被 SVG transform 二次變換
    line.setAttribute('y1', fromEdge.y);
}
```

**問題**：縮放時連接線會偏移，無法正確連接節點

### ✅ 正確範例

```javascript
render() {
    this.canvas.style.transform =
        `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
    // ✅ SVG 層不設置 transform
}

updateConnections() {
    // ⚠️ 關鍵：查詢 DOM 元素以獲取實際渲染尺寸
    const fromEl = this.canvas.querySelector(`[data-id="${fromNode.id}"]`);
    const toEl = this.canvas.querySelector(`[data-id="${toNode.id}"]`);

    // ⚠️ 使用 DOM 的實際尺寸（處理 CSS padding、min-width/height 等影響）
    const fromWidth = fromEl ? fromEl.offsetWidth : fromNode.width;
    const fromHeight = fromEl ? fromEl.offsetHeight : fromNode.height;
    const toWidth = toEl ? toEl.offsetWidth : toNode.width;
    const toHeight = toEl ? toEl.offsetHeight : toNode.height;

    const fromCenter = {
        x: fromNode.x + fromWidth / 2,
        y: fromNode.y + fromHeight / 2
    };
    const toCenter = {
        x: toNode.x + toWidth / 2,
        y: toNode.y + toHeight / 2
    };

    // 建立包含實際尺寸的 rect 物件
    const fromRect = { x: fromNode.x, y: fromNode.y, width: fromWidth, height: fromHeight };
    const toRect = { x: toNode.x, y: toNode.y, width: toWidth, height: toHeight };

    // 計算邊緣交點（使用實際尺寸）
    const fromEdge = this.getEdgePoint(fromRect, fromCenter, toCenter);
    const toEdge = this.getEdgePoint(toRect, toCenter, fromCenter);

    // ✅ 手動應用 scale 和 offset
    const x1 = fromEdge.x * this.scale + this.offsetX;
    const y1 = fromEdge.y * this.scale + this.offsetY;
    const x2 = toEdge.x * this.scale + this.offsetX;
    const y2 = toEdge.y * this.scale + this.offsetY;

    line.setAttribute('x1', x1);
    line.setAttribute('y1', y1);
    line.setAttribute('x2', x2);
    line.setAttribute('y2', y2);

    // ✅ 標籤座標也要使用變換後的座標
    if (conn.label) {
        const midX = (x1 + x2) / 2;
        const midY = (y1 + y2) / 2;
        text.setAttribute('x', midX);
        text.setAttribute('y', midY - 5);
    }
}
```

### 檢查方法

生成 Map 檔後，執行以下檢查：

```bash
# 搜尋 SVG transform（應該沒有任何結果）
grep "svgLayer.style.transform" your-map.html
# 應該返回：無結果 ✓

# 搜尋 Canvas transform（應該有 1 個結果）
grep "canvas.style.transform" your-map.html
# 應該返回：1 個結果在 render() 方法中 ✓
```

---

## 🖼️ Favicon 處理

### 標準配置

```html
<link rel="icon" type="image/png" href="./concept-crafter_icon.png">
```

### 檔案要求

| 項目 | 要求 |
|------|------|
| 檔案名稱 | `concept-crafter_icon.png` |
| 檔案位置 | 與 Map 檔在同一目錄 |
| 檔案格式 | PNG |
| 建議尺寸 | 128x128 像素 |
| 來源 | `.claude/ConceptMap/concept-crafter_icon.png` |

### 使用內定圖標（推薦）

```bash
# 複製內定圖標到專案目錄
cp .claude/ConceptMap/concept-crafter_icon.png ./project-dir/

# Windows
copy .claude\ConceptMap\concept-crafter_icon.png .\project-dir\
```

### 使用自訂圖標

若使用者提供自訂圖標：
1. 確認是 PNG 格式
2. 重新命名為 `concept-crafter_icon.png`
3. 放置在與 Map 檔相同目錄
4. 建議（非強制）調整為 128x128

---

## 📄 HTML 檔案結構

```html
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <title>{{TITLE}} - Concept Map Viewer</title>

    <!-- 必須：Favicon 引用 -->
    <link rel="icon" type="image/png" href="./concept-crafter_icon.png">

    <!-- 必須：Viewer 樣式 -->
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;  /* 重要！防止連接線深入節點 */
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            overflow: hidden;
            background: #f0f0f0;
        }

        #container {
            width: 100vw;
            height: 100vh;
            overflow: hidden;
            position: relative;
            cursor: grab;
        }

        #container.grabbing {
            cursor: grabbing;
        }

        #canvas {
            width: 100%;
            height: 100%;
            transform-origin: 0 0;
            position: relative;
        }

        svg {
            position: absolute;
            top: 0;
            left: 0;
            pointer-events: none;
            z-index: 50;
            overflow: visible;
        }

        .node {
            position: absolute;
            background: #3498db;
            /* color 由 JS getContrastColor() 動態設定，不在此寫死 */
            padding: 15px;
            border-radius: 8px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.15);
            user-select: none;
            pointer-events: none;  /* ⚠️ v4.8 關鍵：讓觸控/滑鼠事件穿透到 Container */
        }

        .node-content {
            width: 100%;
            height: 100%;
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            pointer-events: none;
        }

        .node-content * {
            margin: 0;
            pointer-events: none;
        }

        .node-content a {
            color: navy;
            text-decoration: underline;
            pointer-events: auto;  /* ⚠️ 唯一例外：文字中的超連結可點擊 */
        }

        /* 緊湊型標籤節點 - 用於連接線拐點（基於尺寸自動套用） */
        .node-compact {
            padding: 3px 6px;
            min-width: unset;
            min-height: unset;
            border-radius: 4px;
            font-size: 12px;
        }

        /* 標籤樣式節點 - 基於 isLabel 屬性套用 */
        .node.node-label-mode {
            padding: 4px !important;
            width: auto !important;
            height: auto !important;
            min-width: unset !important;
            min-height: unset !important;
            white-space: nowrap;  /* 強制文字橫向排列，不換行 */
        }

        .frame {
            position: absolute;
            border: 2px dashed #95a5a6;
            background: rgba(149, 165, 166, 0.1);
            border-radius: 4px;
            pointer-events: none;  /* ⚠️ v4.8 關鍵：讓觸控/滑鼠事件穿透到 Container */
        }

        .status-badge {
            position: absolute;
            top: 8px;
            right: 8px;
            font-size: 16px;
            font-weight: bold;
            padding: 4px 8px;
            border-radius: 4px;
        }

        .status-completed {
            background: #27ae60;
            color: white;
        }

        .status-abandoned {
            background: #e74c3c;
            color: white;
        }
    </style>

    <!-- 必須：Map 數據 -->
    <script id="map-data" type="application/json">
    {{MAP_DATA_JSON}}
    </script>
</head>
<body>
    <div id="map-title">{{TITLE}}</div>
    <div id="container">
        <svg id="svg-layer"></svg>
        <div id="canvas"></div>
    </div>

    <!-- 必須：Map Viewer 腳本（v4.8 全域 Hit Test 方案） -->
    <script>
        class MapViewer {
            constructor() {
                this.container = document.getElementById('container');
                this.canvas = document.getElementById('canvas');
                this.svgLayer = document.getElementById('svg-layer');

                this.data = JSON.parse(document.getElementById('map-data').textContent);
                this.nodes = this.data.data.nodes || [];
                this.frames = this.data.data.frames || [];
                this.connections = this.data.data.connections || [];

                this.scale = 1;
                this.offsetX = 0;
                this.offsetY = 0;
                this.isPanning = false;
                this.panStart = { x: 0, y: 0 };
                this.isMoved = false;  // ⚠️ 關鍵：區分拖曳和點擊

                // 觸控相關狀態
                this.touches = [];
                this.lastTouchDistance = 0;

                this.init();
            }

            init() {
                this.centerContent();
                this.updateSVGSize();
                window.addEventListener('resize', () => this.updateSVGSize());

                // ===== 滑鼠事件 =====
                this.container.addEventListener('mousedown', (e) => {
                    if (e.button === 0) {
                        this.isPanning = true;
                        this.isMoved = false;
                        this.panStart = { x: e.clientX, y: e.clientY };
                        this.container.classList.add('grabbing');
                    }
                });

                // ⚠️ 使用 Container 的 click 事件 + Hit Test 處理節點點擊
                this.container.addEventListener('click', (e) => this.handleNodeClick(e));

                window.addEventListener('mousemove', (e) => {
                    if (this.isPanning) {
                        this.offsetX += e.clientX - this.panStart.x;
                        this.offsetY += e.clientY - this.panStart.y;
                        this.panStart = { x: e.clientX, y: e.clientY };
                        this.isMoved = true;
                        this.render();
                    } else {
                        this.updateCursor(e);
                    }
                });

                window.addEventListener('mouseup', () => {
                    this.isPanning = false;
                    this.container.classList.remove('grabbing');
                });

                this.container.addEventListener('wheel', (e) => {
                    e.preventDefault();
                    const delta = e.deltaY > 0 ? 0.9 : 1.1;
                    const rect = this.container.getBoundingClientRect();
                    const mouseX = e.clientX - rect.left;
                    const mouseY = e.clientY - rect.top;

                    this.offsetX = mouseX - (mouseX - this.offsetX) * delta;
                    this.offsetY = mouseY - (mouseY - this.offsetY) * delta;
                    this.scale *= delta;
                    this.scale = Math.max(0.1, Math.min(5, this.scale));
                    this.render();
                }, { passive: false });

                // ===== 觸控事件 =====
                this.container.addEventListener('touchstart', (e) => {
                    this.touches = Array.from(e.touches);

                    if (this.touches.length === 1) {
                        this.isPanning = true;
                        this.isMoved = false;
                        this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
                    } else if (this.touches.length === 2) {
                        this.isPanning = false;
                        const dx = this.touches[0].clientX - this.touches[1].clientX;
                        const dy = this.touches[0].clientY - this.touches[1].clientY;
                        this.lastTouchDistance = Math.sqrt(dx * dx + dy * dy);
                    }
                }, { passive: false });

                this.container.addEventListener('touchmove', (e) => {
                    e.preventDefault();
                    this.touches = Array.from(e.touches);

                    if (this.touches.length === 1 && this.isPanning) {
                        this.offsetX += this.touches[0].clientX - this.panStart.x;
                        this.offsetY += this.touches[0].clientY - this.panStart.y;
                        this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
                        this.isMoved = true;
                        this.render();
                    } else if (this.touches.length === 2) {
                        const dx = this.touches[0].clientX - this.touches[1].clientX;
                        const dy = this.touches[0].clientY - this.touches[1].clientY;
                        const distance = Math.sqrt(dx * dx + dy * dy);

                        if (this.lastTouchDistance > 0) {
                            const delta = distance / this.lastTouchDistance;
                            const rect = this.container.getBoundingClientRect();
                            const centerX = (this.touches[0].clientX + this.touches[1].clientX) / 2 - rect.left;
                            const centerY = (this.touches[0].clientY + this.touches[1].clientY) / 2 - rect.top;

                            this.offsetX = centerX - (centerX - this.offsetX) * delta;
                            this.offsetY = centerY - (centerY - this.offsetY) * delta;
                            this.scale *= delta;
                            this.scale = Math.max(0.1, Math.min(5, this.scale));
                            this.render();
                        }

                        this.lastTouchDistance = distance;
                    }
                }, { passive: false });

                this.container.addEventListener('touchend', (e) => {
                    this.touches = Array.from(e.touches);

                    if (this.touches.length === 0) {
                        this.isPanning = false;
                        this.lastTouchDistance = 0;
                    } else if (this.touches.length === 1) {
                        this.isPanning = true;
                        this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
                        this.lastTouchDistance = 0;
                    }
                });

                this.render();
            }

            // ⭐ Hit Test：動態游標更新
            updateCursor(e) {
                const rect = this.container.getBoundingClientRect();
                const wx = (e.clientX - rect.left - this.offsetX) / this.scale;
                const wy = (e.clientY - rect.top - this.offsetY) / this.scale;

                let hitLink = false;
                for (let i = this.nodes.length - 1; i >= 0; i--) {
                    const node = this.nodes[i];
                    if (wx >= node.x && wx <= node.x + node.width &&
                        wy >= node.y && wy <= node.y + node.height) {
                        if (node.link && node.link.trim()) {
                            hitLink = true;
                        }
                        break;
                    }
                }
                this.container.style.cursor = hitLink ? 'pointer' : (this.isPanning ? 'grabbing' : 'grab');
            }

            // ⭐ Hit Test：節點點擊處理
            handleNodeClick(e) {
                if (this.isMoved) return;
                if (e.target.closest('a')) return;

                const rect = this.container.getBoundingClientRect();
                const wx = (e.clientX - rect.left - this.offsetX) / this.scale;
                const wy = (e.clientY - rect.top - this.offsetY) / this.scale;

                for (let i = this.nodes.length - 1; i >= 0; i--) {
                    const node = this.nodes[i];
                    if (wx >= node.x && wx <= node.x + node.width &&
                        wy >= node.y && wy <= node.y + node.height) {

                        if (node.link && node.link.trim()) {
                            const link = node.link.trim();
                            const isProjectHtml = !link.match(/^[a-zA-Z]+:\/\//) &&
                                                  !link.match(/^\//) &&
                                                  !link.match(/^[a-zA-Z]:[\/\\]/) &&
                                                  link.match(/\.html?$/i);

                            if (isProjectHtml) {
                                window.location.href = link;
                            } else {
                                window.open(link, '_blank', 'noopener,noreferrer');
                            }
                        }
                        break;
                    }
                }
            }

            // 內容自動置中
            centerContent() {
                if (this.nodes.length === 0 && this.frames.length === 0) return;

                let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;

                this.nodes.forEach(node => {
                    minX = Math.min(minX, node.x);
                    minY = Math.min(minY, node.y);
                    maxX = Math.max(maxX, node.x + node.width);
                    maxY = Math.max(maxY, node.y + node.height);
                });

                this.frames.forEach(frame => {
                    minX = Math.min(minX, frame.x);
                    minY = Math.min(minY, frame.y);
                    maxX = Math.max(maxX, frame.x + frame.width);
                    maxY = Math.max(maxY, frame.y + frame.height);
                });

                if (minX === Infinity) return;

                const contentCenterX = (minX + maxX) / 2;
                const contentCenterY = (minY + maxY) / 2;

                const rect = this.container.getBoundingClientRect();
                const viewportCenterX = rect.width / 2;
                const viewportCenterY = rect.height / 2;

                this.offsetX = viewportCenterX - contentCenterX;
                this.offsetY = viewportCenterY - contentCenterY;
            }

            // 根據背景亮度自動回傳深色或淺色文字色（sRGB luminance 公式）
            getContrastColor(hex) {
                const h = (hex || '#3498db').replace(/^#/, '');
                const r = parseInt(h.substr(0, 2), 16);
                const g = parseInt(h.substr(2, 2), 16);
                const b = parseInt(h.substr(4, 2), 16);
                const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
                return luminance > 0.5 ? '#1a1a1a' : '#ffffff';
            }

            render() {
                this.canvas.innerHTML = '';
                this.svgLayer.innerHTML = '';

                // 渲染框架（按 zIndex 排序）
                const sortedFrames = [...this.frames].sort((a, b) =>
                    (a.zIndex || 0) - (b.zIndex || 0)
                );
                sortedFrames.forEach(frame => this.renderFrame(frame));

                // 渲染節點
                this.nodes.forEach(node => this.renderNode(node));

                // 渲染連接線
                this.updateConnections();

                this.canvas.style.transform =
                    `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
                // ⚠️ 注意：SVG 層不使用 transform，連接線座標在 updateConnections() 中手動計算

                // 後置安全檢查：確保每個節點都有正確的文字對比色
                this.canvas.querySelectorAll('.node').forEach(el => {
                    if (!el.style.color) {
                        const bg = el.style.background || el.style.backgroundColor || '#3498db';
                        el.style.color = this.getContrastColor(bg);
                    }
                });
            }

            renderNode(node) {
                const nodeEl = document.createElement('div');
                nodeEl.className = 'node';
                nodeEl.setAttribute('data-id', node.id);  // ⚠️ 必須：用於連接線查詢 DOM 元素

                // ⚠️ 標籤樣式（基於 isLabel 屬性）
                if (node.isLabel) {
                    nodeEl.classList.add('node-label-mode');
                }

                // 小節點自動套用緊湊樣式（基於尺寸，與 isLabel 獨立）
                if (node.height < 40 || node.width < 60) {
                    nodeEl.classList.add('node-compact');
                }

                nodeEl.style.left = node.x + 'px';
                nodeEl.style.top = node.y + 'px';
                nodeEl.style.width = node.width + 'px';
                nodeEl.style.height = node.height + 'px';
                const nodeBg = node.color || '#3498db';
                nodeEl.style.background = nodeBg;
                nodeEl.style.color = this.getContrastColor(nodeBg);
                nodeEl.style.textAlign = node.textAlign || 'center';
                nodeEl.innerHTML = '<div class="node-content">' + (node.text || '') + '</div>';

                // 處理文字中的超連結
                const links = nodeEl.querySelectorAll('a');
                links.forEach(link => {
                    const href = link.getAttribute('href');
                    if (href) {
                        const isProjectHtml = !href.match(/^[a-zA-Z]+:\/\//) &&
                                              !href.match(/^\//) &&
                                              !href.match(/^[a-zA-Z]:[\/\\]/) &&
                                              href.match(/\.html?$/i);

                        if (!isProjectHtml) {
                            link.setAttribute('target', '_blank');
                            link.setAttribute('rel', 'noopener noreferrer');
                        }
                    }
                });

                if (node.completed || node.abandoned) {
                    const badge = document.createElement('div');
                    badge.className = `status-badge ${node.completed ? 'status-completed' : 'status-abandoned'}`;
                    badge.textContent = node.completed ? '\u2714' : '\u2717';
                    nodeEl.appendChild(badge);
                }

                this.canvas.appendChild(nodeEl);
            }

            renderFrame(frame) {
                const frameEl = document.createElement('div');
                frameEl.className = 'frame';
                frameEl.setAttribute('data-id', frame.id);  // ⚠️ 必須：用於連接線查詢 DOM 元素
                frameEl.style.left = frame.x + 'px';
                frameEl.style.top = frame.y + 'px';
                frameEl.style.width = frame.width + 'px';
                frameEl.style.height = frame.height + 'px';
                frameEl.style.borderColor = frame.borderColor || '#95a5a6';
                frameEl.style.background = frame.bgColor || 'rgba(149, 165, 166, 0.1)';
                frameEl.style.zIndex = frame.zIndex || 0;

                if (frame.label) {
                    const label = document.createElement('div');
                    label.style.position = 'absolute';
                    label.style.top = '5px';
                    label.style.left = '10px';
                    label.style.fontSize = '14px';
                    label.style.fontWeight = '600';
                    label.style.color = '#7f8c8d';
                    label.textContent = frame.label;
                    frameEl.appendChild(label);
                }

                this.canvas.appendChild(frameEl);
            }

            updateConnections() {
                // 創建箭頭標記
                const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');

                // ⚠️ 關鍵：為每種顏色創建專屬箭頭標記，確保箭頭與連接線顏色一致
                const colors = new Set();
                this.connections.forEach(conn => {
                    colors.add(conn.color || '#2c3e50');
                });

                // ⚠️ 必須搭配 viewBox 才能讓 markerWidth 動態縮放生效
                // 視覺大小 = markerHeight × strokeWidth = 6 × 2 = 12px（scale=1 時）
                const markerW = (9 * this.scale).toString();
                const markerH = (6 * this.scale).toString();

                colors.forEach(color => {
                    const colorId = color.replace('#', '');

                    // Single arrow marker
                    const markerSingle = document.createElementNS('http://www.w3.org/2000/svg', 'marker');
                    markerSingle.setAttribute('id', `arrowhead-single-${colorId}`);
                    markerSingle.setAttribute('viewBox', '0 0 9 6');
                    markerSingle.setAttribute('markerWidth', markerW);
                    markerSingle.setAttribute('markerHeight', markerH);
                    markerSingle.setAttribute('refX', '9');
                    markerSingle.setAttribute('refY', '3');
                    markerSingle.setAttribute('orient', 'auto');
                    const pathSingle = document.createElementNS('http://www.w3.org/2000/svg', 'path');
                    pathSingle.setAttribute('d', 'M0,0 L0,6 L9,3 z');
                    pathSingle.setAttribute('fill', color);
                    markerSingle.appendChild(pathSingle);
                    defs.appendChild(markerSingle);

                    // Double arrow marker
                    const markerDouble = document.createElementNS('http://www.w3.org/2000/svg', 'marker');
                    markerDouble.setAttribute('id', `arrowhead-double-${colorId}`);
                    markerDouble.setAttribute('viewBox', '0 0 9 6');
                    markerDouble.setAttribute('markerWidth', markerW);
                    markerDouble.setAttribute('markerHeight', markerH);
                    markerDouble.setAttribute('refX', '0');
                    markerDouble.setAttribute('refY', '3');
                    markerDouble.setAttribute('orient', 'auto');
                    const pathDouble = document.createElementNS('http://www.w3.org/2000/svg', 'path');
                    pathDouble.setAttribute('d', 'M9,0 L9,6 L0,3 z');
                    pathDouble.setAttribute('fill', color);
                    markerDouble.appendChild(pathDouble);
                    defs.appendChild(markerDouble);
                });

                this.svgLayer.appendChild(defs);

                // 繪製連接線（⚠️ 支援節點和框架）
                this.connections.forEach(conn => {
                    // ⚠️ 重要：from 和 to 可以是節點或框架
                    const fromNode = this.nodes.find(n => n.id === conn.from) ||
                                     this.frames.find(f => f.id === conn.from);
                    const toNode = this.nodes.find(n => n.id === conn.to) ||
                                   this.frames.find(f => f.id === conn.to);
                    if (!fromNode || !toNode) return;

                    // ⚠️ 關鍵：查詢 DOM 元素以獲取實際渲染尺寸
                    const fromEl = this.canvas.querySelector(`[data-id="${fromNode.id}"]`);
                    const toEl = this.canvas.querySelector(`[data-id="${toNode.id}"]`);

                    // ⚠️ 使用 DOM 的實際尺寸（處理 CSS padding、min-width/height 等影響）
                    const fromWidth = fromEl ? fromEl.offsetWidth : fromNode.width;
                    const fromHeight = fromEl ? fromEl.offsetHeight : fromNode.height;
                    const toWidth = toEl ? toEl.offsetWidth : toNode.width;
                    const toHeight = toEl ? toEl.offsetHeight : toNode.height;

                    // 計算節點中心（使用實際尺寸）
                    const fromCenter = {
                        x: fromNode.x + fromWidth / 2,
                        y: fromNode.y + fromHeight / 2
                    };
                    const toCenter = {
                        x: toNode.x + toWidth / 2,
                        y: toNode.y + toHeight / 2
                    };

                    // 建立包含實際尺寸的 rect 物件
                    const fromRect = { x: fromNode.x, y: fromNode.y, width: fromWidth, height: fromHeight };
                    const toRect = { x: toNode.x, y: toNode.y, width: toWidth, height: toHeight };

                    // 計算邊緣交點（使用實際尺寸）
                    const fromEdge = this.getEdgePoint(fromRect, fromCenter, toCenter);
                    const toEdge = this.getEdgePoint(toRect, toCenter, fromCenter);

                    // ⚠️ 關鍵：手動應用 scale 和 offset
                    const x1 = fromEdge.x * this.scale + this.offsetX;
                    const y1 = fromEdge.y * this.scale + this.offsetY;
                    const x2 = toEdge.x * this.scale + this.offsetX;
                    const y2 = toEdge.y * this.scale + this.offsetY;

                    const lineColor = conn.color || '#2c3e50';
                    const colorId = lineColor.replace('#', '');

                    // 創建連接線
                    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
                    line.setAttribute('x1', x1);
                    line.setAttribute('y1', y1);
                    line.setAttribute('x2', x2);
                    line.setAttribute('y2', y2);
                    line.setAttribute('stroke', lineColor);
                    line.setAttribute('stroke-width', '2');

                    // ⚠️ 關鍵：根據 arrowType 設置樣式
                    if (conn.arrowType === 'single') {
                        line.setAttribute('marker-end', `url(#arrowhead-single-${colorId})`);
                    } else if (conn.arrowType === 'double') {
                        line.setAttribute('marker-start', `url(#arrowhead-double-${colorId})`);
                        line.setAttribute('marker-end', `url(#arrowhead-single-${colorId})`);
                    } else if (conn.arrowType === 'dashed') {
                        // 虛線樣式，無箭頭，用於註解連接
                        line.setAttribute('stroke-dasharray', '8,4');
                    }
                    // 'none' 類型：無箭頭實線，不需要額外設置

                    this.svgLayer.appendChild(line);

                    // 標籤（使用變換後的座標）
                    if (conn.label) {
                        const midX = (x1 + x2) / 2;
                        const midY = (y1 + y2) / 2;
                        const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
                        text.setAttribute('x', midX);
                        text.setAttribute('y', midY - 5);
                        text.setAttribute('text-anchor', 'middle');
                        text.setAttribute('font-size', '12');
                        text.setAttribute('class', 'connection-label');
                        text.textContent = conn.label;
                        this.svgLayer.appendChild(text);
                    }
                });
            }

            // ⚠️ 重要：計算從矩形中心到邊緣的交點
            // 注意：不使用 Math.max 限制最小尺寸，以支援小型標籤節點
            getEdgePoint(rect, center, target) {
                const dx = target.x - center.x;
                const dy = target.y - center.y;

                if (dx === 0 && dy === 0) {
                    return { x: center.x, y: center.y };
                }

                const halfWidth = rect.width / 2;
                const halfHeight = rect.height / 2;

                let t;
                if (Math.abs(dx) * halfHeight > Math.abs(dy) * halfWidth) {
                    t = halfWidth / Math.abs(dx);
                } else {
                    t = halfHeight / Math.abs(dy);
                }

                return {
                    x: center.x + dx * t,
                    y: center.y + dy * t
                };
            }

            updateSVGSize() {
                const rect = this.container.getBoundingClientRect();
                this.svgLayer.setAttribute('width', rect.width);
                this.svgLayer.setAttribute('height', rect.height);
            }
        }

        new MapViewer();
    </script>
</body>
</html>
```

---

## 📱 觸控螢幕支援（v4.8 全域 Hit Test 方案）

### 核心功能

Map 檔必須支援觸控螢幕操作，確保在平板電腦和觸控螢幕電腦上正常使用：

| 手勢 | 功能 | 說明 |
|------|------|------|
| **單指拖動** | 平移畫布 | 與滑鼠拖動相同 |
| **雙指捏合** | 縮放畫布 | 向內捏合縮小，向外展開放大 |
| **點擊連結節點** | 導航 | 只有短按才觸發，拖動後不觸發 |

### ⭐ v4.8 核心設計：全域 Hit Test 方案

**問題**：使用 `touch-action: none` 或 `pointer-events: auto` 時，觸碰到節點/框架仍可能被攔截觸控事件。

**解決方案**：採用 Concept Crafter v2.7.4 的「全域 Hit Test」技術：

1. **所有元素設置 `pointer-events: none`**（節點、框架、連接線）
2. **觸控/滑鼠事件統一由 Container 處理**
3. **使用座標計算（Hit Test）偵測點擊**
4. **用 `isMoved` 標記區分「拖曳」和「點擊」**

```
┌─────────────────────────────────────────────────────┐
│  Container（接收所有觸控/滑鼠事件）                    │
│  ┌─────────────────────────────────────────────────┐│
│  │  Node (pointer-events: none)                    ││
│  │  ┌─────────────────────────────────────────────┐││
│  │  │  <a> 連結 (pointer-events: auto) ← 唯一例外  │││
│  │  └─────────────────────────────────────────────┘││
│  └─────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────┐│
│  │  Frame (pointer-events: none)                   ││
│  └─────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────┘
         ↓ 點擊事件                    ↓ 觸控事件
    handleNodeClick()              touchstart/move/end
    (Hit Test 判斷)                (平移/縮放)
```

### CSS 設定（必須）

```css
#container {
    width: 100vw;
    height: 100vh;
    overflow: hidden;
    position: relative;
    cursor: grab;
}

#container.grabbing {
    cursor: grabbing;
}

svg {
    position: absolute;
    top: 0;
    left: 0;
    pointer-events: none;  /* ⚠️ SVG 層不攔截事件 */
    z-index: 50;
}

.node {
    position: absolute;
    /* ... 其他樣式 ... */
    pointer-events: none;  /* ⚠️ 關鍵：讓觸控事件穿透 */
}

.node-content {
    width: 100%;
    height: 100%;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    pointer-events: none;  /* ⚠️ 內容也不攔截 */
}

.node-content * {
    margin: 0;
    pointer-events: none;  /* ⚠️ 所有子元素不攔截 */
}

.node-content a {
    color: navy;
    text-decoration: underline;
    pointer-events: auto;  /* ⚠️ 唯一例外：文字中的超連結可點擊 */
}

.frame {
    position: absolute;
    /* ... 其他樣式 ... */
    pointer-events: none;  /* ⚠️ 關鍵：讓觸控事件穿透 */
}
```

### Viewport Meta 標籤（必須）

```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
```

### JavaScript 完整實作

#### 狀態變數

```javascript
class MapViewer {
    constructor() {
        this.container = document.getElementById('container');
        this.canvas = document.getElementById('canvas');
        this.svgLayer = document.getElementById('svg-layer');

        this.data = JSON.parse(document.getElementById('map-data').textContent);
        this.nodes = this.data.data.nodes || [];
        this.frames = this.data.data.frames || [];
        this.connections = this.data.data.connections || [];

        this.scale = 1;
        this.offsetX = 0;
        this.offsetY = 0;
        this.isPanning = false;
        this.panStart = { x: 0, y: 0 };
        this.isMoved = false;  // ⚠️ 關鍵：區分拖曳和點擊

        // 觸控相關狀態
        this.touches = [];
        this.lastTouchDistance = 0;

        this.init();
    }
}
```

#### 事件監聽器設置

```javascript
init() {
    this.centerContent();  // 初始化時將內容置中
    this.updateSVGSize();
    window.addEventListener('resize', () => this.updateSVGSize());

    // ===== 滑鼠事件 =====
    this.container.addEventListener('mousedown', (e) => {
        if (e.button === 0) {
            this.isPanning = true;
            this.isMoved = false;  // ⚠️ 重置移動標記
            this.panStart = { x: e.clientX, y: e.clientY };
            this.container.classList.add('grabbing');
        }
    });

    // ⚠️ 使用 Container 的 click 事件 + Hit Test 處理節點點擊
    this.container.addEventListener('click', (e) => this.handleNodeClick(e));

    window.addEventListener('mousemove', (e) => {
        if (this.isPanning) {
            this.offsetX += e.clientX - this.panStart.x;
            this.offsetY += e.clientY - this.panStart.y;
            this.panStart = { x: e.clientX, y: e.clientY };
            this.isMoved = true;  // ⚠️ 標記為已移動
            this.render();
        } else {
            // ⚠️ 動態更新游標（因為節點是 pointer-events: none）
            this.updateCursor(e);
        }
    });

    window.addEventListener('mouseup', () => {
        this.isPanning = false;
        this.container.classList.remove('grabbing');
    });

    this.container.addEventListener('wheel', (e) => {
        e.preventDefault();
        const delta = e.deltaY > 0 ? 0.9 : 1.1;
        const rect = this.container.getBoundingClientRect();
        const mouseX = e.clientX - rect.left;
        const mouseY = e.clientY - rect.top;

        this.offsetX = mouseX - (mouseX - this.offsetX) * delta;
        this.offsetY = mouseY - (mouseY - this.offsetY) * delta;
        this.scale *= delta;
        this.scale = Math.max(0.1, Math.min(5, this.scale));
        this.render();
    }, { passive: false });

    // ===== 觸控事件 =====
    this.container.addEventListener('touchstart', (e) => {
        this.touches = Array.from(e.touches);

        if (this.touches.length === 1) {
            // 單指：開始平移
            this.isPanning = true;
            this.isMoved = false;  // ⚠️ 重置移動標記
            this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
        } else if (this.touches.length === 2) {
            // 雙指：準備縮放
            this.isPanning = false;
            const dx = this.touches[0].clientX - this.touches[1].clientX;
            const dy = this.touches[0].clientY - this.touches[1].clientY;
            this.lastTouchDistance = Math.sqrt(dx * dx + dy * dy);
        }
    }, { passive: false });

    this.container.addEventListener('touchmove', (e) => {
        e.preventDefault();  // ⚠️ 重要：防止頁面滾動
        this.touches = Array.from(e.touches);

        if (this.touches.length === 1 && this.isPanning) {
            // 單指平移
            this.offsetX += this.touches[0].clientX - this.panStart.x;
            this.offsetY += this.touches[0].clientY - this.panStart.y;
            this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
            this.isMoved = true;  // ⚠️ 標記為已移動
            this.render();
        } else if (this.touches.length === 2) {
            // 雙指縮放
            const dx = this.touches[0].clientX - this.touches[1].clientX;
            const dy = this.touches[0].clientY - this.touches[1].clientY;
            const distance = Math.sqrt(dx * dx + dy * dy);

            if (this.lastTouchDistance > 0) {
                const delta = distance / this.lastTouchDistance;

                // 計算縮放中心（兩指中點）
                const rect = this.container.getBoundingClientRect();
                const centerX = (this.touches[0].clientX + this.touches[1].clientX) / 2 - rect.left;
                const centerY = (this.touches[0].clientY + this.touches[1].clientY) / 2 - rect.top;

                this.offsetX = centerX - (centerX - this.offsetX) * delta;
                this.offsetY = centerY - (centerY - this.offsetY) * delta;
                this.scale *= delta;
                this.scale = Math.max(0.1, Math.min(5, this.scale));
                this.render();
            }

            this.lastTouchDistance = distance;
        }
    }, { passive: false });

    this.container.addEventListener('touchend', (e) => {
        this.touches = Array.from(e.touches);

        if (this.touches.length === 0) {
            this.isPanning = false;
            this.lastTouchDistance = 0;
        } else if (this.touches.length === 1) {
            // 從雙指變成單指，重新設定平移起點
            this.isPanning = true;
            this.panStart = { x: this.touches[0].clientX, y: this.touches[0].clientY };
            this.lastTouchDistance = 0;
        }
    });

    this.render();
}
```

#### ⭐ Hit Test：動態游標更新

```javascript
updateCursor(e) {
    // 因為節點是 pointer-events: none，需要用 Hit Test 動態更新游標
    const rect = this.container.getBoundingClientRect();
    const wx = (e.clientX - rect.left - this.offsetX) / this.scale;
    const wy = (e.clientY - rect.top - this.offsetY) / this.scale;

    let hitLink = false;
    for (let i = this.nodes.length - 1; i >= 0; i--) {
        const node = this.nodes[i];
        if (wx >= node.x && wx <= node.x + node.width &&
            wy >= node.y && wy <= node.y + node.height) {
            if (node.link && node.link.trim()) {
                hitLink = true;
            }
            break;
        }
    }
    this.container.style.cursor = hitLink ? 'pointer' : (this.isPanning ? 'grabbing' : 'grab');
}
```

#### ⭐ Hit Test：節點點擊處理

```javascript
handleNodeClick(e) {
    // ⚠️ 如果有移動過，不視為點擊（防止拖曳後誤觸連結）
    if (this.isMoved) return;

    // ⚠️ 如果點的是文字中的超連結，讓它正常處理
    if (e.target.closest('a')) return;

    // 將螢幕座標轉換為畫布座標
    const rect = this.container.getBoundingClientRect();
    const wx = (e.clientX - rect.left - this.offsetX) / this.scale;
    const wy = (e.clientY - rect.top - this.offsetY) / this.scale;

    // Hit Test：從最上層節點開始檢查
    for (let i = this.nodes.length - 1; i >= 0; i--) {
        const node = this.nodes[i];
        if (wx >= node.x && wx <= node.x + node.width &&
            wy >= node.y && wy <= node.y + node.height) {

            // 如果節點有連結，執行導航
            if (node.link && node.link.trim()) {
                const link = node.link.trim();

                // 判斷是否為專案內的 HTML 文件（相對路徑且以 .html/.htm 結尾）
                const isProjectHtml = !link.match(/^[a-zA-Z]+:\/\//) &&    // 不包含協議
                                      !link.match(/^\//) &&                 // 不是 Unix 絕對路徑
                                      !link.match(/^[a-zA-Z]:[\/\\]/) &&    // 不是 Windows 絕對路徑
                                      link.match(/\.html?$/i);              // 以 .html 或 .htm 結尾

                if (isProjectHtml) {
                    window.location.href = link;  // 同視窗開啟
                } else {
                    window.open(link, '_blank', 'noopener,noreferrer');  // 新視窗開啟
                }
            }
            // 無論有無連結，點到節點就不再向下穿透
            break;
        }
    }
}
```

#### 內容自動置中

```javascript
centerContent() {
    if (this.nodes.length === 0 && this.frames.length === 0) return;

    let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;

    // 計算節點邊界
    this.nodes.forEach(node => {
        minX = Math.min(minX, node.x);
        minY = Math.min(minY, node.y);
        maxX = Math.max(maxX, node.x + node.width);
        maxY = Math.max(maxY, node.y + node.height);
    });

    // 計算框架邊界
    this.frames.forEach(frame => {
        minX = Math.min(minX, frame.x);
        minY = Math.min(minY, frame.y);
        maxX = Math.max(maxX, frame.x + frame.width);
        maxY = Math.max(maxY, frame.y + frame.height);
    });

    if (minX === Infinity) return;

    // 計算內容中心和視口中心
    const contentCenterX = (minX + maxX) / 2;
    const contentCenterY = (minY + maxY) / 2;

    const rect = this.container.getBoundingClientRect();
    const viewportCenterX = rect.width / 2;
    const viewportCenterY = rect.height / 2;

    // 設置偏移量使內容置中
    this.offsetX = viewportCenterX - contentCenterX;
    this.offsetY = viewportCenterY - contentCenterY;
}
```

### ⚠️ 重要注意事項

1. **`pointer-events: none` vs `touch-action: none`**
   - `touch-action: none` 在某些情況下仍會被元素攔截
   - `pointer-events: none` + Hit Test 是最可靠的方案

2. **`isMoved` 標記**：
   - 必須在 `mousedown`/`touchstart` 時重置為 `false`
   - 在 `mousemove`/`touchmove` 時設為 `true`
   - 在 `click` 處理時檢查，避免拖曳後誤觸連結

3. **文字中的超連結**：
   - 設置 `pointer-events: auto` 讓 `<a>` 標籤可以正常點擊
   - 在 `handleNodeClick()` 中用 `e.target.closest('a')` 排除

4. **動態游標更新**：
   - 因為節點是 `pointer-events: none`，hover 時不會顯示手指游標
   - 需要在 `mousemove` 時用 Hit Test 動態更新 `container.style.cursor`

5. **測試建議**：
   - 在實際觸控設備上測試平移/縮放
   - 確認觸碰節點/框架/連接線時仍能平移
   - 確認節點連結點擊有效，但拖曳後不觸發

6. **節點文字顏色**：
   - CSS `.node` **不可寫死 `color: white`**，否則淺色底色會導致文字不可見
   - 必須由 `getContrastColor()` 根據背景亮度動態計算
   - `render()` 結尾需執行後置安全檢查，確保無節點遺漏

---

## 🔧 輔助函數

### ID 生成

```javascript
// ✅ 標準 ID 生成方式
function generateId(prefix) {
    return prefix + '-' + Date.now();
}

// 使用範例
const nodeId = generateId('node');       // "node-1734176400001"
const frameId = generateId('frame');     // "frame-1734176400002"
const connId = generateId('conn');       // "conn-1734176400003"
```

### 網格布局

```javascript
function createGridLayout(items, columns = 3, spacingX = 250, spacingY = 180) {
    return items.map((item, index) => ({
        ...item,
        id: generateId('node'),
        x: 150 + (index % columns) * spacingX,
        y: 150 + Math.floor(index / columns) * spacingY,
        width: 200,
        height: 100
    }));
}
```

### 文字對比色（⚠️ 必須實作！）

CSS `.node` 不可寫死 `color: white`。文字顏色必須由下列方法動態計算：

```javascript
// 根據背景亮度自動回傳深色或淺色文字色（sRGB luminance 公式）
getContrastColor(hex) {
    const h = (hex || '#3498db').replace(/^#/, '');
    const r = parseInt(h.substr(0, 2), 16);
    const g = parseInt(h.substr(2, 2), 16);
    const b = parseInt(h.substr(4, 2), 16);
    const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
    return luminance > 0.5 ? '#1a1a1a' : '#ffffff';
}
```

**使用方式**：
- `renderNode()` 設定背景時同步呼叫：
  ```javascript
  const nodeBg = node.color || '#3498db';
  nodeEl.style.background = nodeBg;
  nodeEl.style.color = this.getContrastColor(nodeBg);
  ```
- `render()` 結尾執行後置安全檢查（防漏網之魚）：
  ```javascript
  this.canvas.querySelectorAll('.node').forEach(el => {
      if (!el.style.color) {
          const bg = el.style.background || el.style.backgroundColor || '#3498db';
          el.style.color = this.getContrastColor(bg);
      }
  });
  ```

**效果**：深色底 → 白字 (`#ffffff`)；淺色底 → 深灰字 (`#1a1a1a`)，自動切換。

---

### 自動生成連接線

```javascript
function createSequentialConnections(nodeIds) {
    const connections = [];
    for (let i = 0; i < nodeIds.length - 1; i++) {
        connections.push({
            id: generateId('conn'),
            from: nodeIds[i],
            to: nodeIds[i + 1],
            arrowType: 'single',
            color: '#2c3e50',
            label: ''
        });
    }
    return connections;
}
```

---

## 🎨 預設值和顏色

### 建議的預設值

```javascript
const DEFAULTS = {
    node: {
        width: 200,
        height: 100,
        color: '#3498db',
        textAlign: 'center'
    },
    frame: {
        borderColor: '#95a5a6',
        bgColor: 'rgba(149, 165, 166, 0.1)',
        zIndex: 0
    },
    connection: {
        color: '#2c3e50',
        arrowType: 'single'
    }
};
```

### 常用顏色

```javascript
const NODE_COLORS = {
    blue: '#3498db',      // 一般資訊
    green: '#2ecc71',     // 正面/完成
    red: '#e74c3c',       // 警告/重要
    yellow: '#f39c12',    // 注意
    purple: '#9b59b6',    // 特殊
    gray: '#95a5a6',      // 次要/註解
    white: '#ffffff',     // 註解/小提示（需搭配邊框或陰影）
};
```

---

## 📁 專案結構範例

### 單一 Map 檔專案
```
MyProject/
├── concept-crafter_icon.png
└── project-map.html
```

### 多 Map 檔專案
```
MyProject/
├── concept-crafter_icon.png  # 所有 Map 共用
├── overview.html
├── module-1.html
├── module-2.html
└── README.md
```

---

## 📚 參考資源

### 檔案位置
- **驗證腳本**：`~/.claude/ConceptMap/json-validation-script.js`
- **模板**：`~/.claude/ConceptMap/templates/map-viewer-template.html`
- **範例**：
  - `~/.claude/ConceptMap/examples/sample-project-map.html` - 基本專案概念圖
  - `~/.claude/ConceptMap/examples/label-node-waypoint-example.html` - 標籤節點拐點技巧
- **內定圖標**：`~/.claude/ConceptMap/concept-crafter_icon.png`

### 相關文檔
- **操作流程**：`WORKFLOW.md` - 生成步驟和驗證清單
- **全局配置**：`~/.claude/CLAUDE.md` - Claude 全局設定

---

**版本歷史**：
- v5.8 (2026-02-22): 🔧 修正箭頭縮放問題 — 為箭頭標記添加 `viewBox='0 0 9 6'` 並使用正向縮放（`3 * this.scale`），讓箭頭隨畫布等比縮放；若無 `viewBox` 改變 `markerWidth` 對路徑大小無效
- v5.6 (2026-02-16): 修正 `isLabel` 說明歧義——介面定義、預設值表格、程式碼範例、章節標題全面改寫，明確標示所有節點必填（一般節點 = false，不可省略）
- v5.5 (2026-02-16): 「四種標準組合樣式」加入強制對稱規則與具名樣式（單向流動/反向流動/雙向互動/純關聯）；「禁止穿越節點」精準化為「視線原則（Line of Sight）」，手段 A（偏移）/手段 B（拐點化）
- v5.4 (2026-02-16): 「禁止穿越節點」章節新增「⛔ A-B-C 共線陷阱（隔山打牛）」具名模式，說明情境、解決方案（偏移佈局、並排佈局）；佈局規劃流程步驟 3 更新為明確檢查「隔山打牛」
- v5.3 (2026-02-14): 拐點技巧章節開頭新增使用前提警告（僅在路徑確實被阻擋時才用，不是預設選項）
- v5.2 (2026-02-14): 新增「連接線佈局規範」章節 — 禁止穿越節點、拐點箭頭規則（進入側必須為 none）、禁止連接線重合（交叉可接受）、佈局規劃流程；同步更新 instructions.md 佈局規則摘要
- v5.1 (2026-02-13): 節點文字對比色自動計算 — 移除 CSS `.node { color: white }` 寫死，改由 `getContrastColor()` 根據背景亮度動態決定文字色（深色底→白字，淺色底→深灰字）；`render()` 新增後置安全檢查；新增「文字對比色」輔助函數規範；更新重要注意事項與驗證清單；統一徽章符號為 `\u2714` / `\u2717`
- v4.9 (2026-01-26): 修正模板和標籤節點樣式：
  - 修正模板註解，移除佔位符文字避免 `replace()` 只替換第一個
  - 標籤節點新增 `white-space: nowrap` 強制文字橫向排列
- v4.8 (2026-01-24): 🎉 **全域 Hit Test 方案** — 徹底重構觸控支援，採用 Concept Crafter v2.7.4 的設計模式：
  - 所有元素（節點、框架）設置 `pointer-events: none`，觸控事件完美穿透
  - 使用 Container 層級的 click 事件 + Hit Test（座標計算）處理節點點擊
  - 新增 `isMoved` 標記區分「拖曳」和「點擊」，避免平移後誤觸連結
  - 新增 `updateCursor()` 動態更新游標（因節點是 pointer-events: none）
  - 新增 `centerContent()` 初始化時自動將內容置中
  - 文字中的 `<a>` 超連結保持 `pointer-events: auto` 可正常點擊
- v4.7 (2026-01-21): 改進觸控支援 — 將節點和框架的 `pointer-events: none` 改為 `touch-action: none`（已被 v4.8 取代）
- v4.6 (2026-01-20): 修正觸控操作問題（已被 v4.8 取代）
- v4.5 (2026-01-19): 新增「📱 觸控螢幕支援」章節，包含單指平移、雙指縮放功能；更新 CSS 設定（touch-action）、viewport meta 標籤、完整的觸控事件處理範例
- v4.4 (2026-01-19): 新增節點 `isLabel` 屬性（必須屬性），支援標籤樣式呈現；新增 `.node-label-mode` CSS 樣式；更新 TypeScript 定義、默認值規範、範例代碼和 Viewer 渲染邏輯
- v4.3 (2026-01-17): 新增「虛線（dashed）」連接線樣式用於註解連接；新增節點作為註解說明的設計技巧；新增「節點作為連接線標籤（拐點技巧）」進階設計；新增白色節點顏色
- v4.1 (2024-12-29): 新增「框架連接線」特性說明和範例，簡化複雜圖表的連接數量
- v4.0 (2024-12-29): 加入「安全的 JSON 生成方法」章節，從源頭預防錯誤
- v3.0 (2024-12-27): 精簡規範，整合重複內容
- v2.0 (2024-12-27): 加入三層 JSON 驗證機制
- v1.0 (2024-12-26): 初始版本
