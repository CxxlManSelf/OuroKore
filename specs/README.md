# OuroKore 規格手冊體系 (Specifications & Manuals)

歡迎查閱 **OuroKore** 核心系統之官方手冊與規格文件庫。

---

## 📚 1. 使用者說明書 (Developer Manual)

專為使用 OuroKore 框架開發應用系統（如遊戲邏輯、大規模世界編輯器、高效能後端圖託管）的軟體工程師設計：

1. [01. 系統概述與架構哲學](manual/01_introduction.md) - 心智模型、四大基石與三層邊界隔離哲學
2. [02. 5 分鐘快速上手](manual/02_quickstart.md) - 宿主初始化、自訂領域物件、屬性存取與存檔
3. [03. 領域物件設計規範](manual/03_domain_object_design.md) - Getter/Setter、OuroReadLock/OuroWriteLock、DCL 二次確認防死鎖、原子標髒
4. [04. Handle 拓撲管理系統](manual/04_handles_and_topology.md) - OwningHandle、UnboundHandle、OwningContainerHandle、OuroPtr
5. [05. 自動換頁脫水與儲存驅動](manual/05_dehydration_and_storage.md) - 記憶體脫水、透明按需復水、LRU 策略配置
6. [06. 宿主生命週期與特權管理](manual/06_host_lifecycle.md) - HostContext 獨佔特權、插件隔離防護、雙層安全退出防禦體系
7. [07. 樹狀結構容器與文字 DSL 指南](manual/07_tree_and_dsl.md) - TreeNode 雙模態容器、顯式堆疊走訪與正交無等號 DSL
8. [08. 基礎工具庫指南](manual/08_base_utilities.md) - DynamicLibrary 生命週期反向錨定、constexpr Hash、並行同步與 UTF-8
9. [09. 外掛 Heap 追蹤與記憶體防禦指南](manual/09_heap_and_memory.md) - 編譯期 A/B 方案、全域重載、外掛結束前洩漏檢驗與純 C ABI
10. [10. 公開 C++ API 參照手冊](manual/10_api_reference.md) - 完整公開 API 清單與核心型別定義（終端速查字典附錄）

---

## 🛠️ 2. 底層技術架構手冊 (Technical Specifications)

專為 OuroKore C++ 核心維護者、多語言 FFI 介面開發者與架構審查者設計：

1. [01. 系統架構設計規範](technical/01_system_architecture.md) - 三層隔離設計、圖論演算法、循環參照 GC 原理
2. [02. 二進位串流與藍圖打包協議](technical/02_binary_protocols.md) - 純 Payload 與拓撲分離、防記憶體爆炸與防重複鍵
3. [03. 純 C ABI 規格與記憶體佈局規範](technical/03_c_abi_and_memory.md) - 37 個純 C 導出函式規格、Pinning 弱引用防護、DCL 防禦、CRT 隔離
4. [04. 併發模型、鎖階層規範與防死鎖設計](technical/04_concurrency_and_locks.md) - 單向鎖順序、執行緒池防自我死鎖、物件鎖定 DCL 二次確認、進程退出無鎖防線
5. [05. ORKT 樹狀結構文字交換格式規範](technical/05_orkt_format_spec.md) - 中括號標頭容器語義、無等號語法、雜訊零參與、廣義根閉環 FSM 狀態機

---

## 🤖 3. 應用端專用 AI 技能手冊 (AI Developer Skill)

- [OuroKore 應用端 AI 技能指引](../skills/ourokore-app/SKILL.md) - AI 助理與應用端工程師專用最佳實踐、避坑指南與核心程式碼範本

---

## 🗺️ 4. 視覺化互動概念圖體系 (Interactive Concept Maps)

為解決純文字手冊在複雜圖論拓撲與多維狀態機上的認知負擔，手冊體系採取**「圖文互補、按需增強」**方針：
* **心智模型先行**：在核心拓撲章節開頭提供互動式 HTML 概念圖（支援滑鼠滾輪縮放、平移與節點探索），使開發者在 30 秒內建立全貌直覺。
* **概念圖目錄**：所有互動概念圖統一存放於 [manual/conceptmaps/](manual/conceptmaps/) 目錄：
  - 🗺️ **[OuroKore 系統全景與三層邊界互動概念圖](manual/conceptmaps/overview_map.html)**（對應《01. 系統概述與架構哲學》）
  - 🗺️ **[三大安全句柄與物件圖拓撲全景概念圖](manual/conceptmaps/handles_topology_map.html)**（對應《04. Handle 拓撲管理系統》）
  - 🗺️ **[自動換頁脫水、墓碑查詢與儲存驅動全景概念圖](manual/conceptmaps/dehydration_lifecycle_map.html)**（對應《05. 自動換頁脫水與儲存驅動》）
  - 🗺️ **[循環參照收集器與逆向圖論走訪全景概念圖](manual/conceptmaps/cycle_collector_map.html)**（對應《04. Handle 拓撲管理系統》第 2 節與技術手冊《01. 系統架構設計規範》第 3 節）
  - 🗺️ **[五層單向鎖階層與併發防死鎖全景概念圖](manual/conceptmaps/lock_hierarchy_map.html)**（對應技術手冊《04. 併發模型、鎖階層規範與防死鎖設計》）
* **文字規範兜底**：各章節 Markdown 文件始終維持作為具有最終工程約束力的標準字典。

---

## 🔍 5. 規格一致性驗證

本手冊體系受 `core_cpp/scripts/verify_specs.py` 自動化自檢腳本約束，確保核心 C ABI、受管類別與手冊 100% 保持同步。
