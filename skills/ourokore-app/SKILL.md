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
   - 🛡️ **脫水換頁安全保證與即時解錨（宿主錨定與墓碑零阻礙 Invariant）**：
     若動態外掛生成的領域物件會參與自動脫水（Dehydration），宿主主程式應使用特權方法 host.SetObjectModuleLoader(obj.GetTargetID(), plugin_dll) 或專職介面 IObjectModuleBinder，將動態庫直接錨定於受管物件中。物件脫水期間 DLL 保持長存，確保未來透明復水（RehydrateCallback）有效。
     **更關鍵的是「即時解錨」**：當該物件強引用歸零並在 DeferredDeleteQueue 完成 Payload 物理銷毀後，核心會**立即主動釋放該 DynamicLibrary 引用**！即使外部仍有 UnboundHandle 弱引用維持 ControlBlock 墓碑，動態庫也不會被鎖死，得以在所有實體銷毀後第一時間安全卸載！
   - 🛡️ **專職單元權限委派（IObjectModuleBinder 介面隔離）**：
     若動態庫載入與物件生成由專門的模組管理單元（如 `PluginManager`）負責，主程式切勿傳遞完整的 `HostContext`（避免外洩 `Shutdown`、`FlushStorage` 等全域特權）。應透過 `host.GetModuleBinder()` 取得輕量之 `std::shared_ptr<ork::IObjectModuleBinder>` 交給專職單元，貫徹最小特權原則（Least Privilege）。
   - 👁️ **弱引用觀察與 reset() 後重獲晉升 (WeakDynamicLibrary Invariant)**：
     若主程式或外掛管理器為了配合自動卸載而呼叫了 `DynamicLibrary::reset()` 放棄初始強引用，但未來仍需要使用該動態庫（如再次獲取工廠符號產生物件），**應事先在呼叫 `reset()` 前透過 `auto weak_lib = lib.to_weak();` 保留一份弱引用**。
     只要先前產生的物件仍有存活，隨時可透過 `if (auto locked = weak_lib.lock())` 零開銷重獲強引用（無須重新調用作業系統 LoadLibrary）；當所有物件解構後，DLL 自動安全卸載，弱引用安全過期（`weak_lib.expired() == true`，`lock()` 安全傳回無效實例）。
   - 🪙 **純生命週期存活權杖 (Pure Lifetime Token Invariant)**：
     若外掛內部為複雜樹狀結構（如 `TreeNodeBase` 百萬節點群）、容器群或非同步任務，不便或無需綁定單一實體物件裸指標時，可透過 `auto token = lib.create_lifetime_token();` 產生型別擦除之純存活權杖（`std::shared_ptr<const void>`）。整棵樹的所有節點均可共同持有此 Token，只要全宇宙尚有任一節點存活，DLL 便絕不被物理卸載；最後一個節點解構時 Token 計數歸零觸發自動卸載。
   - 🤝 **非同步善後握手卸載協定 (Async Shutdown Handshake)**：
     主程式發起外掛關閉（`reset()` 或釋放引用）後，主程式執行緒**0ms 立即返回繼續運作，絕不卡頓**；外掛於背景執行冗長善後（資料落盤、關閉網路、釋放大型 GPU/快取資源），完成後呼叫 `on_ready_to_unload()` 握手通知 DynamicLibrary 背景等待線程被喚醒，確認外掛徹底停工後才呼叫 `FreeLibrary` 物理卸載 DLL，兼顧主程式極致流暢與外掛安全收尾！
   - 🔔 **物理卸載完成通知回呼 (Post-Unload Hook)**：
     宿主可透過 `lib.add_post_unload_hook(cb)` 註冊在 DLL 物理卸載（`FreeLibrary` / `dlclose`）完成後執行的通知回呼，零輪詢被動接收「外掛已完全死透、資源已全數釋放」事件。
   - 🛡️ **非同步離棧延遲卸載防護 (Deferred Stack-Decoupled Unload)**：
     呼叫 `lib.enable_deferred_unload(true)` 可開啟離棧保護。當最後一個節點是在外掛自身的虛擬解構函式中解構時，卸載動作自動移交獨立背景執行緒執行，確保當前物件解構呼叫棧完全退出後才卸載代碼段，100% 杜絕呼叫棧自毀崩潰 (Self-Unload Stack Trap)。

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

### 模式 C：藍圖序列化、持久化與脫水/復水 (Dehydration & Rehydration)
```cpp
ork::HandleID boss_id = boss.GetTargetID();

// 1. 同步與非同步存檔至儲存驅動
ork::Save(boss);
std::future<ork::AsyncResult<Monster>> save_future = ork::SaveAsync(boss);
// ... 主迴圈繼續執行 ...
if (save_future.get().success) {
    std::cout << "背景存檔完成！ID: " << boss_id << std::endl;
}

// 2. 手動脫水（右值消耗語意：傳入的原 boss 指標將被立即清空重置，杜絕懸空）
bool dehydrated = ork::Dehydrate(std::move(boss));
assert(!boss); // 原 boss 指標已安全清空

// 亦支援非同步背景脫水：
// ork::DehydrateAsync(boss_id);

// 3. 儲存狀態查詢（純 ControlBlock 查詢，零 I/O 墓碑保證）
if (ork::GetStorageState(boss_id) == ork::StorageState::Dehydrated) {
    std::cout << "物件已脫水落盤，實體記憶體已釋放" << std::endl;
}

// 4. 物件復水 (Rehydration)：
// 4.1 【推薦】透明延遲按需復水：若物件被 OwningHandle 或 UnboundHandle 持有，
//      再次調用時由 ControlBlock 自動透明載入還原，業務端無需任何額外載入代碼！
// 4.2 【顯式手動復水】：若需手動主動還原並取得全新活躍 OuroPtr：
ork::OuroPtr<Monster> restored_boss = ork::Rehydrate<Monster>(boss_id);
restored_boss(&Monster::Attack);

// 4.3 【非同步背景復水】：預先在背景執行緒載入與反序列化
std::future<ork::AsyncResult<Monster>> rehydrate_future = ork::RehydrateAsync<Monster>(boss_id);
if (rehydrate_future.get().success) {
    ork::OuroPtr<Monster> async_boss = std::move(rehydrate_future.get().ptr);
}
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

### 模式 G：動態外掛載入、生命週期啟始/收尾與自動卸載 (DynamicLibrary Lifecycle & Auto-Unload)
```cpp
#include <ourokore/base/DynamicLibrary.hpp>

// 1. 載入外掛 DLL（load 回傳值已持有引用計數 1）
auto lib = ork::DynamicLibrary::load("AIPlugin.dll");
if (!lib) {
    std::cerr << "外掛載入失敗: " << lib.get_last_error() << std::endl;
    return;
}

// 2. 外掛全域啟始與收尾協定（首次載入時初始化，註冊卸載前收尾）
// 💡 若先前其他模組已載入過此 DLL，initialize_once 會自動安全略過，避免二次初始化！
using PluginInitFn = int32_t (*)(void* host_context);
lib.initialize_once<PluginInitFn>("ork_plugin_init", host_context_ptr);

// 註冊卸載前收尾回呼：保證在所有持有者與物件解構、DLL 真正被卸載前一刻調用 (LIFO)
lib.register_shutdown_symbol("ork_plugin_shutdown");

// 3. 獲取工廠函式符號
auto create_fn = lib.get_symbol<CreatePluginFn>("CreateAIPlugin");
auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyAIPlugin");

// 3. 建立原生實體並透過 bind_lifecycle 綁定動態庫存活權杖（此時引用計數為 2）
IAIPlugin *raw = create_fn();
std::shared_ptr<IAIPlugin> plugin = lib.bind_lifecycle(raw, destroy_fn);

// 4. ⚠️ 關鍵：呼叫端主動放棄 load() 回傳的初始句柄！
// 若主程式日後仍可能需要使用該動態庫，可在 reset() 前保留一份弱引用觀察者：
ork::WeakDynamicLibrary weak_lib = lib.to_weak();
// 此時 lib.use_count() 由 2 降為 1（僅由 plugin 持有存活權杖）
lib.reset();

// 5. 業務安全使用：plugin 存活期間代碼段絕不被卸載
plugin->ExecuteAI();

// 5.1 再次使用需求（弱引用晉升重獲）：
// 若日後需要再次建立新物件，透過 weak_lib.lock() 即可重獲強引用（無須重新 LoadLibrary）：
if (auto locked_lib = weak_lib.lock()) {
    auto p2 = locked_lib.bind_lifecycle(create_fn(), destroy_fn);
    // 使用完畢後 locked_lib 隨作用域解構或 reset()，不影響自動卸載
}

// 6. 當所有持有 plugin 的變數全數銷毀歸零時，Deleter 執行且 DLL 自動在底層卸載！
plugin.reset(); // 此刻底層安全呼叫 FreeLibrary / dlclose
// 此時 weak_lib.expired() == true，weak_lib.lock() 安全傳回無效實例
```

### 模式 G-1：純生命週期權杖、多節點共享（如 Tree）與後置卸載通知、離棧保護實戰
適用於外掛內部包含整棵龐大樹狀結構（百萬節點）、非同步背景工作，且宿主手上沒有單一實體指標時。

```cpp
#include <ourokore/base/DynamicLibrary.hpp>
#include <ourokore/base/Tree.hpp>

// 1. 宿主載入外掛 DLL
auto plugin = ork::DynamicLibrary::load("PluginWithTree.dll");

// 2. 啟用非同步離棧卸載保護（杜絕節點解構棧自毀崩潰）
plugin.enable_deferred_unload(true);

// 3. 註冊真結束事件通知（DLL 物理卸載完成後通知宿主）
plugin.add_post_unload_hook([]() {
    std::cout << "【宿主收到通知】外掛內部的所有樹節點已全數死透，DLL 已安全卸載！\n";
});

// 4. 插件索取純生命週期存活權杖
std::shared_ptr<const void> tree_token = plugin.create_lifetime_token();

// 5. 宿主放心地放棄初始強引用句柄
// （若日後需重獲可事先 auto weak_plugin = plugin.to_weak();）
plugin.reset(); 

// 6. 插件內部：樹狀結構共享此 Token（任意子節點被拿去外面用都安全）
// auto root = StringTreeNode::CreateRoot(u8"Root");
// ... 當外部手裡最後一個節點被釋放時，Token 計數歸零，
// DynamicLibrary 自動在背景分離執行緒安全執行 FreeLibrary，並觸發宿主通知回呼！
```

### 模式 H：現代樹狀結構容器與文字 DSL 狀態機實戰 (Tree & TreeIO Utilities)
適用於階層式遊戲資料、屬性樹、樹狀配置檔案與寬容文字 DSL 串流儲存。容器採用「單一容器雙模態統合」設計，序列化支援標準可讀與 3 種緊湊模式，並支援 CRTP 衍生領域節點與精準型別反序列化。

```cpp
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using ork::base::StringTreeNode;
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根節點
auto player = StringTreeNode::CreateRoot(u8"Player");
player->SetData("英雄角色");

// 2. 建立具名子節點（物件屬性，AddChild 即享 O(1) 雜湊尋址）
auto hp = player->AddChild(u8"HP");
hp->SetData("100");

// 3. 建立陣列節點（子節點無名即自動判定為陣列形態，DSL 輸出為 ( )）
// 💡 享受 O(1) 保序 vector 連續記憶體與 O(1) 哈希索引，下標與鍵名 100% 互通！
auto inventory = player->AddChild(u8"Inventory");
inventory->AddChild()->SetData("草藥");
inventory->AddChild()->SetData("黃金盔甲");
inventory->AddChild()->SetData("雙手大劍");

assert(inventory->ElementCount() == 3);
assert((*inventory)[0]->GetData() == "草藥");     // O(1) 極速隨機下標存取
assert((*inventory)[1]->GetData() == "黃金盔甲");
assert((*player)[0]->GetName() == u8"HP");       // 具名節點也能按下標存取！

// 4. 輸出為文字 DSL（非遞迴顯式堆疊走訪，防範爆棧；支援 3 種緊湊輸出）
// 標準格式（含縮排換行與空格）
TreeIO::Serialize(std::cout, player, CompactMode::None);

// 傳輸最佳化：保留等號之緊湊模式 [Player]="英雄角色"{[HP]="100"[Inventory]=("草藥"...)}
std::string compact_with_eq = TreeIO::SerializeToString(player, CompactMode::WithEqual);

// 極限省頻寬：無等號之極致緊湊模式 [Player]"英雄角色"{[HP]"100"[Inventory]("草藥"...)}
std::string compact_no_eq = TreeIO::SerializeToString(player, CompactMode::WithoutEqual);

// 5. 寬容型狀態機反序列化（原生支援 // 單行、/* */ 區塊與 # 腳本註解，自動過濾雜訊）
std::string config_dsl = R"(
    // 單行註解：[IgnoreMe] = "FakeData"
    /* 區塊註解：
       [Blocked] = "NotLoaded"
    */
    # 腳本風格單行註解
    這是一段任意說明文字，狀態機自動無視！
    [Player] = "英雄角色" "第二段引號視為多餘無視" // 行尾註解
    {
        [HP] = "100" # 生命值屬性
        [Inventory] = (
            "草藥"
            /* 註解排除已廢棄裝備："生鏽鐵劍" */
            這段純文字說明被無視
            "黃金盔甲"
            "雙手大劍"
        )
    }
)";
// 支援從 std::istream 串流 (如 std::istringstream) 或字串視圖直接反序列化
std::istringstream iss(config_dsl);
auto restored = TreeIO::Deserialize(iss); // 或 TreeIO::DeserializeFromString(config_dsl);
assert((*restored)[u8"HP"]->GetData() == "100");
auto restored_inv = (*restored)[u8"Inventory"];
assert((*restored_inv)[0]->GetData() == "草藥");

// 6. 自定義 CRTP 衍生領域節點（享有一體化型別自動萃取，回傳精準 std::shared_ptr<CustomNode>）
class CustomHeroNode : public ork::base::TreeNodeBase<CustomHeroNode> {
public:
    std::string title;
    int power{999};

    explicit CustomHeroNode(std::u8string name = u8"")
        : TreeNodeBase<CustomHeroNode>(std::move(name)) {}
};

// 一鍵精準反序列化為自定義節點，子節點亦自動為 CustomHeroNode！
auto custom_hero = TreeIO::DeserializeFromString<CustomHeroNode>(
    config_dsl,
    // 支援 In-place Node Setter Handler 直接解構並賦值給自定義節點欄位：
    [](const std::shared_ptr<CustomHeroNode> &node, const std::string &raw_val) {
        node->title = raw_val;
    }
);
static_assert(std::is_same_v<decltype(custom_hero), std::shared_ptr<CustomHeroNode>>);
assert(custom_hero->title == "英雄角色");

// 7. 🛡️ 執行緒安全整樹走訪與反向走訪（關鍵鐵律：走訪期間只能讀取資料，嚴禁操作節點！）
{
    // 整棵樹所有節點共享同一個讀寫鎖，外層只需持讀鎖一次即可保護整棵子樹
    std::shared_lock<std::shared_mutex> lock(player->GetTreeMutex());

    // 正向走訪（由左向右）：使用標準 STL Range-for 零拷貝遍歷
    for (const auto &child : *player) {
        if (child) {
            std::cout << "節點: " << ork::utf8::to_string(child->GetName())
                      << ", 資料: " << child->GetData() << std::endl;
        }
    }

    // 反向走訪（由右向左）：直接使用 node->Reversed() 視圖糖衣（零拷貝）
    for (const auto &child : player->Reversed()) {
        if (child) {
            std::cout << "反向節點: " << child->GetData() << std::endl;
        }
    }
    // ⚠️ 嚴禁在持讀鎖期間調用 player->RemoveChild(...) 或 AddChild(...)！
    // 若需依條件刪除節點，必須先收集指標，待讀鎖釋放後再批次呼叫 RemoveChild。
}

// 8. 🛡️ 百萬層深樹顯式堆疊防爆棧（零 Call Stack 堆疊消耗，無行程退出 UAF）
// - 析構防爆棧：TreeNodeBase 解構子內建「顯式堆疊迭代展平（Iterative Flattening）」，將級聯析構展平為堆積迴圈。
// - 反序列化防爆棧：TreeIO::Deserialize 採用純 Heap 顯式堆疊狀態機，以 O(1) 呼叫深度解析巨深巢狀 DSL。
// - 兩者均完全杜絕遞迴呼叫堆疊溢位 (Stack Overflow)，且不依賴背景分離執行緒 (t.detach())，保證行程退出零 UAF。

// 9. 📐 正交界定符與無等號哲學（連續空節點、匿名容器與物件陣列）
// - 四大界定符 []、""、{}、() 為唯一語法 Token，等號 = 純為可選裝飾符號。
// - 在極致緊湊無等號模式（CompactMode::WithoutEqual）下完全省略 =，所有結構 100% 精準對稱還原：
//   * 連續具名空節點：[Flags]{[EnableHDR][EnableVsync][EnableAA]}
//   * 連續匿名空元素：[EmptyList]("" "" "")
//   * 物件陣列：( { [item1]="A" } { [item2]="B" } )
//   * 單元素容器拓撲保全：("Single") 與 { [Key]="Val" } 完整保留容器身分，絕不發生單元素脫殼降級！
```

---

### 模式 I：Base 高效能並行排程與同步原語實戰 (ThreadPool, Queue & Synchronization)
`ourokore_base` 提供開箱即用的現代 C++20 執行緒池與同步設施：

```cpp
#include <ourokore/base/ThreadPool.hpp>
#include <ourokore/base/ThreadSafeQueue.hpp>
#include <ourokore/base/Semaphore.hpp>
#include <iostream>

void ConcurrencyCookbook() {
    // 1. 固定執行緒池 (CPU 密集型任務)
    ork::base::FixedThreadPool fixed_pool(4);
    auto fut = fixed_pool.submit([](int x) { return x * x; }, 42);
    std::cout << "平方計算: " << fut.get() << std::endl;

    // 2. 彈性動態伸縮執行緒池 (I/O 與非同步任務突增場景)
    // 核心 2 個執行緒，上限 8 個，閒置 3 秒自動縮容回收
    ork::base::DynamicThreadPool dynamic_pool(2, 8, std::chrono::milliseconds(3000));
    dynamic_pool.submit_detached([]() {
        // Fire-and-Forget 任務，零包裝器配置開銷
    });
    dynamic_pool.wait_idle();

    // 3. 多生產者-多消費者 (MPMC) 阻塞佇列
    ork::base::ThreadSafeQueue<std::string> task_queue;
    task_queue.push("Job_Alpha");
    std::string job;
    if (task_queue.pop_for(job, std::chrono::milliseconds(200))) {
        // 成功在逾時前取出
    }

    // 4. 計數信號量 (資源併發門閥)
    ork::base::Semaphore sem(0);
    // sem.acquire(); // 阻塞等待資源
    // sem.release(2); // 批次補充 2 個可用資源

    // 5. 事件通知原語 (Event)
    ork::base::Event broadcast(ork::base::EventResetMode::ManualReset, false);
    // broadcast.wait(); // 等待信號
    // broadcast.set();  // 廣播喚醒全體等待者
}
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
   * **弱引用重獲保證 (WeakDynamicLibrary)**：主程式在呼叫 `reset()` 放棄持有前，可透過 `lib.to_weak()` 保留弱引用觀察者。日後需要再次存取符號或建立物件時，呼叫 `lock()` 即可安全晉升重獲強引用；若所有物件已釋放，DLL 自動卸載，弱引用安全過期（`expired() == true`）。
   * 🛡️ **受管物件 Payload 銷毀即刻解錨（墓碑零阻礙）**：綁定至受管物件的動態庫會在物件 Payload 實體物理解構完成時立即由核心釋放引用，弱引用句柄（UnboundHandle）的長存墓碑絕不阻礙動態庫及時卸載。
   * 🪙 **純存活權杖與多節點共生 (create_lifetime_token)**：樹狀結構（百萬節點）或無單一裸指標時，透過 `create_lifetime_token()` 產生純權杖，任意節點存活皆保證 DLL 代碼段存活，全數死透自動卸載。
   * 🤝 **非同步善後握手協定 (Async Shutdown Handshake)**：外掛若有冗長善後（磁碟落盤、關閉連線、釋放大型 GPU 資源），應透過 `add_async_cleanup_hook` 或 `register_async_shutdown_symbol` 註冊。主程式呼叫 `reset()` 後**0ms 立即返回繼續運作（零卡頓）**；外掛於背景執行善後完畢後調用 `on_ready()` 握手通知 DynamicLibrary 背景等待線程被喚醒，確認外掛停工後才呼叫 `FreeLibrary` 物理卸載 DLL。
   * 🔔 **後置卸載通知與離棧保護 (add_post_unload_hook & enable_deferred_unload)**：可透過 `add_post_unload_hook` 註冊物理卸載完成通知；開啟 `enable_deferred_unload(true)` 可將卸載移交分離執行緒，徹底杜絕外掛自解構呼叫棧崩潰 (Self-Unload Stack Trap)。
   * 🔄 **多重載入快取分辨與單次啟始/收尾保證 (Single-Execution Lifecycle Invariant)**：
     - 當進程內不同子系統多次請求載入同一動態庫時，`DynamicLibrary` 內部透過規範化路徑快取共享控制區塊。
     - 僅在首次載入（0 -> 1）時 `lib.is_first_loaded()` 為 true，可透過 `initialize_once` 執行全域初始化（重複載入時自動安全略過）。
     - 透過 `register_shutdown_symbol` 或 `add_cleanup_hook` 註冊的收尾函式，嚴格保證在最後一個使用者與物件全數釋放（1 -> 0）、DLL 卸載前夕剛好觸發一次。
9. **樹狀容器單一容器雙模態統合與 CRTP 型別自適應鐵律 (Tree Dual-Mode & CRTP Invariant)**：
   * 容器內部統一採用保序 `vector` 與名稱查表 `unordered_map` 雙向索引，徹底終結 Array 與 Object 分裂。具名與無名子節點均使用 `AddChild` 或 `PushElement`。
   * 存取下標 `operator[](size_t)` 與鍵名 `operator[](u8string_view)` 100% 互通，均享有 $O(1)$ 時間複雜度。
   * 結構形態自動由資料驅動判定：只要包含無名子節點即視為陣列（輸出為 `()`），全為具名鍵值則視為物件（輸出為 `{}`）。
   * 序列化支援 3 種緊湊模式：`CompactMode::None`（預設，格式化縮排換行）、`CompactMode::WithEqual`（保留 `=` 緊湊）、`CompactMode::WithoutEqual`（無 `=` 極致緊湊）。DSL 狀態機對 3 種格式均具備 100% 雙向反序列化相容性，並原生支援 `//` 單行註解、`/* ... */` 區塊註解與 `#` 腳本註解過濾（即使註解內部包含引號或括號界定符亦可安全略過）。
   * **整樹共享讀寫鎖與走訪死鎖防禦鐵律**：整棵樹（Root 與所有子孫節點）共享同一個 `std::shared_mutex`，節點脫離時自立分配新鎖。**呼叫端在持讀鎖走訪期間「只能進行純資料使用，絕對禁止操作節點拓撲（Add/Remove/Clear/Detach）」**，否則會因非遞迴讀寫鎖引發重複加鎖死鎖（Deadlock）；動態刪除需求必須採用「先收集指針、釋放讀鎖後再批次修改」的兩階段安全範式。
   * **CRTP 節點衍生與型別自適應萃取保證**：自定義節點可直接繼承 `TreeNodeBase<Derived>`，`TreeIO::Deserialize<NodeType>` 與 `DeserializeFromString<NodeType>` 會精準回傳 `std::shared_ptr<NodeType>`，子節點亦為相同衍生型別；反序列化 handler 支援 `(string) -> Data` 值轉換與 `(shared_ptr<NodeType>, string) -> void` 就地賦值兩種模式，徹底實現零樣板、強型別安全的領域樹模型。

---

10. **執行緒池與同步原語使用鐵律 (ThreadPool & Queue Invariant)**：
    * **禁止 Worker 自我等待死鎖**：`FixedThreadPool` 與 `DynamicThreadPool` 內部已針對 `wait_idle()` 設置執行緒防護（Worker 呼叫時自動略過），但應用端切忌在池內任務中 `get()` 一個排在自己之後、且執行緒池已無額外 Worker 可調度的子任務，以防執行緒飢餓死鎖。
    * **MPMC 佇列鎖外安全析構**：`ThreadSafeQueue::clear()` 會在釋放互斥鎖後才進行元素析構，應用端自訂 Task 或析構函式中若涉及其他鎖，應注意鎖的獲取順序，杜絕逆向加鎖。

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


### 4.5 快速屬性宣告與安全代理 (OuroProxy & X-Macro)
> 🌟 **若領域物件包含大量屬性欄位，推薦使用 X-Macro 一鍵生成實體與安全 Proxy**：

```cpp
#include <ourokore/component/OuroCore.hpp>

#define PLAYER_PROPERTIES(X) \
    X(std::string, Name, "冒險者") \
    X(int32_t,     Level, 1) \
    X(int32_t,     Exp,   0)

class Player : public ork::Subclass<Player, ork::OuroObject> {
public:
    Player() = default;
    OURO_GEN_ENTITY_PROPERTIES(PLAYER_PROPERTIES)
    OURO_GEN_ENTITY_SERIALIZATION(PLAYER_PROPERTIES)
};

OURO_DEFINE_PROXY(PlayerProxy, Player, PLAYER_PROPERTIES)

// 使用端享受原生點呼叫：
void TestPlayer() {
    auto p = ork::CreateObject<Player>();
    auto proxy = AsProxy(p);
    proxy.SetName("勇者欣梅爾");
    proxy.SetLevel(99);
}
// 亦可搭配 OURO_PROXY_METHOD 一行擴充自訂業務方法轉發：
class BossProxy : public ork::OuroProxyBase<Boss> {
public:
    using TargetType = Boss;
    using ork::OuroProxyBase<Boss>::OuroProxyBase;
    PLAYER_PROPERTIES(OURO_GEN_PROXY_PROPERTY)

    OURO_PROXY_METHOD(CastSkill) // 自動完美轉發任意參數與回傳值
};
OURO_REGISTER_PROXY(BossProxy, Boss)
```
