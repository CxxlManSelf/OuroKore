---
name: ourokore
description: "OuroKore 物件圖託管、弱引用代數系統、自動脫水換頁、藍圖序列化、HostContext 特權隔離與非同步執行系統之全能架構與維護技能。適用於應用程式開發指導、C++ 核心維護演進，以及規格手冊（specs）同步維護。"
---

# OuroKore 全能架構與核心維護技能 (OuroKore Architecture & Maintenance Skill)

本技能為 AI 助理提供對 **OuroKore** 核心框架的全方位架構理解、程式設計守則、避坑指南以及規格同步標準作業程序（SOP）。

---

## 🧭 1. 核心心智模型與架構哲學

OuroKore 是一個針對**超大規模物件圖（Large-Scale Object Graph）**、**極致執行緒安全**、**記憶體吃緊時自動換頁脫水（Dehydration/Rehydration）**以及**藍圖持久化打包（Blueprint Packaging）**所設計的高效能系統框架。

### 核心四大基石：
1. **控制區塊與 Handle 代數系統 (ControlBlock & Handle System)**：
   - 物件不由裸指標或標準 `std::shared_ptr` 直接持有，而是由全域唯一的 64 位元識別碼 `HandleID` 與底層控制區塊託管。
   - `OwningHandle<T>` / `OwningContainerHandle`：表示強引用與擁有權（邊緣拓撲），自動向所屬父物件註冊槽位（Slot）。內部業務拓撲（含雙向關聯）放膽使用，由 `CycleCollector` 背景非同步消化。
   - `UnboundHandle<T>`：純旁觀者句柄（只記住電話號碼，絕不干涉對方生死）。專為「外掛隨時卸載防卡死」、「UI 暫時瞄一眼」等情境設計。要使用時打電話確認（`LockAndAcquire()`），對方在就安心用，對方若已銷毀或卸載就自動傳回 null 並擦乾淨記錄，絕不強留對方。
   - `OuroPtr<T>`：棧上 / 全域根引用守衛（Root Edge），內部自動調用 `ork_acquire_object_pointer` 與讀寫鎖。
   - ⚠️ **循環參照使用鐵律**：業務圖內部雙向互指（A <-> B）一律 100% 使用 `OwningHandle`，交由背景 `CycleCollector` 自動安全回收。**千萬不要為了「破環」而濫用 `UnboundHandle`**，只有在你「完全不想為對方的生命週期負責」時才使用它。

2. **記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)**：
   - 物件生命週期具備四種儲存狀態：`UnsavedNew(0)`、`Clean(1)`、`Dirty(2)`、`Dehydrated(3)`。
   - 當記憶體壓力觸發或由脫水器（如 `OuroLRUAutoDehydrator`）挑選物件時，核心執行 `ork_dehydrate_object`：將 Dirty 物件序列化落盤，安全銷毀 Payload 實體記憶體，保留控制區塊（ControlBlock）。
   - 當任何執行緒嘗試存取已脫水物件時，框架透過註冊的 `ork_rehydrate_fn_t` 回呼透明地自儲存驅動（`IStorageDriver`）復原記憶體實體，對使用者完全透明。

3. **全域型別系統與 ControlBlock 墓碑長存 (Type System & Dehydration Invariant)**：
   - 每個受管物件在兩階段構造時分配並鎖定 64 位元唯一 `TypeID`，儲存於 ControlBlock 墓碑中。
   - 物件即使脫水進入磁碟，型別資訊永不丟失；呼叫 `ptr.Is<T>()`、`ork_is_instance_of` 或 `ptr.GetTypeID()` 為**零 I/O 純記憶體查詢**，絕對不會誘發穿透性復水。

4. **三層權限隔離與 HostContext 獨佔特權**：
   - 凡涉及全進程生命週期（`Shutdown`、`Reset`）、基礎設施注入（`IStorageDriver`、`IAutoDehydrator`）、全域排程與排空（`FlushStorage`、`FlushDeferredDeletions`、`CollectCycles`）等特權，**必須收斂至 HostContext**。
   - 第三方插件僅能使用受管物件、OuroPtr、Handle 拓撲與讀寫鎖，物理隔離所有特權 API。

---

## 🛠️ 2. 應用開發指南 (Application Developer Guide)

### 2.0 語言標準規範 (C++20 Standard Invariant)
> ⚠️ **編譯標準宣告與使用端規範**：
> OuroKore 核心程式庫（`ourokore_base`、`ourokore_core`）全部嚴格採用 **ISO C++20 標準**（`CMAKE_CXX_STANDARD 20`，停用編譯器擴展）建立。
> **程式庫的使用端（無論是 Host 主程式或第三方插件 Component）強烈建議一律採用 C++20 或更高標準（C++20+）進行開發與編譯**。這能確保 C++ 模板（如 `OwningHandle<T>`、`OuroPtr<T>`）、STL 物件佈局、同步原語與記憶體模型之 100% 相容，避免跨標準混合編譯導致的潛在未定義行為或語法問題。

### 2.0.1 全域文字訊息標準 (UTF-8 Standard Invariant)
> ⚠️ **全域字串編碼鐵律**：
> OuroKore 系統全體（屬性 Key、插槽名稱、錯誤文字、日誌、二進位字串與 FFI 邊界）**唯一強制使用 UTF-8 編碼**。
> Windows 下涉及路徑或系統呼叫必須在底層顯式轉換為 `std::wstring` 呼叫 `W` 版 API，對外與對內一律回歸 UTF-8，嚴禁混用 ANSI 本地編碼（CP950/Big5/GBK 等）。

### 2.0.2 外掛插件 CMake 建置規範 (Plugin CMake MODULE Invariant)
> ⚠️ **外掛 Target 宣告鐵律**：
> 凡是作為 OuroKore 動態外掛（Plugin / Component，透過 `DynamicLibrary` 動態載入）的模組，在 CMake 中**一律強制使用 `add_library(<name> MODULE ...)`**，嚴禁宣告為 `SHARED`！
> - **核心原因**：`MODULE` 在 CMake 中代表「不可在編譯期被其他 Target 靜態鏈結，僅供執行期動態加載（`LoadLibrary` / `dlopen`）」。若誤用 `SHARED`，可能導致其他模組誤用 `target_link_libraries` 依賴它，破壞動態插件可隨時卸載與熱更新之架構隔離性。
> - **標準配置範本**：
>   ```cmake
>   add_library(MyPlugin MODULE MyPlugin.cpp)
>   target_link_libraries(MyPlugin PRIVATE ourokore_core ourokore_base)
>   set_target_properties(MyPlugin PROPERTIES 
>       PREFIX ""
>       LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
>   )
>   ```

### 2.1 主程式 Entry Point (HostContext)
```cpp
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>

auto storage = std::make_shared<ork::InMemoryStorage>();
ork::HostContext host = ork::Init(storage);
assert(host.IsValid());
// host 遵循 RAII 自動生命週期管理，離開作用域時解構式會自動觸發優雅關閉（Shutdown），無需且不建議手動呼叫。
```

### 2.2 定義受管物件與型別宣告 (Inherit OuroObject & ORK_OBJECT)
所有受管物件必須繼承自 `ork::OuroObject`，並推薦使用 `ORK_OBJECT(ClassName, ParentClassName)` 巨集自動註冊型別階層：

```cpp
#include <ourokore/component/OuroCore.hpp>

class Monster : public ork::OuroObject {
    ORK_OBJECT(Monster, ork::OuroObject)
public:
    ork::OwningHandle<Monster>          m_minion{"MinionSlot"};
    ork::UnboundHandle<ork::OuroObject> m_plugin_module; // 無繫結引用，防止釘死動態 DLL 模組非同步卸載

    int32_t GetHp() const { ork::OuroReadLock lock(*this); return m_hp; }
    void SetHp(int32_t hp) { ork::OuroWriteLock lock(*this); m_hp = hp; }

    void SerializePayload(ork::OuroStream &stream) const override {
        stream.WriteProperty("hp", m_hp);
    }

    void DeserializePayload(ork::OuroStream &stream) override {
        stream.ReadProperty("hp", m_hp);
    }

private:
    int32_t m_hp{100};
};
```

### 2.3 型別識別與安全向下/向上轉型 (Type System & Safe Casting)
OuroKore 核心透過 ControlBlock 墓碑長存 64 位元唯一 `TypeID` 與全域型別繼承拓撲（TypeRegistry），支援極速、零指標解引用且**脫水狀態下絕不觸發復水**的安全轉型：

```cpp
ork::OuroPtr<Creature> creature = ork::CreateObject<BossMonster>();

// 1. 多型型別檢查（純 ControlBlock 查詢，零 I/O 消耗）
if (creature.Is<BossMonster>()) {
    // 2. 向下安全轉型（若型別不符回傳空 OuroPtr，合法則安全增持根引用）
    ork::OuroPtr<BossMonster> boss = creature.As<BossMonster>();
    // 亦支援 STL 風格轉型：ork::dynamic_pointer_cast<BossMonster>(creature);
}

// 3. 右值所有權移動轉型（零引用計數變更開銷）
ork::OuroPtr<Monster> monster = std::move(creature).As<Monster>();
```

### 2.4 Base 通用現代雜湊工具模組 (Hash Utilities)
在 `<ourokore/base/Hash.hpp>` 中提供 C++20 標準高效能雜湊工具，全面支援 `constexpr` 編譯期常數計算：
```cpp
#include <ourokore/base/Hash.hpp>
using namespace ork::base::literals;

// 1. FNV-1a 64-bit（型別系統 TypeID、字串識別碼預設演算法）
constexpr uint64_t type_id = "MyPluginComponent"_fnv64;
uint64_t runtime_hash = ork::base::Fnv1a64(str_view);

// 2. CRC32 (IEEE 802.3，防竄改與封包/藍圖完整性校驗)
constexpr uint32_t magic = "OURO_BLUEPRINT"_crc32;
uint32_t checksum = ork::base::Crc32(data_span);

// 3. MurmurHash3 32-bit（高品質分佈與雪崩效應雜湊）
uint32_t seed = 0x9747b28c;
uint32_t hash32 = ork::base::MurmurHash3(data_span, seed);

// 4. HashCombine 變參組合
size_t combined = 0;
ork::base::HashCombine(combined, obj_id, slot_name, timestamp);
```

### 2.5 脫水安全判空與復水存取最佳實踐 (Dehydration-Safe Null Check)
- **純狀態與存活判定（絕不觸發復水）**：
  ```cpp
  if (creature) { ... }          // 內部調用 ork_check_alive，純查詢 ControlBlock
  if (creature.IsAlive()) { ... } // 純 ControlBlock 活躍判定，零 I/O
  if (creature.Is<Boss>()) { ... } // 純 ControlBlock TypeID 判定，零 I/O
  ```
- **取得實體記憶體指標（若脫水則透明復水）**：
  ```cpp
  creature->Attack();           // operator->() 觸發 ork_acquire_object_pointer，必要時自磁碟載入
  Creature* raw = creature.Get(); // 觸發復水
  ```

---

## ⚖️ 3. 核心擴展鐵律 (Core Contributor Invariants)

在增修 C++ 核心 (`core/` 與 `include/ourokore/`) 時，必須誓死遵守以下規範：

1. **C ABI 邊界嚴禁洩漏 C++ 例外 (No C++ Exception Leaks)**：
   - 位於 `core.h`、`component_api.h` 與 `host_api.h` 的所有 `ork_*` 導出函式，內部必須使用 `try-catch (...)` 包裹，嚴格轉換為 `ORK_STATUS_*` 錯誤碼。
   - 符號跨動態庫呼叫慣例統一為 `ORK_CALL`。
2. **跨模組 CRT 隔離與自定義 Deleter (CRT Isolation)**：
   - 物件的記憶體釋放必須在其建立模組的 CRT 堆疊中執行（透過 `ork_destroy_fn_t` 回呼）。禁止在 `core.dll` 內部直接對來自 plugin 的 `OuroObject*` 呼叫 `delete`。
3. **單向鎖階層順序 (Lock Ordering)**：
   - 嚴格遵循 `Registry 鎖 -> ControlBlock 鎖 -> 佇列鎖`。
   - 禁止在持有子物件獨占鎖的情況下逆向索取父物件的獨占鎖。
4. **內部自救權杖與預留情境雙重保護 (Passkey & Reservation Context Guard)**：
   - 核心底層 OOM 自救通道（`TriggerRuntimeRescue`）必須受 `OuroCreationToken`（Passkey Pattern，私有建構子）保護，且核心內部必須校驗 HandleID 是否正處於合法預留或脫水狀態（`IsValidRescueContext`）。嚴禁向插件暴露可主動調用之全域記憶體調度 API。
   - 所有底層內部回呼（如 `RehydrateCallback`）回傳型別為 `void`，由 `detail::Rehydrator` 類別進行私有封裝（Private static），嚴格收斂至 `ork::detail` 內部命名空間，嚴禁對外暴露裸指標或允許插件任意調用。
5. **外掛模組建置規範 (Plugin MODULE Target Invariant)**：
   - 任何專案內部的測試動態外掛（如 `test_plugin_dll`）或第三方 Component 範例，在 CMake 中必須統一使用 `add_library(<name> MODULE ...)` 並清除前綴（`PREFIX ""`），嚴禁編譯為可被靜態鏈結的 `SHARED` 導入庫，以維持執行期動態加載的純淨隔離性。
6. **全域 TypeID 雜湊標準統一 (Fnv1a64 Invariant)**：
   - 核心所有型別唯一碼（`ork_type_id_t`）、編譯期 `ORK_OBJECT` 巨集、執行期字串型別註冊與查詢，**一律統一採用 `ork::base::Fnv1a64` 計算**。嚴禁在核心不同模組或外掛中各搞一套手寫雜湊邏輯，確保跨模組與脫水反序列化識別碼 100% 絕對一致。

---

## 🔄 4. 變更連動與規格同步 SOP (Mandatory Sync Protocol)

當您修改或擴充了 OuroKore 系統時，**必須執行以下檢核流程**：

```text
[修改程式碼] 
     │
     ▼
[執行測試：build/bin 下測試執行檔] ──(失敗)──> [修復程式碼]
     │ (通過)
     ▼
[檢查規格連動]
  1. API 簽名變更？ ───> 更新 `specs/manual/07_api_reference.md`
  2. C ABI / 導出函式變更？ ───> 更新 `specs/technical/03_c_abi_and_memory.md`
  3. 二進位串流/藍圖格式變更？ ───> 更新 `specs/technical/02_binary_protocols.md`
  4. 拓撲或架構演算法變更？ ───> 更新 `specs/technical/01_system_architecture.md`
  5. 應用端介面/範本變更？ ───> 強制更新 `skills/ourokore-app/SKILL.md`
     │
     ▼
[執行同步驗證腳本]
  python scripts/verify_specs.py
     │
     ▼
[更新完成，提交變更]
```
