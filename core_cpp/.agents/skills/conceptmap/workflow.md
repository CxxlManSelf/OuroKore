# ConceptMap 生成工作流程

> **操作步驟指南和驗證清單**
>
> 版本：4.12 | 更新日期：2026-02-22

---

## 🎯 生成 Map 檔標準流程

### 步驟 1：讀取規範
- 參考 `SPEC.md` 了解數據結構
- 確認 JSON 格式規則
- 檢查範例檔案

### 步驟 2：處理圖標

**預設行為**（無需詢問）：
```bash
# 複製內定圖標到專案目錄
cp .claude/ConceptMap/concept-crafter_icon.png ./project-dir/

# Windows
copy .claude\ConceptMap\concept-crafter_icon.png .\project-dir\
```

**使用者提供自訂圖標時**：
1. 驗證是 PNG 格式
2. 重新命名為 `concept-crafter_icon.png`
3. 放置在與 Map 檔相同目錄

### 步驟 3：佈局視線檢查（Line-of-Sight Check）🚨 強制！不可跳過！

在決定任何節點座標之前，必須先完成視線檢查。這是避免連接線穿越節點的唯一時機。

**執行流程**：

1. **列出所有節點與連接關係**（from → to）
2. **草擬節點相對位置**，讓連線方向盡可能不交叉
3. **建立射線**：對每一條連線，想像一條從 `from.center` 到 `to.center` 的射線
4. **障礙排除（隔山打牛）**：檢查畫布上是否有任何「非目標」節點位於此射線上——垂直／水平清單佈局特別容易觸發
5. **消除穿越**：
   - 若有障礙物 → **優先移動障礙物**（手段 A：偏移）
   - 若無法移動 → **在障礙物旁建立 `isLabel: true` 的標籤節點作為拐點，繞過障礙**（手段 B：拐點化）
6. **確認無重複連接**：任意兩節點之間只能有一條連接線

> ❌ 未完成此步驟就生成數據 → 後期極難修正，佈局必須整體重做

### 步驟 4：標籤節點規範檢查

若步驟 3 中使用了拐點（標籤節點），必須逐一確認每個 `isLabel: true` 的節點：

1. **聚湊尺寸**：寬度建議 36、高度建議 22（高度 < 40 時 Viewer 自動套用緊湊樣式）
2. **一進一出**：每個標籤節點必須恰好有兩條連接線，一條進、一條出
3. **箭頭方向符合四種具名樣式之一**（見 SPEC.md「四種標準組合樣式（強制對稱）」）：
   - 單向流動：進 `none` / 出 `single`
   - 反向流動：進 `single` / 出 `none`
   - 雙向互動：進 `single` / 出 `single`
   - 純關聯：進 `none` / 出 `none`

### 步驟 5：生成數據

**⚠️ 必須使用安全的生成方法**：

**✅ 推薦做法：JavaScript 對象 + JSON.stringify**

```javascript
// 步驟 1：創建 JavaScript 對象
const mapData = {
    title: "概念圖標題",
    data: {
        nodes: [
            {
                id: "node-" + Date.now(),
                x: 100,                     // ✅ 數字（無引號）
                y: 200,                     // ✅ 數字（無引號）
                width: 200,
                height: 100,
                text: "內容",
                color: "#3498db",
                textAlign: "center",
                completed: false,           // ⚠️ 必須包含
                abandoned: false,           // ⚠️ 必須包含
                link: "",                   // ⚠️ 必須包含（即使為空）
                isLabel: false              // ⚠️ 所有節點必填（一般節點固定為 false，不可省略）
            }
        ],
        frames: [],
        connections: []
    }
};

// 步驟 2：轉換為 JSON（自動確保格式正確）
const jsonString = JSON.stringify(mapData, null, 4);
```

**❌ 避免：手動編輯 JSON 字符串**
- 容易產生「數字後多雙引號」錯誤
- 難以發現和修正

**關鍵規則**（`JSON.stringify` 會自動處理）：
- ✅ **所有屬性必須明確導出**：即使是默認值也不可省略
- ✅ ID 格式：`'node-' + Date.now()` （字符串）
- ✅ 數字值：不加引號（x, y, width, height, zIndex）
- ✅ 布林值：不加引號（completed, abandoned）
- ✅ 字符串值：加引號（id, text, color, link）
- ✅ 空值使用：`false`（布林）或 `""`（字符串），不可使用 `null` 或 `undefined`

### 步驟 6：組裝 HTML

**使用模板字符串組裝**：

```javascript
const htmlContent = `
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <title>${mapData.title} - Concept Map Viewer</title>
    <link rel="icon" type="image/png" href="./concept-crafter_icon.png">
    <style>
        /* 樣式代碼 */
    </style>
    <script id="map-data" type="application/json">
    ${jsonString}
    </script>
</head>
<body>
    <!-- HTML 結構 -->
    <script>
        // MapViewer 代碼
    </script>
</body>
</html>
`;
```

確保包含：
- ✅ Favicon 引用：`<link rel="icon" href="./concept-crafter_icon.png">`
- ✅ CSS 包含：`* { box-sizing: border-box; }`
- ✅ Map 數據：`<script id="map-data" type="application/json">`
- ✅ MapViewer 類完整實現

### 步驟 6.5：生成後立即驗證（🚨 強制！不可跳過！）

**前提**：如果步驟 4 確實使用了 `JSON.stringify()`，以下錯誤幾乎不可能發生。
這個步驟的目的是**確認你真的用了 JSON.stringify()**，而非手寫 JSON。

#### 正則檢查（搜尋下列模式，必須零結果）

| 搜尋模式 | 代表什麼 | 錯誤範例 |
|---------|---------|---------|
| `\d+"` | 數字後多了雙引號 | `"y": 440",` |
| `"(true\|false)"` | 布林值被引號包住 | `"completed": "false",` |

- ✅ **全部零結果** → 通過，繼續步驟 5
- 🚨 **有任何結果** → 立即停止，修正後重新執行此步驟

#### 執行驗證腳本

```bash
node ~/.claude/ConceptMap/json-validation-script.js
```

### 步驟 7：瀏覽器測試（必須！）

1. 在瀏覽器中打開檔案
2. 確認地圖正確顯示
3. 測試平移和縮放功能
4. 檢查 Console 無錯誤

### 步驟 8：最終驗證

- [ ] ⚠️ **所有元素的所有屬性都已明確導出**（不可省略任何屬性）
- [ ] 所有 Node 包含：id, x, y, width, height, text, color, textAlign, completed, abandoned, link, isLabel
- [ ] 所有 Frame 包含：id, x, y, width, height, label, borderColor, bgColor, zIndex, completed, abandoned
- [ ] 所有 Connection 包含：id, from, to, arrowType, label, color
- [ ] `concept-crafter_icon.png` 與 Map 檔在同一目錄
- [ ] 所有 ID 都是字符串格式
- [ ] CSS 包含 `box-sizing: border-box`
- [ ] 導航連結可以正常跳轉（如果有）

---

## ✅ 完整驗證清單

### JSON 格式驗證

> 若步驟 3 確實使用 `JSON.stringify()`，以下項目應全部自動通過。
> 有任何未通過 → 代表 JSON 有手動編輯，需排查。

- [ ] 搜尋 `\d+"` → 沒有任何結果（數字後不應有引號）
- [ ] 搜尋 `"(true|false)"` → 沒有任何結果（布林值不應有引號）
- [ ] 執行驗證腳本通過

### 佈局驗證

- [ ] **檢查「隔山打牛」**：任意兩連接節點之間沒有「中間人」阻擋直線路徑（垂直/水平清單佈局必查）⚠️ 重要
- [ ] **每條連接線的直線路徑上沒有其他節點**（逐條目視確認）⚠️ 重要
- [ ] **任意兩節點之間沒有重複連接線**（需雙向者改用 `double`）⚠️ 重要
- [ ] **所有拐點節點都有對應的路徑障礙**（無障礙則不應存在拐點）
- [ ] **拐點進入側為 `none`**，箭頭只出現在指向真正目標的一側

### 基本結構驗證

- [ ] 是獨立的 HTML 文件
- [ ] `concept-crafter_icon.png` 與 Map 檔在同一目錄
- [ ] HTML 使用 `<link rel="icon" href="./concept-crafter_icon.png">`
- [ ] 所有 ID 使用字符串格式（'node-...'、'frame-...'、'conn-...'）
- [ ] CSS 包含 `* { box-sizing: border-box; }`
- [ ] **SVG 層不使用 transform**（搜尋 `svgLayer.style.transform` 應無結果）⚠️ 重要
- [ ] ⚠️ **所有 Node 包含以下全部欄位**（不可省略任何一個，即使是默認值）：
  - `id, x, y, width, height, text, color, textAlign`
  - `completed, abandoned, link`
  - `isLabel` ← 🚨 **最常被遺漏**：一般節點必須明確寫出 `isLabel: false`
- [ ] ⚠️ **所有 Frame 包含以下全部欄位**：
  - `id, x, y, width, height, label, borderColor, bgColor, zIndex`
  - `completed, abandoned`
- [ ] ⚠️ **所有 Connection 包含以下全部欄位**：
  - `id, from, to, arrowType, label, color`

### 功能驗證

- [ ] 可在瀏覽器中運行
- [ ] 支援平移和縮放（滑鼠）
- [ ] **支援觸控操作**（單指平移、雙指縮放）⚠️ 必須
- [ ] **連接線在縮放時不會偏移**（縮放後連接線仍正確連接節點）⚠️ 重要
- [ ] 連接線不會深入節點內部
- [ ] 瀏覽器 Console 無錯誤訊息
- [ ] 所有節點和框架正確顯示
- [ ] **箭頭與連接線顏色一致**（每條連接線的箭頭顏色應與線條顏色相同）⚠️ 重要

---

## 🚨 常見錯誤和解決方法

### 錯誤 1：數字後多雙引號 ⚠️ 最常見

**錯誤範例**：
```json
{
    "x": 120,
    "y": 440",    // ← 數字後多了雙引號
    "width": 160
}
```

**症狀**：整個 Map 無法顯示（只有背景）

**檢查方法**：搜尋 `\d+"` 應該沒有任何結果

**修正方法**：移除數字後的雙引號
```json
"y": 440,     // ✅ 正確
```

### 錯誤 2：數字被引號包住

**錯誤範例**：
```json
"x": "100",   // ❌ 數字被當成字符串
```

**修正方法**：
```json
"x": 100,     // ✅ 正確
```

### 錯誤 3：布林值被引號包住

**錯誤範例**：
```json
"completed": "false",   // ❌ 布林值被當成字符串
```

**修正方法**：
```json
"completed": false,     // ✅ 正確
```

### 錯誤 4：ID 格式錯誤

**錯誤範例**：
```json
"id": 1234567890,      // ❌ 數字格式
"id": "node_123",      // ❌ 使用底線
```

**修正方法**：
```json
"id": "node-1234567890",  // ✅ 正確
```

### 錯誤 5：忘記複製 icon 檔案

**症狀**：瀏覽器 tab 沒有顯示圖標

**修正方法**：
```bash
cp .claude/ConceptMap/concept-crafter_icon.png ./project-dir/
```

### 錯誤 6：忘記 box-sizing

**症狀**：連接線深入節點內部

**修正方法**：確保 CSS 包含
```css
* {
    box-sizing: border-box;
}
```

### 錯誤 7：SVG 層使用 transform ⚠️ 常見且難察覺

**錯誤範例**：
```javascript
// ❌ 錯誤：SVG 層使用了 transform
this.svgLayer.style.transform =
    `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
```

**症狀**：
- 初始狀態連接線正確
- 縮放畫布後連接線位置錯誤
- 連接線沒有連接到節點邊緣

**檢查方法**：
搜尋 `svgLayer.style.transform` → 應該**沒有任何結果**

**修正方法**：
```javascript
// ✅ 移除 SVG transform，改為手動計算座標
// 在 render() 中只設置 canvas transform
this.canvas.style.transform =
    `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;

// 在 updateConnections() 中手動計算座標
const x1 = fromEdge.x * this.scale + this.offsetX;
const y1 = fromEdge.y * this.scale + this.offsetY;
const x2 = toEdge.x * this.scale + this.offsetX;
const y2 = toEdge.y * this.scale + this.offsetY;

line.setAttribute('x1', x1);
line.setAttribute('y1', y1);
line.setAttribute('x2', x2);
line.setAttribute('y2', y2);
```

**詳細說明**：參考 `SPEC.md` 的「SVG 連接線處理規範」章節

### 錯誤 8：箭頭與連接線顏色不一致 ⚠️ 常見

**錯誤範例**：
```javascript
// ❌ 錯誤：箭頭標記使用固定顏色
const markerSingle = document.createElementNS('http://www.w3.org/2000/svg', 'marker');
markerSingle.setAttribute('id', 'arrowhead-single');
const pathSingle = document.createElementNS('http://www.w3.org/2000/svg', 'path');
pathSingle.setAttribute('fill', '#2c3e50');  // 固定顏色！

// 但連接線使用動態顏色
line.setAttribute('stroke', conn.color || '#2c3e50');  // 動態顏色
line.setAttribute('marker-end', 'url(#arrowhead-single)');  // 總是指向同一個固定顏色的箭頭
```

**症狀**：
- 連接線有不同的顏色（如綠色、藍色、紅色）
- 但所有箭頭都是同一種顏色（通常是深灰色 #2c3e50）
- 視覺上不一致，看起來不協調

**檢查方法**：
1. 搜尋 `setAttribute('fill', '#` → 箭頭標記中是否使用固定顏色
2. 檢查箭頭標記 ID 是否包含顏色資訊（如 `arrowhead-single-34495e`）
3. 在瀏覽器中檢視：不同顏色的連接線是否有相同顏色的箭頭

**修正方法**：
```javascript
// ✅ 正確：為每種顏色創建專屬箭頭標記，並使用 viewBox 讓箭頭隨畫布縮放
updateConnections() {
    const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');

    // 收集所有使用的顏色
    const colors = new Set();
    this.connections.forEach(conn => {
        colors.add(conn.color || '#2c3e50');
    });

    // ⚠️ 必須搭配 viewBox 才能讓 markerWidth 動態縮放生效
    // 無 viewBox 時，markerWidth 只影響裁剪框，路徑大小固定，無法縮放
    // 視覺大小 = markerHeight × strokeWidth = 6 × 2 = 12px（scale=1 時）
    const markerW = (9 * this.scale).toString();
    const markerH = (6 * this.scale).toString();

    // 為每種顏色創建箭頭標記
    colors.forEach(color => {
        const colorId = color.replace('#', '');

        // Single arrow marker
        const markerSingle = document.createElementNS('http://www.w3.org/2000/svg', 'marker');
        markerSingle.setAttribute('id', `arrowhead-single-${colorId}`);
        markerSingle.setAttribute('viewBox', '0 0 9 6');   // ✅ 必須有 viewBox
        markerSingle.setAttribute('markerWidth', markerW);  // ✅ 正向縮放
        markerSingle.setAttribute('markerHeight', markerH);
        markerSingle.setAttribute('refX', '9');
        markerSingle.setAttribute('refY', '3');
        markerSingle.setAttribute('orient', 'auto');
        const pathSingle = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        pathSingle.setAttribute('d', 'M0,0 L0,6 L9,3 z');
        pathSingle.setAttribute('fill', color);  // ✅ 使用動態顏色
        markerSingle.appendChild(pathSingle);
        defs.appendChild(markerSingle);

        // Double arrow marker (同樣方式)
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
        pathDouble.setAttribute('fill', color);  // ✅ 使用動態顏色
        markerDouble.appendChild(pathDouble);
        defs.appendChild(markerDouble);
    });

    this.svgLayer.appendChild(defs);

    // 渲染連接線時使用對應顏色的箭頭
    this.connections.forEach(conn => {
        const lineColor = conn.color || '#2c3e50';
        const colorId = lineColor.replace('#', '');

        line.setAttribute('stroke', lineColor);

        // ✅ 使用顏色特定的箭頭標記
        if (conn.arrowType === 'single') {
            line.setAttribute('marker-end', `url(#arrowhead-single-${colorId})`);
        } else if (conn.arrowType === 'double') {
            line.setAttribute('marker-start', `url(#arrowhead-double-${colorId})`);
            line.setAttribute('marker-end', `url(#arrowhead-single-${colorId})`);
        }
    });
}
```

**詳細說明**：參考 `SPEC.md` 的完整 `updateConnections()` 實現

---

## 🔍 快速診斷方法

### 症狀：Map 完全不顯示內容（只有背景）

**原因**：99% 是 JSON 格式錯誤

**診斷步驟**：
1. 打開瀏覽器開發者工具（F12）
2. 查看 Console 標籤
3. 是否有 `JSON.parse` 錯誤？
   - **是** → JSON 語法錯誤（括號、逗號、引號等）
   - **否** → 繼續下一步
4. 使用正則表達式搜尋：`\d+"`
   - **有結果** → 數字後多雙引號（移除多餘的引號）
   - **無結果** → 執行驗證腳本找出具體問題

### 症狀：連接線深入節點內部

**原因**：缺少 `box-sizing: border-box`

**修正方法**：在 CSS 頂部加入
```css
* {
    box-sizing: border-box;
}
```

### 症狀：連接線在縮放時偏移 ⚠️ 常見

**原因**：SVG 層使用了 transform，導致連接線座標被二次變換

**錯誤範例**：
```javascript
// ❌ 錯誤：SVG 層使用了 transform
render() {
    this.canvas.style.transform = `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
    this.svgLayer.style.transform = `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`; // 錯誤！
}

updateConnections() {
    line.setAttribute('x1', fromEdge.x);  // 使用原始座標，會被 SVG transform 二次變換
    line.setAttribute('y1', fromEdge.y);
}
```

**檢查方法**：
```bash
# 搜尋 SVG transform（應該沒有任何結果）
grep "svgLayer.style.transform" your-map.html
```

**修正方法**：
```javascript
// ✅ 正確：移除 SVG transform，改為手動計算座標
render() {
    this.canvas.style.transform = `translate(${this.offsetX}px, ${this.offsetY}px) scale(${this.scale})`;
    // SVG 層不設置 transform
}

updateConnections() {
    // 計算邊緣交點（原始座標）
    const fromEdge = this.getEdgePoint(fromNode, fromCenter, toCenter);
    const toEdge = this.getEdgePoint(toNode, toCenter, fromCenter);

    // ✅ 手動應用 scale 和 offset
    const x1 = fromEdge.x * this.scale + this.offsetX;
    const y1 = fromEdge.y * this.scale + this.offsetY;
    const x2 = toEdge.x * this.scale + this.offsetX;
    const y2 = toEdge.y * this.scale + this.offsetY;

    line.setAttribute('x1', x1);
    line.setAttribute('y1', y1);
    line.setAttribute('x2', x2);
    line.setAttribute('y2', y2);

    // 標籤也使用變換後的座標
    if (conn.label) {
        const midX = (x1 + x2) / 2;
        const midY = (y1 + y2) / 2;
        text.setAttribute('x', midX);
        text.setAttribute('y', midY - 5);
    }
}

// ⚠️ 重要：getEdgePoint 方法 - 不使用最小尺寸限制
// 這樣才能正確支援小型標籤節點（isLabel: true）
getEdgePoint(rect, center, target) {
    const dx = target.x - center.x;
    const dy = target.y - center.y;
    if (dx === 0 && dy === 0) return { x: center.x, y: center.y };

    const halfWidth = rect.width / 2;   // ✅ 直接使用實際尺寸
    const halfHeight = rect.height / 2; // ❌ 不要用 Math.max(50, ...) 或 Math.max(30, ...)

    let t;
    if (Math.abs(dx) * halfHeight > Math.abs(dy) * halfWidth) {
        t = halfWidth / Math.abs(dx);
    } else {
        t = halfHeight / Math.abs(dy);
    }
    return { x: center.x + dx * t, y: center.y + dy * t };
}
```

**詳細說明**：參考 `SPEC.md` 的「SVG 連接線處理規範」章節

### 症狀：無法平移或縮放（滑鼠）

**原因**：MapViewer 事件監聽器未正確設置

**檢查**：
- `setupEventListeners()` 是否被調用
- 事件處理函數是否正確綁定

### 症狀：觸控螢幕無法操作 ⚠️ 常見

**原因**：缺少觸控事件處理或 CSS 設定

**檢查清單**：
1. **CSS**：`#container` 是否包含 `touch-action: none`
2. **Viewport**：是否設置 `<meta name="viewport" content="..., maximum-scale=1.0, user-scalable=no">`
3. **觸控事件**：是否有 `touchstart`、`touchmove`、`touchend` 事件監聽器
4. **passive 設定**：觸控事件是否設置 `{ passive: false }`

**修正方法**：

1. 在 CSS 中加入：
```css
#container {
    touch-action: none;  /* 防止瀏覽器預設觸控行為 */
}
```

2. 更新 viewport meta 標籤：
```html
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
```

3. 加入觸控事件處理（參考 `SPEC.md` 的「觸控螢幕支援」章節）

**詳細說明**：參考 `SPEC.md` 的「📱 觸控螢幕支援」章節

### 症狀：瀏覽器 tab 沒有顯示圖標

**原因**：icon 檔案不存在或路徑錯誤

**檢查清單**：
1. `concept-crafter_icon.png` 是否與 Map 檔在同一目錄
2. 檔案名稱是否正確（區分大小寫）
3. HTML 中的路徑是否為 `./concept-crafter_icon.png`
4. 清除瀏覽器快取並重新載入

### 症狀：箭頭與連接線顏色不一致 ⚠️ 常見

**原因**：箭頭標記使用固定顏色，而不是根據連接線顏色動態創建

**症狀**：
- 連接線有多種顏色（綠色、藍色、紅色等）
- 但所有箭頭都是同一種顏色（通常是 #2c3e50）

**檢查方法**：
```bash
# 搜尋箭頭標記中的固定顏色（應該沒有結果，或者應該是變數）
grep "pathSingle.setAttribute('fill', '#" your-map.html
grep "pathDouble.setAttribute('fill', '#" your-map.html
```

**修正方法**：
1. 收集所有使用的顏色：`const colors = new Set(); this.connections.forEach(conn => colors.add(conn.color || '#2c3e50'));`
2. 為每種顏色創建專屬箭頭標記：`markerSingle.setAttribute('id', 'arrowhead-single-${colorId}')`
3. 使用對應顏色的標記：`line.setAttribute('marker-end', 'url(#arrowhead-single-${colorId}')`

**詳細說明**：參考 `WORKFLOW.md` 錯誤 8 的完整修正代碼

---

## 💡 最佳實踐

### 1. 生成後立即驗證
不要等到最後才檢查，生成完立即執行正則表達式檢查。

### 2. 使用自動化工具
執行驗證腳本而不只是目測，可以避免 95% 的錯誤。

### 3. 測試優先
在瀏覽器中測試通過後才交付，不要只看程式碼。

### 4. 建立習慣
每次生成都執行相同的檢查流程，形成肌肉記憶。

### 5. 保持簡潔
只生成必要的內容，避免過度工程化。

---

## 📋 交付前最終檢查

在交付 Map 檔給使用者前，確認：

- [ ] 已執行所有三層 JSON 驗證
- [ ] 已在瀏覽器中測試通過
- [ ] Console 無錯誤訊息
- [ ] 所有功能正常運作
- [ ] `concept-crafter_icon.png` 已放置在正確位置
- [ ] 檔案結構清晰（README.md 如果需要）

---

## 🔗 相關資源

- **技術規範**：`SPEC.md` - 完整的數據結構和技術細節
- **驗證腳本**：`json-validation-script.js` - 自動化驗證工具
- **模板檔案**：`templates/map-viewer-template.html` - 完整模板
- **範例檔案**：`examples/sample-project-map.html` - 參考範例

---

**記住**：一個小小的雙引號可能導致整個 Map 無法運作！
養成嚴格檢查的習慣，可以避免 99% 的問題。

---

## 🔍 診斷案例：艾森豪矩陣失敗分析

> 真實案例，說明「視線原則」被違反的典型場景。

- **錯誤現象**：「核心工具」節點的連接線壓在「緊急且重要」文字上，視覺混亂
- **根因**：將「緊急且重要」視為普通節點擺在中線，未執行從「主節點」到「框架」的視線檢查
- **正確做法**：
  1. 將「緊急且重要」設定為 `isLabel: true`（啟用緊湊標籤樣式）
  2. 將該節點移出主連線的 Y 軸路徑（手段 A：偏移）
  3. 或將其作為「拐點」，讓連線先連到它，再連往框架（手段 B：拐點化）

---

**版本歷史**：
- v4.12 (2026-02-22): 錯誤 8 修正方法代碼補齊完整 marker 屬性（viewBox、markerWidth/Height 正向縮放）；功能驗證新增箭頭縮放檢查項目；對應 SPEC.md v5.8
- v4.10 (2026-02-16): 修正程式碼範例 `isLabel` 說明歧義；完整驗證清單「基本結構驗證」新增逐欄位明細清單，並特別標示 `isLabel` 為最常被遺漏欄位；對應 SPEC.md v5.6
- v4.9 (2026-02-16): 步驟 3 升級為「佈局視線檢查（Line-of-Sight Check）」射線思維；新增步驟 4「標籤節點規範檢查」（原步驟 4–7 順移為 5–8）；新增「診斷案例：艾森豪矩陣」；對應 SPEC.md v5.5
- v4.8 (2026-02-16): 步驟 3 佈局規劃新增第 4 子步驟「檢查隔山打牛」；佈局驗證清單新增對應項目；對應 SPEC.md v5.4
- v4.7 (2026-02-14): 新增步驟 3「佈局規劃」強制檢查點（原步驟 3–6 順移為 4–7）；驗證清單新增「佈局驗證」區塊（路徑淨空、無重複連接、拐點合理性、拐點箭頭規則）；對應 SPEC.md v5.2
- v4.6 (2026-02-11): 精簡驗證步驟：合併重複的步驟 4.5／步驟 5 為單一驗證步驟；重構完整驗證清單的 JSON 格式驗證區塊，強調 JSON.stringify() 是根本保障
- v4.5 (2026-01-27): 版本號對齊
- v4.4 (2026-01-26): 同步 SPEC.md v4.9 的模板修正（模板註解、標籤節點 `white-space: nowrap`）
- v4.3 (2026-01-19): 新增觸控螢幕支援驗證項目（單指平移、雙指縮放）、觸控問題診斷方法
- v4.0 (2024-12-29): 加入「步驟 4.5 強制檢查點」和「安全生成方法」，從源頭預防錯誤
- v3.0 (2024-12-27): 精簡流程，強化驗證步驟
- v2.0 (2024-12-27): 加入三層驗證機制
- v1.0 (2024-12-26): 初始版本
