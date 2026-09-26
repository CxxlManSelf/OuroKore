# 05. 自動換頁脫水與儲存驅動 (Dehydration & Storage)

OuroKore 具備針對超大型世界物件圖的記憶體自動分頁技術，長時間未活動的物件自動釋放實體記憶體，存取時透明復原。

---

## 🧊 1. 脫水 (Dehydration) 的運作機制

當物件長時間未被存取且滿足脫水條件時：
1. **檢查 Root 計數**：若 `root_count > 0`（代表有執行緒持有 `OuroPtr`），脫水程序安全略過，絕不打擾活躍業務。
2. **藍圖序列化**：若狀態為 `Dirty` 或 `UnsavedNew`，自動呼叫儲存驅動落盤。
3. **實體記憶體卸載**：銷毀 C++ 物件實體記憶體，控制區塊（ControlBlock）保留於記憶體中並標記為 `Dehydrated` 墓碑。
4. **Handle 拓撲不變性**：物件的全域唯一識別碼 `HandleID` 與指向它的所有 Handle **完全保持不變**。

---

## 💧 2. 透明按需復水 (Transparent Rehydration)

當任何程式碼透過 `handle.Get()`、`handle.LockAndAcquire()` 或 `OuroPtr` 嘗試存取已處於 `Dehydrated` 狀態的物件時：
* 控制區塊自動攔截該請求。
* 觸發預先註冊的復水回呼，自儲存驅動讀取藍圖串流。
* 在模組 CRT 中重新 `new T()` 配置記憶體，反序列化狀態並重新綁定至原控制區塊。
* **呼叫端代碼對這一切完全無感！**

---

## ⚙️ 3. 配置儲存驅動與 LRU 自動脫水器

```cpp
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>
#include <ourokore/component/builtin/OuroLRUAutoDehydrator.hpp>

// 1. 建立儲存驅動
auto storage = std::make_shared<ork::InMemoryStorage>();

// 2. 建立 LRU 自動脫水器（最大快取 1000 個物件）
auto lru = std::make_shared<ork::OuroLRUAutoDehydrator>(storage, 1000);

// 3. 宿主初始化時注入
ork::HostContext host = ork::Init(storage, lru);

// 隨時可由 Host 調整或更換脫水策略
host->SetAutoDehydrator(lru);
```
