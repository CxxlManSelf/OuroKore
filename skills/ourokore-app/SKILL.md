---
name: ourokore-app
description: "專為 OuroKore 應用程式與外掛開發人員設計的 AI 輔助開發技能。提供自訂 OuroObject 物件設計、Handle 拓撲管理、OuroPtr 安全運算子轉發調用 (operator())、執行緒安全讀寫鎖（OuroReadLock/OuroWriteLock）、藍圖打包序列化、LRU 自動換頁脫水配置，以及避坑最佳實踐與實戰程式碼範本。"
---

# OuroKore 應用開發者指南 (OuroKore Application Developer Skill)

本技能專為使用 **OuroKore** 框架構建應用程式（如遊戲邏輯、大規模世界編輯器、高效能物件後端系統）的軟體工程師與 AI 助理設計。

---

## 💡 1. 應用開發者必備心智模型

1. **永遠不直接使用裸指標或 `std::shared_ptr` 管理領域物件**：
   - 領域物件的生命週期、持久化與記憶體換頁完全由 OuroKore 核心接管。
   - 所有實例化操作一律呼叫 `ork::CreateObject<T>(args...)`，它會回傳棧上安全保護的 `ork::OuroPtr<T>`。
2. **`OuroPtr<T>` 安全轉發調用機制 (Zero Raw Pointer Guarantee)**：
   - 為杜絕裸指標逃逸與懸垂指標（UAF）漏洞，`OuroPtr` **全面移除 `get()`、`operator->` 與 `operator*`**。
   - 所有物件調用一律透過安全運算子轉發：operator()(Fn&&, Args&&...) 或 Invoke(...)。**C++20 編譯期嚴格限制只能傳入成員指標（成員函式或欄位指標）**，排斥任意 Lambda / Functor，杜絕呼叫端透過閉包外洩受管物件裸指標；進階受信任閉包操作由 WithObject 提供。
   - **延遲快取極速執行 (Lazy Pointer Caching)**：`OuroPtr` 持有期間受到 Root Edge 保護，保證物件絕對不會被脫水。內部在首次調用時延遲解析並快取記憶體指標（若脫水則透明復水），後續所有調用繞過核心鎖定機制，直接以 **$O(1)$ 純原生暫存器速度**極速執行。
3. **三種 Handle 職責分工**：
   - `OwningHandle<T>`：宣告單一子物件插槽（擁有權拓撲邊緣），子物件生命週期由父物件持有。業務圖內部雙向與網狀關聯亦直接使用 `OwningHandle`，充分享受背景 `CycleCollector` 的非同步卸載。
   - `OwningContainerHandle`：宣告動態子物件容器（如背包物品清單）。
   - `UnboundHandle<T>`：宣告**非擁有型、生命週期解耦之引用**。專為**動態模組/DLL 插件熱卸載（Hot-Reload / Dynamic Unload）防釘死、可再生服務實體與旁路觀察**設計。存取時透過 `.LockAndAcquire()` 暫時換取 `OuroPtr<T>`，平時不佔用擁有權拓撲邊緣，允許目標隨時安全被卸載或重載。（⚠️ 注意：切勿將 UnboundHandle 當作 std::weak_ptr 用於打破業務圖循環參照！業務圖內部雙向互指請 100% 使用 OwningHandle，由底層 CycleCollector 自動安全回收）。
4. **執行緒安全與自動 Dirty 標記（重要！）**：
   - 物件的純資料屬性（Payload）請一律封裝在 `private` 或 `protected` 中。
   - 讀取時使用 `ork::OuroReadLock`（共用讀鎖）。
   - 修改時使用 `ork::OuroWriteLock`（獨占寫鎖）。**當 `OuroWriteLock` 離開作用域解構時，內部會自動原子化標記為 Dirty 並解鎖**，杜絕多執行緒 Data Race 與髒污狀態遺失。
5. **記憶體換頁（脫水與復水）完全透明**：
   - 長時間未被存取的物件會由脫水器自動釋放實體記憶體（保留 ControlBlock 墓碑）。
   - 當程式再次透過 `handle.Get()`、`handle.LockAndAcquire()` 或調用 `OuroPtr` 時，框架會透明地自儲存體重新還原物件，呼叫端無須撰寫額外載入邏輯。
6. **動態模組載入與自動卸載哲學 (DynamicLibrary Life-Bound Retention)**：
   - 載入器不提供手動 `unload()` 介面，以防提早手動卸載引發 vtable/代碼段失效與 Crash。
   - 應用端應將動態庫「產生的物件」與動態庫建立生命週期綁定（透過 `bind_lifecycle()` 或 Deleter 閉包捕捉 `DynamicLibrary` 實例），當產生的物件全數解構後自動在底層卸載。
   - ⚠️ **關鍵約束**：`ork::DynamicLibrary::load()` 的回傳值本身「已經將動態庫綁定（持有引用計數）」。若呼叫端不放棄此回傳值變數（如長存於成員/全域變數、或外層未離開作用域/未呼叫 `reset()`），DLL 是絕對不會被卸載的！呼叫端必須主動放棄該初始句柄，將存活權杖全權交給產生的物件。

---

## 📝 2. 自訂領域物件開發範本 (Standard Component Template)

> ⚠️ **全面嚴格強制宣告鐵律 (Strict Subclass Invariant)**：
> 凡是交由 OuroKore 託管的領域物件（透過 `ork::CreateObject<T>()` 建立者），**一律強制繼承自 `ork::Subclass<T, Base = ork::OuroObject>`**。
> **嚴格禁止直接裸繼承 `OuroObject`**（如 `class Foo : public OuroObject`）；若直接繼承，`CreateObject<Foo>()` 將於編譯期觸發 `static_assert` 攔截阻斷。
> 
> - **零巨集干擾**：類別體內部無需撰寫任何巨集，單一真實來源。
> - **編譯期型別名稱萃取**：C++20 自動從編譯器符號解析短名稱（如 `"Monster"`），永不產生 mangled 雜亂字串。
> - **支援多層繼承與建構子轉發**：子類別可直接以 `Subclass(...)` 將參數完美轉發給父類別與祖父類別。

```cpp
#include <ourokore/component/OuroCore.hpp>
#include <string>

// 1. 基底受管物件（預設 Base 為 ork::OuroObject）
class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    Monster() = default;
    explicit Monster(int32_t hp) : m_hp(hp) {}
    // 拓撲槽位：持有單一寵物子物件擁有權
    ork::OwningHandle<Monster> m_pet{"PetSlot"};

    // 弱引用解耦：指向外部動態外掛或觀察目標，防釘死模組卸載
    ork::UnboundHandle<ork::OuroObject> m_plugin_module;

    // --- 業務操作方法 (Getter / Setter) ---
    std::string GetName() const {
        ork::OuroReadLock lock(*this);
        return m_name;
    }

    void SetName(const std::string &name) {
        ork::OuroWriteLock lock(*this); // 取得寫鎖
        m_name = name;
        // 離開作用域：lock 解構時自動原子標記 Dirty！
    }

    int32_t GetHp() const {
        ork::OuroReadLock lock(*this);
        return m_hp;
    }

    void SetHp(int32_t hp) {
        ork::OuroWriteLock lock(*this);
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

    void Attack() {
        ork::OuroReadLock lock(*this);
        // 執行攻擊邏輯...
    }

    // --- 藍圖序列化介面 (Blueprint Serialization) ---
    void SerializePayload(ork::OuroStream &stream) const override {
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
    std::string m_name{"野怪"};
    int32_t m_hp{100};
    int32_t m_attack{10};
};
```

---

## 🚀 3. 常見實戰模式 (Cookbook)

### 模式 A：物件建立、雙向關聯與弱引用外掛存取
```cpp
// 1. 建立根物件（持有 Root 邊緣）
ork::OuroPtr<Monster> boss = ork::CreateObject<Monster>();
boss(&Monster::SetName, "黑龍領主");
boss(&Monster::SetHp, 5000);

// 2. 建立寵物子物件並掛載至槽位
ork::OuroPtr<Monster> drake = ork::CreateObject<Monster>();
drake(&Monster::SetName, "幼龍");
boss(&Monster::m_pet).Set(drake); // 直接透過成員指標安全持有 drake 擁有權

// 3. 關聯外部動態 DLL 模組（使用 UnboundHandle，避免釘死動態庫導致無法卸載）
ork::OuroPtr<ork::OuroObject> dynamic_plugin = LoadPluginFromDll("AIPlugin.dll");
boss(&Monster::m_plugin_module) = dynamic_plugin; // 成員指標直接賦值

// 4. 弱引用安全存取 (Anti-Dangling Guard & Decoupled Access)
ork::OuroPtr<ork::OuroObject> plugin = boss(&Monster::m_plugin_module).LockAndAcquire();
// 亦可使用進階閉包通道：boss.WithObject([](Monster &b) { return b.m_plugin_module.LockAndAcquire(); });


if (plugin) {
    // 目標存活且已取得根鎖定，安全執行操作
    std::cout << "插件模組在線，執行功能！" << std::endl;
} else {
    // 模組已動態卸載或銷毀，內部自動完成惰性修剪 (Lazy Pruning)
    std::cout << "插件模組已熱卸載或未載入" << std::endl;
}
```

### 模式 B：物件修改與執行緒安全同步 (Thread-Safe Mutation)
```cpp
// 方式一（強烈推薦）：透過安全運算子轉發調用 Setter（內部自動使用 OuroWriteLock）
// Setter 作用域結束時，OuroWriteLock 解構會原子性標記 Dirty 並釋放獨占鎖
boss(&Monster::SetHp, boss(&Monster::GetHp) - 500);

// 方式二：跨多個欄位批次修改時，透過 Lambda 閉包在安全生命週期內執行
boss([](Monster &b) {
    ork::OuroWriteLock lock(b); // 取得 ControlBlock 獨占寫入鎖
    b.SetName("狂暴的黑龍領主");
    b.SetAttack(b.GetAttack() * 2);
}); // 離開作用域時自動 atomic mark dirty 並解鎖
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

### 模式 D：型別識別與安全多型轉型 (Type Casting & Inspection)
```cpp
// 假設繼承階層：OuroObject -> Creature -> Monster -> BossMonster
ork::OuroPtr<Creature> creature = ork::CreateObject<BossMonster>();

// 1. 多型型別檢查（純 ControlBlock 墓碑長存查詢，零 I/O 脫水安全）
// 支援整條繼承鏈向上/向下安全比對，即使物件已脫水落盤亦絕不誘發穿透復水
if (creature.Is<BossMonster>()) {
    std::cout << "確認為 BossMonster 實例" << std::endl;
}

// 2. 向下安全轉型（左值拷貝：型別相符時安全增加根引用；不符時安全回傳空 OuroPtr）
ork::OuroPtr<BossMonster> boss = creature.As<BossMonster>();
if (boss) {
    boss(&BossMonster::CastUltimateSkill);
}

// 3. 右值所有權移動轉型（極度推薦：零引用計數變更開銷，原子轉移所有權！）
// 若轉型成功，creature 自動被掏空歸零，boss_moved 接管根引用；若失敗則安全銷毀根引用
ork::OuroPtr<BossMonster> boss_moved = std::move(creature).As<BossMonster>();

// 4. STL 風格轉型支援（相容標準庫動態與靜態轉型習慣）
auto dyn_boss = ork::dynamic_pointer_cast<BossMonster>(boss_moved);
auto stat_boss = ork::static_pointer_cast<Creature>(boss_moved);

// 5. 弱引用晉升轉型（UnboundHandle 直接於晉升時安全向下轉型）
ork::UnboundHandle<Creature> creature_weak = boss_moved;
if (auto acquired_boss = creature_weak.LockAndAcquire<BossMonster>()) {
    acquired_boss(&BossMonster::CastUltimateSkill);
}

// 6. 插槽協變多型賦值（基底插槽直接接收衍生類別指標）
ork::OwningHandle<Creature> slot{"MinionSlot"};
slot = boss_moved; // 自動建立拓撲邊緣
```

### 模式 E：Base 現代高效能雜湊工具庫實戰 (Hash Utilities)
```cpp
#include <ourokore/base/Hash.hpp>
using namespace ork::base::literals;

// 1. FNV-1a 64-bit（全域 TypeID 與字串 ID 唯一標準）
constexpr uint64_t type_id = "Monster"_fnv64;
uint64_t hash_val = ork::base::Fnv1a64(str_view);

// 2. CRC32 (IEEE 802.3 防竄改與資料校驗)
constexpr uint32_t magic = "OURO_BLUEPRINT"_crc32;
uint32_t checksum = ork::base::Crc32(data_span);

// 3. MurmurHash3 32-bit & HashCombine 變參組合
uint32_t hash32 = ork::base::MurmurHash3(data_span, 0x9747b28c);
size_t combined = 0;
ork::base::HashCombine(combined, id, tag, timestamp);
```

### 模式 F：脫水安全狀態判定（零 I/O 判空防線）
```cpp
// 正確：純 ControlBlock 判定，即使物件脫水亦絕不引發磁碟 I/O 復水
if (monster) { /* 存活 */ }
if (monster.IsAlive()) { /* 存活 */ }
if (monster.Is<BossMonster>()) { /* 型別相符 */ }

// 安全調用：operator() 在首次調用時延遲復水並快取指標，後續以 O(1) 原生極速調用
monster(&Monster::Attack);
```

### 模式 G：動態外掛載入、物件反向錨定與自動卸載 (DynamicLibrary Lifecycle & Auto-Unload)
```cpp
#include <ourokore/base/DynamicLibrary.hpp>

// 1. 載入外掛 DLL（load 回傳值已持有引用計數 1）
auto lib = ork::DynamicLibrary::load("AIPlugin.dll");
if (!lib) {
    std::cerr << "外掛載入失敗: " << lib.get_last_error() << std::endl;
    return;
}

// 2. 獲取工廠函式符號
auto create_fn = lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyAIPlugin");

// 3. 建立原生實體並透過 bind_lifecycle 綁定動態庫存活權杖（此時引用計數為 2）
IAIPlugin *raw = create_fn();
std::shared_ptr<IAIPlugin> plugin = lib.bind_lifecycle(raw, destroy_fn);

// 4. ⚠️ 關鍵：呼叫端主動放棄 load() 回傳的初始句柄！
// 此時 lib.use_count() 由 2 降為 1（僅由 plugin 持有存活權杖）
lib.reset();

// 5. 業務安全使用：plugin 存活期間代碼段絕不被卸載
plugin->ExecuteAI();

// 6. 當所有持有 plugin 的變數全數銷毀歸零時，Deleter 執行且 DLL 自動在底層卸載！
plugin.reset(); // 此刻底層安全呼叫 FreeLibrary / dlclose
```

---

## ⚠️ 4. 應用開發高壓線條款 (Critical Invariants)

1. **全面杜絕裸指標解引用 (Zero Raw Pointer Guarantee)**：
   * `OuroPtr<T>` 徹底移除了 `get()`、`operator->` 與 `operator*`，嚴禁任何將裸指標逃逸至 Handle 保護之外的行為。
   * 一律透過 operator()(Fn&&, Args&&...) 或 Invoke(...) 調用成員函式或成員欄位（受 C++20 std::is_member_pointer_v 約束，編譯期阻絕 Lambda 閉包偷渡外洩裸指標）；若確有跨步驟複合閉包需求，顯式使用 WithObject，由框架保證生命週期安全並透過內部延遲快取提供原生極速。
2. **嚴禁在棧上或全域宣告 `OwningHandle<T>`**：
   * `OwningHandle` 僅能作為繼承自 `OuroObject` 的成員變數使用。
   * 棧上與臨時變數請一律使用 `OuroPtr<T>`！違者在 Debug 模式下會觸發 Fail-Fast 斷言拋出例外。
3. **嚴禁在外部直接使用 `new` 或 `delete` 操作受管物件**：
   * 物件必須透過 `ork::CreateObject<T>()` 建立。
   * 物件解構與記憶體釋放由 OuroKore 拓撲與延遲隊列自動接管，手動 `delete` 將導致全進程崩潰。
4. **避免在持鎖期間執行耗時操作**：
   * 在持有 `OuroWriteLock` 或 `OuroReadLock` 時，禁止執行磁碟 I/O、網路傳輸或沉重演算法，避免引發鎖爭用或死鎖。
5. **第三方插件物理隔離保證**：
   * 第三方插件僅需引入 `<ourokore/component/OuroCore.hpp>`。
   * 插件絕對不應嘗試呼叫宿主特權 API（如 `Shutdown`、`FlushStorage`、`CollectCycles` 等），這些特權皆受 `HostContext` 嚴格防護。
6. **外掛插件 CMake 必須宣告為 MODULE（高壓鐵律）**：
   * 所有動態插件（透過 `DynamicLibrary` 動態載入之模組）在 CMake 中**必須使用 `add_library(<name> MODULE ...)`**，嚴格禁止宣告為 `SHARED`！
   * 宣告為 `SHARED` 會生成導入庫，極易被其他模組在編譯期誤鏈結（Mislink），徹底破壞插件的熱卸載與生命週期隔離。
7. **全域 TypeID 雜湊標準統一 (Fnv1a64 Invariant)**：
   * 領域物件型別定義一律繼承自 `ork::Subclass<Derived, Base>` 樣板基底；若需自訂常數識別碼，一律統一使用 `ork::base::Fnv1a64` 或字面量 `_fnv64`，嚴禁自寫重複雜湊邏輯。
8. **動態庫載入器生命週期反向錨定與自動卸載鐵律 (DynamicLibrary Invariant)**：
   * `DynamicLibrary` 禁絕提供手動 `unload()` 方法，以防虛擬函式表與代碼段提前失效引發崩潰。
   * 正確用法是透過 `lib.bind_lifecycle(raw, deleter)` 或 Deleter 閉包將產生的物件與動態庫綁定，待物件全數銷毀後由底層自動卸載。
   * ⚠️ **高壓約束**：`ork::DynamicLibrary::load()` 的回傳值本身「已經將動態庫綁定（持有引用計數）」。若應用端一直保留該回傳值（如存為長存成員或未離開作用域/未呼叫 `reset()`），DLL 是絕對不會被卸載的！必須主動放棄該初始句柄（如 `lib.reset()`），才能實現產生物件全數銷毀後 DLL 自動卸載。

---

## 📦 5. 外掛插件 CMake 建置規範 (Plugin CMake Configuration)

當您為 OuroKore 開發動態擴充外掛（Plugin / Component，供宿主透過 `DynamicLibrary` 動態載入）時，請務必遵循以下 CMake 標準範本：

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKorePlugin LANGUAGES CXX)

# 強制 C++20 標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ⚠️ 關鍵：使用 MODULE 宣告外掛庫，嚴禁使用 SHARED
add_library(MyPlugin MODULE 
    MyPlugin.cpp
)

# 引入 OuroKore 標頭檔與核心程式庫
target_include_directories(MyPlugin PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyPlugin PRIVATE ourokore_core ourokore_base)

# 跨平台設定：移除 lib 前綴並輸出至執行檔同級目錄便於載入
set_target_properties(MyPlugin PROPERTIES 
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
```
