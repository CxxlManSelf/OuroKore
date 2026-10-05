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
   - `OuroPtr<T>`：棧上 / 全域根引用守衛（Root Edge）。為杜絕指標逃逸與 UAF，徹底移除裸指標存取（無 `get()`/`operator->`），改以 `operator()(Fn&&, Args&&...)` 安全轉發執行；**C++20 編譯期嚴格限制只能傳入成員指標（成員函式或欄位），徹底杜絕在調用端以 Lambda 閉包偷渡外洩受管物件裸指標**；持有期間受 Root Edge 保護保證不脫水，內部自動延遲快取指標實現 $O(1)$ 極速原生調用。若有進階受信任需求，可顯式透過 `WithObject` 介面。
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

### 2.0.3 動態庫生命週期綁定與自動卸載規範 (DynamicLibrary Lifecycle & Auto-Unload Invariant)
> ⚠️ **DLL 卸載與生命週期反向錨定鐵律**：
> 1. **禁絕手動卸載**：`DynamicLibrary` 刻意不提供手動 `unload()` 介面，以防提前卸載導致物件虛擬函式表 (vtable) 與代碼段失效引發記憶體崩潰。
> 2. **物件生命週期反向錨定**：應用端應將產生的物件與動態庫綁定（透過 `lib.bind_lifecycle(raw_ptr, deleter_fn)` 或在自訂 Deleter 閉包中捕捉 `DynamicLibrary` 實例）。當由該 DLL 產生的所有物件全部解構後，動態庫才會在底層自動安全卸載。
> 3. **關鍵約束：`load()` 回傳值之生命週期約束**：
>    `ork::DynamicLibrary::load()` 的回傳值本身「已經將動態庫綁定（持有一份引用計數）」。**若呼叫端不放棄此回傳值變數（如長存於成員/全域變數、或外層未離開作用域/未重設），動態庫是絕對不會被卸載的！**
>    應用端必須在完成物件綁定後主動放棄該初始句柄（例如讓其隨工廠作用域結束自然解構，或主動呼叫 `lib.reset()`），將存活權杖全權移交給物件持有，才能確保「物件全數解構後 DLL 自動卸載」。
> 4. **受管物件 Payload 銷毀即刻解錨（墓碑零阻礙鐵律）**：
>    當受管物件透過 `SetObjectModuleLoader` / `IObjectModuleBinder` 綁定動態庫時，動態庫於物件脫水（Dehydrated）期間長存以備透明復水；**一旦最後一個強引用歸零並由 `DeferredDeleteQueue` 完成 Payload 物理銷毀，核心立即在核心空間主動釋放該 DynamicLibrary 引用**。即使外部仍有 `UnboundHandle` 弱引用維持 ControlBlock 墓碑，也絕不阻礙動態庫及時安全卸載。
> 5. **主程式 reset() 後之弱引用晉升重獲（WeakDynamicLibrary 鐵律）**：
>    當主程式為配合自動卸載而呼叫 `lib.reset()` 或讓強引用變數離開作用域時，若未來仍需要使用該動態庫（如再次解析符號、創建物件）或監控其存活，**應事先透過 `auto weak_lib = lib.to_weak();` 保留一份 `ork::WeakDynamicLibrary` 弱引用**。
>    日後需要使用時，透過 `if (auto locked = weak_lib.lock())` 即可零開銷晉升為有效強引用（無須重新 LoadLibrary）；若所有物件已解構且 DLL 已卸載，`weak_lib.expired()` 為 `true`，`lock()` 安全傳回無效實例。
> 6. **純生命週期存活權杖 (Pure Lifetime Token Invariant)**：
>    若外掛內部為複雜樹狀結構（如 `TreeNodeBase` 百萬節點群）、容器群或非同步任務，不便或無需綁定單一實體物件裸指標時，可透過 `auto token = lib.create_lifetime_token();` 產生型別擦除之純存活權杖（`std::shared_ptr<const void>`）。整棵樹的所有節點均可共同持有此 Token，只要全宇宙尚有任一節點存活，DLL 便絕不被物理卸載；最後一個節點解構時 Token 計數歸零觸發自動卸載。
> 7. **物理卸載完成通知回呼 (Post-Unload Hook)**：
>    宿主可透過 `lib.add_post_unload_hook(cb)` 註冊在 DLL 物理卸載（`FreeLibrary` / `dlclose`）完成後執行的通知回呼，零輪詢被動接收「外掛已完全死透、資源已全數釋放」事件。
> 8. **非同步離棧延遲卸載防護 (Deferred Stack-Decoupled Unload)**：
>    呼叫 `lib.enable_deferred_unload(true)` 可開啟離棧保護。當最後一個節點是在外掛自身的虛擬解構函式中解構時，卸載動作自動移交獨立背景執行緒執行，確保當前物件解構呼叫棧完全退出後才卸載代碼段，100% 杜絕呼叫棧自毀崩潰 (Self-Unload Stack Trap)。

### 2.1 主程式 Entry Point (HostContext)
```cpp
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>

auto storage = std::make_shared<ork::InMemoryStorage>();
ork::HostContext host = ork::Init(storage);
assert(host.IsValid());
// host 遵循 RAII 自動生命週期管理，離開作用域時解構式會自動觸發優雅關閉（Shutdown），無需且不建議手動呼叫。
```

### 2.2 定義受管物件與型別宣告 (Strict Subclass Invariant)
> ⚠️ **全面嚴格強制宣告鐵律**：
> 凡是交由 OuroKore 託管的物件（需透過 `ork::CreateObject<T>()` 建立者），**一律強制繼承自 `ork::Subclass<T, Base = ork::OuroObject>`**。
> **嚴格禁止直接裸繼承 `OuroObject`**（如 `class Foo : public OuroObject`）；若直接繼承，`CreateObject<Foo>()` 將於編譯期觸發 `static_assert` 攔截阻斷。
> 
> - **零巨集干擾**：類別體內部無需撰寫任何巨集，單一真實來源（Single Source of Truth）。
> - **編譯期型別名稱萃取**：C++20 自動從符號解析短名稱（如 `"Monster"`），永不產生 mangled 雜亂字串。
> - **支援多層繼承與建構子轉發**：子類別可直接以 `Subclass(...)` 將參數完美轉發給父類別與祖父類別。

```cpp
#include <ourokore/component/OuroCore.hpp>

// 1. 基底受管物件（預設 Base 為 ork::OuroObject）
class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    ork::OwningHandle<Monster>          m_minion{"MinionSlot"};
    ork::UnboundHandle<ork::OuroObject> m_plugin_module; // 無繫結引用，防止釘死動態 DLL 模組非同步卸載

    Monster() = default;
    explicit Monster(int32_t hp) : m_hp(hp) {}

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

// 2. 衍生孫類別（多層繼承時將 Base 指定為 Monster，支援帶參建構子轉發）
class BossMonster : public ork::Subclass<BossMonster, Monster> {
public:
    BossMonster() = default;
    BossMonster(int32_t hp, std::string skill) 
        : Subclass(hp), m_special_skill(std::move(skill)) {}

    std::string m_special_skill{"Meteor"};
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
- **安全調用成員方法與欄位（C++20 成員指標約束，零指標外洩與極速執行）**：
  ```cpp
  // 1. 調用成員函式（首選）：首次呼叫延遲快取指標（若脫水則透明復水一次），後續為 O(1) 零查表極速原生呼叫
  creature(&Creature::Attack);              // 呼叫無參成員函式
  creature(&Creature::SetHp, 250);          // 完美轉發帶參成員函式
  int hp = creature(&Creature::GetHp);      // decltype(auto) 完美保留回傳型別與值

  // 2. 存取公開成員變數（成員物件指標）：
  creature(&Creature::m_hp) = 300;          // 直接以成員指標安全讀寫公開欄位

  // 3. ⚠️ 編譯期安全防禦：operator() 與 Invoke() 嚴格禁止傳入任意 Lambda 或自由函式！
  // creature([](Creature &c) { ... });     // ❌ 編譯錯誤！Concept 判定失敗，杜絕呼叫端透過閉包外洩裸指標

  // 4. 受信任進階閉包通道（WithObject）：
  // 僅在確有跨步驟複合邏輯或藍圖串流操作需求時顯式使用，呼叫端需自行確保不得逃逸物件裸指標
  creature.WithObject([](Creature &c) {
      c.Buff();
      c.SetHp(c.GetHp() * 2);
  });
  ```

### 2.6 安全代理與 X-Macro 屬性生成系統 (OuroProxy & X-Macro System)
> 💡 **核心優勢**：在保持 **Zero Raw Pointer** 與 **防脫水 UAF** 的最高安全標準下，提供流暢的「原生點呼叫語法（`.`）」！

透過 X-Macro 單一真實來源（Single Source of Truth），同時產生**實體欄位、讀寫鎖防護、序列化與安全代理類別**：
```cpp
#include <ourokore/component/OuroCore.hpp>

// 1. 定義屬性清單 (type, name, default_val)
#define MONSTER_PROPERTIES(X) \
    X(std::string, Name, "未知魔物") \
    X(int32_t,     Hp,   100) \
    X(int32_t,     Attack, 20)

// 2. 宣告領域物件（一鍵展開屬性與序列化）
class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    Monster() = default;
    OURO_GEN_ENTITY_PROPERTIES(MONSTER_PROPERTIES)
    OURO_GEN_ENTITY_SERIALIZATION(MONSTER_PROPERTIES)
};

// 3. 一鍵生成安全代理類別與 AsProxy 轉換重載
OURO_DEFINE_PROXY(MonsterProxy, Monster, MONSTER_PROPERTIES)

// 4. 業務調用端：享受極致流暢的原生點呼叫（.）！
void BattleLoop() {
    auto boss = ork::CreateObject<Monster>();
    auto proxy = AsProxy(boss); // 🛡️ 內部僅持有 OuroPtr&，零裸指標暴露

    proxy.SetName("深淵霸主");
    proxy.SetHp(5000);
    proxy.SetAttack(350);

    std::cout << proxy.GetName() << " 參戰，目前生命值: " << proxy.GetHp() << std::endl;
}

// 5. 擴充自訂業務方法：透過 OURO_PROXY_METHOD 一行自動轉發！
class Boss : public ork::Subclass<Boss, Monster> {
public:
    void Enrage() { ork::OuroWriteLock lock(*this); m_hp += 1000; }
    int32_t MultiHit(int32_t hits) const { ork::OuroReadLock lock(*this); return GetAttack() * hits; }
};

class BossProxy : public ork::OuroProxyBase<Boss> {
public:
    using TargetType = Boss;
    using ork::OuroProxyBase<Boss>::OuroProxyBase;
    MONSTER_PROPERTIES(OURO_GEN_PROXY_PROPERTY)

    // 一行式自動轉發任意參數與回傳值：
    OURO_PROXY_METHOD(Enrage)
    OURO_PROXY_METHOD(MultiHit)
};
OURO_REGISTER_PROXY(BossProxy, Boss)
```
* **防禦不變量**：Proxy 嚴格禁止從臨時右值（Rvalue Temporary）建構，杜絕懸垂引用；底層全走 `OuroPtr::operator()`，透明復水完全無縫支援。

### 2.7 現代樹狀結構容器與文字 DSL 串流 (Tree & TreeIO Utilities)
適用於階層式遊戲資料、屬性樹、樹狀配置檔案與寬容文字 DSL 串流儲存。
```cpp
#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using ork::base::StringTreeNode;
using ork::base::TreeIO;
using ork::base::CompactMode;

// 1. 建立根節點（單一容器統合架構，全體子項目存於連續記憶體 vector）
auto player = StringTreeNode::CreateRoot(u8"Player");
player->SetData("英雄角色");

// 2. 建立具名子節點 (O(1) 雜湊尋址)
auto hp = player->AddChild(u8"HP");
hp->SetData("100");

// 3. 建立陣列型節點並享受 O(1) 隨機下標存取！
auto inventory = player->AddChild(u8"Inventory");
inventory->PushElement()->SetData("草藥");
inventory->PushElement()->SetData("黃金盔甲");

// 4. 下標與名稱存取 100% 互通自洽
assert((*inventory)[0]->GetData() == "草藥");
assert((*player)[0] == hp);              // 下標 0 與名稱 "HP" 存取為同一節點！
assert((*player)[u8"HP"] == hp);

// 5. 輸出為文字 DSL（支援 3 種緊湊模式，非遞迴顯式堆疊走訪防爆棧）
TreeIO::Serialize(std::cout, player, CompactMode::None);         // 標準美化縮排
std::string compact_eq = TreeIO::SerializeToString(player, CompactMode::WithEqual);    // 保留等號緊湊 [Player]="英雄"{...}
std::string compact_min = TreeIO::SerializeToString(player, CompactMode::WithoutEqual); // 不保留等號極致緊湊 [Player]"英雄"{...}

// 6. 寬容型狀態機反序列化（自動過濾並忽略雜訊）
auto restored = TreeIO::DeserializeFromString(dsl_text);

// 7. CRTP 衍生領域節點擴充與精準型別萃取（自動回傳 std::shared_ptr<CustomEntityNode>）
class CustomEntityNode : public ork::base::TreeNodeBase<CustomEntityNode> {
public:
    std::string tag;
    explicit CustomEntityNode(std::u8string name = u8"") : TreeNodeBase<CustomEntityNode>(std::move(name)) {}
};
auto custom_hero = TreeIO::DeserializeFromString<CustomEntityNode>(
    dsl_text,
    [](const std::shared_ptr<CustomEntityNode> &node, const std::string &raw) {
        node->tag = raw; // 支援 In-place Node Setter Handler
    }
);
static_assert(std::is_same_v<decltype(custom_hero), std::shared_ptr<CustomEntityNode>>);

// 8. 🛡️ 執行緒安全整樹走訪與死鎖防範（關鍵鐵律：走訪期間只能讀取資料，嚴禁操作節點！）
{
    // 整棵樹所有節點共享同一個讀寫鎖，外層只需持讀鎖一次
    std::shared_lock<std::shared_mutex> lock(player->GetTreeMutex());
    for (const auto &child : *player) {
        std::cout << child->GetData() << std::endl; // ✅ 純資料使用：絕對安全！
        // player->RemoveChild(child);              // ❌ 嚴格禁止！非遞迴鎖會引發重複加鎖死鎖 (Deadlock)！
    }

    // 由右向左反向走訪：直接使用 node->Reversed() 視圖糖衣（零拷貝）
    for (const auto &child : player->Reversed()) {
        std::cout << child->GetData() << std::endl;
    }
}
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
   - 核心所有型別唯一碼（`ork_type_id_t`）、編譯期 `ork::Subclass` 樣板基底、執行期字串型別註冊與查詢，**一律統一採用 `ork::base::Fnv1a64` 計算**。嚴禁在核心不同模組或外掛中各搞一套手寫雜湊邏輯，確保跨模組與脫水反序列化識別碼 100% 絕對一致。
7. **基礎工具層職責與樹狀結構容器規範 (Tree & TreeIO Invariant)**：
   - 樹狀容器（`TreeNodeBase`、`TreeNode<T>`）與串流解析器（`TreeIO`）為基礎通用設施（`ourokore_base`），零依賴核心層。
   - 採用**單一容器雙模態統合架構**：所有子項目統一存於連續記憶體 `std::vector`，具名者由 `std::unordered_map` 提供 $O(1)$ 雜湊尋址，下標與名稱存取 100% 互通。
   - 形態由長度數學關係自動推導：全具名為 Object（`{}`），混入匿名為 Array（`()`）。
   - **整樹共享讀寫鎖與走訪死鎖防禦鐵律**：整棵樹（Root 與所有子孫節點）共享同一個 `std::shared_mutex`，節點脫離時自立分配新鎖。**呼叫端在持讀鎖走訪期間「只能進行純資料使用，絕對禁止操作節點拓撲（Add/Remove/Clear/Detach）」**，否則會因非遞迴讀寫鎖引發重複加鎖死鎖（Deadlock）；動態刪除需求必須採用「先收集指針、釋放讀鎖後再批次修改」的兩階段安全範式。
   - 文字 DSL 支援 3 種緊湊模式（None、WithEqual、WithoutEqual），原生支援 `//` 單行註解、`/* ... */` 區塊註解與 `#` 腳本註解過濾，狀態機寬容過濾任意雜訊並保證 0~255 二進位位元組安全與非遞迴顯式堆疊走訪。
   - **CRTP 節點衍生與型別自適應萃取保證**：自定義節點可直接繼承 `TreeNodeBase<Derived>`，`TreeIO::Deserialize<NodeType>` 與 `DeserializeFromString<NodeType>` 會精準回傳 `std::shared_ptr<NodeType>`，子節點亦為相同衍生型別；反序列化 handler 支援 `(string) -> Data` 值轉換與 `(shared_ptr<NodeType>, string) -> void` 就地賦值兩種模式。

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
