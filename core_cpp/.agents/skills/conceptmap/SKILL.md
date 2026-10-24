---
name: conceptmap
description: 生成概念圖（ConceptMap）。當使用者說「生成 map」、「畫 map 圖」、「建立概念圖」、「ConceptMap」、「概念圖」或「concept crafter」時使用
allowed-tools: "Read, Write, Bash, Glob, Grep"
---

# ConceptMap 生成規範

## 📚 必讀文件（開始前請讀取）

- **技術規範**：[spec.md](spec.md) — 完整的數據結構、JSON 格式規則、HTML 模板
- **操作流程**：[workflow.md](workflow.md) — 生成步驟、驗證清單、錯誤診斷

## 🗂️ 資源檔案

- **預設圖標**：[concept-crafter_icon.png](concept-crafter_icon.png)（需複製到專案目錄）
- **HTML 模板**：[templates/map-viewer-template.html](templates/map-viewer-template.html)
- **驗證腳本**：[scripts/json-validation-script.js](scripts/json-validation-script.js)
- **範例參考**：[examples/](examples/)

## ⚙️ 標準流程

1. 讀取 spec.md 和 workflow.md
2. 複製 `concept-crafter_icon.png` 到專案目錄
3. **執行佈局視線檢查（Line-of-Sight Check）** 🚨 強制！不可跳過！
4. 使用 **JavaScript 對象 + `JSON.stringify()`** 生成數據
5. **生成後立即執行正則驗證** 🚨 強制！不可跳過！
   - 搜尋 `\d+"` → 必須零結果
   - 搜尋 `"(true|false)"` → 必須零結果
   - 有任何結果 → 立即停止，修正後重新檢查
6. 執行 JSON 格式完整驗證
7. 瀏覽器測試（確認可正常運行）

## 🛡️ 黃金法則

1. ✅ **永遠使用 JavaScript 對象 + `JSON.stringify()`**（不要手寫 JSON 字符串）
2. ✅ **生成後立即執行正則檢查**（不可跳過！）
3. ❌ **避免手動編輯 JSON 區塊**（除非絕對必要）

## ⚠️ 最常見致命錯誤：數字後多雙引號

```json
❌ "y": 440",    // 數字後多了雙引號 → 整個地圖無法顯示
✅ "y": 440,     // 正確：數字後只有逗號
```

**如何避免**：使用 `JSON.stringify()` 而非手寫 JSON。

## 📋 JSON 格式規則

| 類型 | 正確 | 錯誤 |
|------|------|------|
| 數字 | `"x": 100` | `"x": "100"` 或 `"x": 100"` |
| 布林 | `"completed": false` | `"completed": "false"` |
| 字串 | `"id": "node-123"` | `"id": node-123` |

## 🔧 重要規範

- ⚠️ **所有元素的所有屬性都必須明確導出**（即使是默認值也不可省略）
  - Node 必須包含：`id, x, y, width, height, text, color, textAlign, completed, abandoned, link, isLabel`
  - Frame 必須包含：`id, x, y, width, height, label, borderColor, bgColor, zIndex, completed, abandoned`
  - Connection 必須包含：`id, from, to, arrowType, label, color`
- 所有 ID 必須是字符串格式：`"node-" + Date.now()`
- CSS 必須包含：`* { box-sizing: border-box; user-select: none; -webkit-user-select: none; }`
- **畫布拖曳防文字反白**：`mousedown` 必須調用 `e.preventDefault()`，防止移動畫布時意外框選反白文字
- **連線禁絕穿透與標籤拐點繞行**：
  - 長距離或跨層連線必須建立標籤拐點節點（`isLabel: true`）進行繞行，禁止橫穿無關節點
  - **標籤寫入節點內（Label in Node）**：說明文字一律寫在拐點節點內（`text`），連線上的 `label` 保持為空字串 `""`，避免連線折線疊字紊亂
  - **虛線註解對稱性（Dashed Waypoint）**：註解虛線經過拐點時，**拐點兩端連線必須同為 `arrowType: "dashed"`**，不可一實一虛
  - 拐點節點維持嚴格「一進一出」規範
- Icon 檔案必須與 Map 檔在同一目錄（使用專案指定的 `concept-crafter_icon.png`）
- **SVG 層不可使用 transform**（搜尋 `svgLayer.style.transform` 應無結果）
- **必須在瀏覽器中測試後才交付**

## 🔍 快速診斷

Map 完全不顯示（只有背景）→ 99% 是 JSON 格式錯誤：
1. 開啟瀏覽器開發者工具（F12）
2. 查看 Console 是否有 `JSON.parse` 錯誤
3. 搜尋 `\d+"` 找出問題位置
