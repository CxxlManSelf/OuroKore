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

## 📦 3. 手動脫水與右值消耗語意 (Manual Dehydration & Move Consume)

除了由背景脫水器自動換頁外，應用端亦可依業務邏輯主動發起脫水：

```cpp
ork::OuroPtr<Monster> boss = ork::CreateObject<Monster>();
ork::HandleID boss_id = boss.GetTargetID();
ork::Save(boss); // 脫水前確保狀態落盤

// 方式一：右值移動消耗脫水（強烈推薦）
// ⚠️ 注意：傳入的原 boss 指標將被立即釋放並重置清空，杜絕懸空指針！
bool dehydrated = ork::Dehydrate(std::move(boss));
assert(!boss); // 原指標已安全清空

// 方式二：非同步背景脫水（不卡頓主執行緒）
// std::future<ork::AsyncResult<void>> future = ork::DehydrateAsync(boss_id);

// 方式三：依 HandleID 脫水
// bool ok = ork::Dehydrate(boss_id);
// 若此時有其他執行緒持有 OuroPtr（root_count > 0），脫水將安全略過並回傳 false
```

---

## 🔄 4. 手動顯式復水 (Explicit Rehydration)

在絕大多數場景下，推薦依賴 **透明按需復水**（直接調用 `handle.Get()` 或 `OuroPtr` 方法）。若特定場景需要在背景預先載入，亦可主動顯式復水：

```cpp
// 1. 同步顯式復水：配置新空殼、載入藍圖並回傳全新活躍 OuroPtr
ork::OuroPtr<Monster> restored_boss = ork::Rehydrate<Monster>(boss_id);
restored_boss(&Monster::Attack);

// 2. 非同步背景復水（預先熱身）：
std::future<ork::AsyncResult<Monster>> future = ork::RehydrateAsync<Monster>(boss_id);
// ... 主迴圈繼續執行其他任務 ...
auto result = future.get();
if (result.success) {
    ork::OuroPtr<Monster> async_boss = std::move(result.ptr);
    async_boss(&Monster::Attack);
}
```

> [!NOTE]
> **私有封閉防禦保證**：
> 核心內部的復水底層回呼（`RehydrateCallback`）被嚴格收斂於私有實作中，回傳值為 `void` 且透過 ControlBlock 私有綁定，嚴格杜絕外洩物件原始裸指標。

---

## 🔍 5. 儲存狀態與零 I/O 墓碑查詢保證 (Zero-I/O StorageState)

```cpp
// 查詢物件儲存四態：UnsavedNew(0), Clean(1), Dirty(2), Dehydrated(3)
ork::StorageState state = ork::GetStorageState(boss_id);
if (state == ork::StorageState::Dehydrated) {
    std::cout << "物件目前已脫水落盤，記憶體已釋放" << std::endl;
}

// 🛡️ 零 I/O 保證：
// 以下查詢純比對記憶體中之 ControlBlock 墓碑，絕對不會誘發磁碟 I/O 復水：
bool alive = ork::IsAlive(boss_id);
uint32_t roots = ork::GetRootEdgeCount(boss_id);
```

---

## ⚙️ 6. 配置儲存驅動與 LRU 自動脫水器

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
host.SetAutoDehydrator(lru);
```
