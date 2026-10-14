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

> [!TIP]
> **多執行緒並發復水安全保證 (Double-Checked Locking Invariant)**：
> 當多個工作執行緒同時嘗試對同一個已脫水 HandleID 進行復水時，核心透過 ControlBlock 獨佔寫鎖與雙重檢查鎖機制（`StorageState != Dehydrated` 且 `ork_is_payload_loaded == true`），保證同一時間僅有第一個取得鎖的執行緒執行藍圖反序列化，其餘執行緒安全早退，絕不重複載入或覆寫實體指標。

---

## 🔍 5. 儲存狀態與零 I/O 墓碑查詢保證 (Zero-I/O StorageState)

```cpp
// 查詢物件儲存四態：UnsavedNew(0), Clean(1), Dirty(2), Dehydrated(3)
ork::StorageState state = ork::GetStorageState(boss_id);
if (state == ork::StorageState::Dehydrated) {
    std::cout << "物件目前已脫水落盤，記憶體已釋放" << std::endl;
}

// 查詢實體 Payload 肉體是否已載入記憶體中（純記憶體讀取，零 I/O 零復水，不要求強引用存活）
bool is_loaded = ork::IsPayloadLoaded(boss_id);

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

---

## 💾 7. 全域狀態保存與兩階段藍綠載入 (Save & Blue-Green Load)

除了透明脫水與復水外，OuroKore 提供應用端顯式全域或物件級的保存（`ork::Save`）與全圖藍綠熱載入（`ork::Load`）。

### 7.1 事務保存與髒物件落盤 (`ork::Save`)
```cpp
// 1. 保存單一物件（若狀態為 Dirty 或 UnsavedNew 則執行序列化落盤並原子標記為 Clean）
ork::Save(boss_ptr);

// 2. 依 HandleID 保存
ork::Save(boss_id);

// 3. 非同步背景保存
std::future<ork::AsyncResult<void>> save_fut = ork::SaveAsync(boss_ptr);
```

### 7.2 兩階段藍綠陰影置換載入 (`ork::Load`)
載入新存檔或藍圖時，若直接在現有物件上原地覆寫，一旦載入過程中遭遇檔案損毀或反序列化失敗，將使整張運行圖處於殘破不可用的災難狀態；且若有其他執行緒正在並發存取，更會引發資料競爭或野指標崩潰。

為此，OuroKore 實現了**兩階段藍綠陰影置換（Two-Phase Blue-Green Shadow Swap）**架構：

```
[ 執行 ork::Load(archive) ]
         │
         ├── 【第一階段：陰影建構 (Phase 1)】
         │    在完全獨立的沙盒空間中反序列化所有物件與邊緣拓撲。
         │    🛡️ 強例外安全：若發生任何格式毀損或例外，全盤銷毀陰影物件，原運行中系統 100% 毫髮無傷！
         │
         └── 【第二階段：原子藍綠置換 (Phase 2)】
              1. 獲取全域置換互斥鎖（m_swap_mutex），序列化所有置換作業，徹底杜絕多執行緒置換引發的「雙世代槽位別名（Slot Aliasing）」與「活鎖飢餓（Live-Lock）」。
              2. 逐一獲取物件 ControlBlock 之獨佔寫鎖（m_rw_lock）。
              3. 在持有寫鎖的臨界區內原子替換 Payload 指標與世代號（m_generation++），並立即原子標記 m_storage_state = StorageState::Clean。
                 🛡️ 狀態防覆蓋保證：徹底消除解開寫鎖至標記 Clean 之間的狀態空隙，防範覆蓋其他執行緒並發寫入標記的 Dirty！
              4. 釋放寫鎖，在鎖外自旋等待舊世代槽位（m_pins[old_gen % 2]）在途調用釘住計數排空歸零（Drain）。
              5. 呼叫舊物件解構函式（old_destroy_fn）。
                 🛡️ 標準 C++ 解構生命週期保障：解構子 ~T() 執行期間內部註冊的 Handles 完整可用，完全由 Handle 解構子依序自然斷開邊緣。
              6. 顯式解開 swap_lock 之後再釋放置換 Pin 守衛，保證多執行緒下 ControlBlock 生命週期安全。
```

#### 🛡️ 核心安全性與並發保證：
1. **雙世代在途調用釘住（Dual-Generation Pinning）**：
   - 每次置換時將世代號遞增，使得所有新進調用自動流向新世代槽位（`m_pins[new_gen % 2]`）。
   - 置換端僅需等待舊世代槽位（`m_pins[old_gen % 2]`）釘住計數排空歸零，即可安全銷毀舊實體，消滅活鎖飢餓。
2. **`m_swap_mutex` 置換操作序列化**：
   - 徹底消滅多執行緒並發呼叫 `SwapObjectPayload` 引發的槽位別名與並發碰撞。
3. **舊實體析構生命週期完備**：
   - 不提前強制清空 Handle，完全由 C++ 標準解構流程依序釋放，確保領域物件的解構子內部若有存取自身 Handles 或日誌輸出時不會遭遇空指針。
4. **孤島拆解態即刻拒絕**：
   - 若物件已被標記為孤島拆解中（`m_is_destructing == true`），置換操作第一時間安全退出，絕不污染即將銷毀的控制區塊。
