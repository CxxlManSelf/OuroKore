---
name: ourokore-app
description: "專為 OuroKore 應用程式與外掛開發人員設計的 AI 輔助開發技能。提供自訂 OuroObject 物件設計、Handle 拓撲管理、執行緒安全讀寫鎖（OuroReadLock/OuroWriteLock）、藍圖打包序列化、LRU 自動換頁脫水配置，以及避坑最佳實踐與實戰程式碼範本。"
---

# OuroKore 應用開發者指南 (OuroKore Application Developer Skill)

本技能專為使用 **OuroKore** 框架構建應用程式（如遊戲邏輯、大規模世界編輯器、高效能物件後端系統）的軟體工程師與 AI 助理設計。

---

## 💡 1. 應用開發者必備心智模型

1. **永遠不直接使用裸指標或 `std::shared_ptr` 管理領域物件**：
   - 領域物件的生命週期、持久化與記憶體換頁完全由 OuroKore 核心接管。
   - 所有實例化操作一律呼叫 `ork::CreateObject<T>(args...)`，它會回傳棧上安全保護的 `ork::OuroPtr<T>`。
2. **三種 Handle 職責分工**：
   - `OwningHandle<T>`：宣告單一子物件插槽（擁有權拓撲邊緣），子物件生命週期由父物件持有。業務圖內部雙向與網狀關聯亦直接使用 `OwningHandle`，充分享受背景 `CycleCollector` 的非同步卸載。
   - `OwningContainerHandle`：宣告動態子物件容器（如背包物品清單）。
   - `UnboundHandle<T>`：宣告**非擁有型、生命週期解耦之引用**。專為**動態模組/DLL 插件熱卸載（Hot-Reload / Dynamic Unload）防釘死、可再生服務實體與旁路觀察**設計。存取時透過 `.LockAndAcquire()` 暫時換取 `OuroPtr<T>`，平時不佔用擁有權拓撲邊緣，允許目標隨時安全被卸載或重載。
3. **執行緒安全與自動 Dirty 標記（重要！）**：
   - 物件的純資料屬性（Payload）請一律封裝在 `private` 或 `protected` 中。
   - 讀取時使用 `ork::OuroReadLock`（共用讀鎖）。
   - 修改時使用 `ork::OuroWriteLock`（獨占寫鎖）。**當 `OuroWriteLock` 離開作用域解構時，內部會自動原子化標記為 Dirty 並解鎖**，杜絕多執行緒 Data Race 與髒污狀態遺失。
4. **記憶體換頁（脫水與復水）完全透明**：
   - 長時間未被存取的物件會由脫水器自動釋放記憶體（保留空殼控制區塊）。
   - 當程式再次呼叫 `handle.Get()` 或 `handle.LockAndAcquire()` 時，框架會透明地自儲存體重新還原物件，呼叫端無須撰寫額外載入邏輯。

---

## 📝 2. 自訂領域物件開發範本 (Standard Component Template)

所有領域物件必須繼承自 `ork::OuroObject`，並推薦使用 **Setter + OuroWriteLock** 確保多執行緒安全：

```cpp
#include <ourokore/component/OuroCore.hpp>
#include <string>

class Monster : public ork::OuroObject {
public:
    // ----------------------------------------------------
    // 1. 拓撲槽位與弱引用宣告 (Handle Slots & Weak References)
    // ----------------------------------------------------
    // 擁有權槽位：參數為 Slot 唯一名稱（內部圖拓撲，包含雙向/樹狀關係一律使用 OwningHandle）
    ork::OwningHandle<Monster> m_pet{"PetSlot"};
    
    // 弱引用：用於關聯外部可動態卸載的模組/插件或可再生服務（防止外部強引用釘死 DLL）
    ork::UnboundHandle<ork::OuroObject> m_plugin_module;

    // ----------------------------------------------------
    // 2. 執行緒安全之屬性存取介面 (Thread-Safe Accessors)
    // ----------------------------------------------------
    std::string GetName() const {
        ork::OuroReadLock lock(*this);
        return m_name;
    }

    void SetName(std::string name) {
        ork::OuroWriteLock lock(*this); // 取得寫入獨占鎖；解構時自動原子標記 Dirty
        m_name = std::move(name);
    }

    int32_t GetHp() const {
        ork::OuroReadLock lock(*this);
        return m_hp;
    }

    void SetHp(int32_t hp) {
        ork::OuroWriteLock lock(*this); // 離開作用域時自動觸發 ork_mark_dirty()
        m_hp = hp;
    }

    int32_t GetAttack() const {
        ork::OuroReadLock lock(*this);
        return m_attack;
    }

    void SetAttack(int32_t attack) {
        ork::OuroWriteLock lock(*this);
        m_attack = attack;
    }

    // ----------------------------------------------------
    // 3. 序列化協議實作 (Pure Payload Serialization)
    // ----------------------------------------------------
    void SerializePayload(ork::OuroStream &stream) const override {
        // 注意：SerializePayload 內部由核心保存機制呼叫，無須額外鎖定 Slot 拓撲
        stream.WriteProperty("name", m_name);
        stream.WriteProperty("hp", m_hp);
        stream.WriteProperty("attack", m_attack);
    }

    void DeserializePayload(ork::OuroStream &stream) override {
        stream.ReadProperty("name", m_name);
        stream.ReadProperty("hp", m_hp);
        stream.ReadProperty("attack", m_attack);
    }

private:
    // ----------------------------------------------------
    // 4. 純資料欄位 (Pure Payload Fields，封裝保護)
    // ----------------------------------------------------
    std::string m_name{"Goblin"};
    int32_t     m_hp{100};
    int32_t     m_attack{15};
};
```

---

## 🚀 3. 常見實戰模式 (Cookbook)

### 模式 A：物件建立、雙向關聯與弱引用外掛存取
```cpp
// 1. 建立根物件（持有 Root 邊緣）
ork::OuroPtr<Monster> boss = ork::CreateObject<Monster>();
boss->SetName("黑龍領主");
boss->SetHp(5000);

// 2. 建立寵物子物件並掛載至槽位
ork::OuroPtr<Monster> drake = ork::CreateObject<Monster>();
drake->SetName("幼龍");
boss->m_pet.Set(drake); // 由 boss 持有 drake 的擁有權

// 3. 關聯外部動態 DLL 模組（使用 UnboundHandle，避免釘死動態庫導致無法卸載）
ork::OuroPtr<ork::OuroObject> dynamic_plugin = LoadPluginFromDll("AIPlugin.dll");
boss->m_plugin_module = dynamic_plugin;

// 4. 弱引用安全存取 (Anti-Dangling Guard & Decoupled Access)
if (ork::OuroPtr<ork::OuroObject> plugin = boss->m_plugin_module.LockAndAcquire()) {
    // 目標存活且已取得根鎖定，安全執行操作
    std::cout << "插件模組在線，執行功能！" << std::endl;
} else {
    // 模組已動態卸載或銷毀，內部自動完成惰性修剪 (Lazy Pruning)
    std::cout << "插件模組已熱卸載或未載入" << std::endl;
}
```

### 模式 B：物件修改與執行緒安全同步 (Thread-Safe Mutation)
```cpp
// 方式一（強烈推薦）：透過封裝好的 Setter（內部自動使用 OuroWriteLock）
// Setter 作用域結束時，OuroWriteLock 解構會原子性標記 Dirty 並釋放獨占鎖
boss->SetHp(boss->GetHp() - 500);

// 方式二：跨多個欄位批次修改時，使用 OuroWriteLock 區塊守衛
{
    ork::OuroWriteLock lock(*boss); // 取得 ControlBlock 獨占寫入鎖
    // 在獨占保護期間進行修改，徹底杜絕 Data Race 與脫水搶奪
    boss->SetName("狂暴的黑龍領主");
    boss->SetAttack(boss->GetAttack() * 2);
} // 離開作用域時自動 atomic mark dirty 並解鎖
```

### 模式 C：藍圖序列化、持久化與手動脫水
```cpp
// 1. 同步儲存至持久化驅動
ork::Save(boss);

// 2. 非同步背景儲存（不卡頓遊戲主迴圈）
std::future<ork::AsyncResult<Monster>> future = ork::SaveAsync(boss);
// ... 主迴圈繼續執行 ...
auto result = future.get();
if (result.success) {
    std::cout << "背景存檔完成！ID: " << result.id << std::endl;
}

// 3. 手動發起非同步脫水（右值移動所有權語意）
ork::DehydrateAsync(std::move(boss));
```

---

## ⚠️ 4. 應用開發高壓線條款 (Critical Invariants)

1. **嚴禁在棧上或全域宣告 `OwningHandle<T>`**：
   * `OwningHandle` 僅能作為繼承自 `OuroObject` 的成員變數使用。
   * 棧上與臨時變數請一律使用 `OuroPtr<T>`！違者在 Debug 模式下會觸發 Fail-Fast 斷言拋出例外。
2. **嚴禁在外部直接使用 `new` 或 `delete` 操作受管物件**：
   * 物件必須透過 `ork::CreateObject<T>()` 建立。
   * 物件解構與記憶體釋放由 OuroKore 拓撲與延遲隊列自動接管，手動 `delete` 將導致全進程崩潰。
3. **避免在持鎖期間執行耗時操作**：
   * 在持有 `OuroWriteLock` 或 `OuroReadLock` 時，禁止執行磁碟 I/O、網路傳輸或沉重演算法，避免引發鎖爭用或死鎖。
4. **第三方插件物理隔離保證**：
   * 第三方插件僅需引入 `<ourokore/component/OuroCore.hpp>`。
   * 插件絕對不應嘗試呼叫宿主特權 API（如 `Shutdown`、`FlushStorage`、`CollectCycles` 等），這些特權皆受 `HostContext` 嚴格防護。
