---
name: ourokore
description: "OuroKore 物件圖託管、弱引用代數系統、自動脫水換頁、藍圖序列化、HostContext 特權隔離與非同步執行系統之全能架構與維護技能。適用於應用程式開發指導、C++ 核心維護演進，以及規格手冊（specs）同步維護。"
---

# OuroKore 全能架構與核心維護技能 (OuroKore Architecture & Maintenance Skill)

本技能為 AI 助理提供對 **OuroKore** 核心框架的全方位架構理解、程式設計守則、避坑指南以及規格同步標準作業程序（SOP）。

---

## 🧭 1. 核心心智模型與架構哲學

OuroKore 是一個針對**超大規模物件圖（Large-Scale Object Graph）**、**極致執行緒安全**、**記憶體吃緊時自動換頁脫水（Dehydration/Rehydration）**以及**藍圖持久化打包（Blueprint Packaging）**所設計的高效能系統框架。

### 核心三大基石：
1. **控制區塊與 Handle 代數系統 (ControlBlock & Handle System)**：
   - 物件不由裸指標或標準 `std::shared_ptr` 直接持有，而是由全域唯一的 64 位元識別碼 `HandleID` 與底層控制區塊託管。
   - `OwningHandle<T>` / `OwningContainerHandle`：表示強引用與擁有權（邊緣拓撲），自動向所屬父物件註冊槽位（Slot）。內部業務拓撲（含雙向關聯）放膽使用，由 `CycleCollector` 背景非同步消化。
   - `UnboundHandle<T>`：無繫結句柄，不佔用物件圖入邊（In-degree = 0），專為動態外掛模組非同步熱卸載防釘死、生命週期解耦與旁路觀察設計，支援安全原子提升 (`LockAndAcquire()`)，具備惰性修剪（Lazy Pruning）機制，徹底杜絕懸掛野指標與模組生命週期被釘死之缺陷。
   - `OuroPtr<T>`：棧上 / 全域根引用守衛（Root Edge），內部自動調用 `ork_acquire_object_pointer` 與讀寫鎖。
   - ⚠️ **循環參照使用鐵律**：業務圖內部雙向互指（A <-> B）一律 100% 使用 `OwningHandle`，交由背景 `CycleCollector` 自動安全回收。**嚴禁為了「破環」而濫用 `UnboundHandle`**，`UnboundHandle` 的核心職能是跨動態外掛邊界解耦與非同步熱卸載防釘死。

2. **記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)**：
   - 物件生命週期具備四種儲存狀態：`UnsavedNew(0)`、`Clean(1)`、`Dirty(2)`、`Dehydrated(3)`。
   - 當記憶體壓力觸發或由脫水器（如 `OuroLRUAutoDehydrator`）挑選物件時，核心執行 `ork_dehydrate_object`：將 Dirty 物件序列化落盤，安全銷毀 Payload 實體記憶體，保留控制區塊（ControlBlock）。
   - 當任何執行緒嘗試存取已脫水物件時，框架透過註冊的 `ork_rehydrate_fn_t` 回呼透明地自儲存驅動（`IStorageDriver`）復原記憶體實體，對使用者完全透明。

3. **三層權限隔離與 HostContext 獨佔特權**：
   - 凡涉及全進程生命週期（`Shutdown`、`Reset`）、基礎設施注入（`IStorageDriver`、`IAutoDehydrator`）、全域排程與排空（`FlushStorage`、`FlushDeferredDeletions`、`CollectCycles`）等特權，**必須收斂至 HostContext**。
   - 第三方插件僅能使用受管物件、OuroPtr、Handle 拓撲與讀寫鎖，物理隔離所有特權 API。

---

## 🛠️ 2. 應用開發指南 (Application Developer Guide)

### 2.1 主程式 Entry Point (HostContext)
```cpp
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>

auto storage = std::make_shared<ork::InMemoryStorage>();
ork::HostContext host = ork::Init(storage);
assert(host.IsValid());
```

### 2.2 定義受管物件 (Inherit OuroObject)
所有受管物件必須繼承自 `ork::OuroObject`，禁止外部直接 `new`：

```cpp
#include <ourokore/component/OuroCore.hpp>

class Monster : public ork::OuroObject {
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
