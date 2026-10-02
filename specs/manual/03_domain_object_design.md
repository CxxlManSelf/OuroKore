# 03. 領域物件設計規範 (Domain Object Design)

本章節介紹如何遵循 OuroKore 規範設計高效能、多執行緒安全的領域模型物件。

---

## 🔒 1. 執行緒安全與自動 Dirty 標記（重要！）

在多執行緒併發環境下，手動維護「物件是否被修改（Dirty 狀態）」非常容易遺漏或產生 Data Race。OuroKore 採用 **RAII 獨占寫鎖與原子標髒** 的一體化設計：

```cpp
class Character : public ork::Subclass<Character, ork::OuroObject> {
public:
    // 讀取：使用 OuroReadLock（多個讀取者可同時併發）
    int32_t GetHp() const {
        ork::OuroReadLock lock(*this);
        return m_hp;
    }

    // 修改：使用 OuroWriteLock（獨占鎖）
    // 當 lock 解構離開作用域時，內部自動調用 ork_mark_dirty() 原子標記 Dirty！
    void SetHp(int32_t hp) {
        ork::OuroWriteLock lock(*this);
        m_hp = hp;
    }

    // 跨多屬性複合操作
    void TakeDamage(int32_t damage) {
        ork::OuroWriteLock lock(*this);
        m_hp = std::max(0, m_hp - damage);
        if (m_hp == 0) {
            m_is_dead = true;
        }
    }

private:
    int32_t m_hp{100};
    bool    m_is_dead{false};
};
```

> [!TIP]
> 始終將純資料欄位（Payload）放在 `private` 或 `protected` 中，並透過 Getter/Setter 提供存取，保證每一次修改都受到 `OuroWriteLock` 的安全保護。

---

## 📦 2. 序列化協議實作 (Pure Payload Serialization)

OuroKore 嚴格實施「純資料（Pure Payload）」與「關聯拓撲（Edge Roster）」分離打包原則：
* **您只需要負責自身的屬性資料**（整數、浮點數、字串、POD 二進位緩衝區）。
* **所有成員插槽（`OwningHandle`）均由框架自動註冊並打包**，絕對不需要也不可以在 `SerializePayload` 裡手動序列化 Handle！

```cpp
void SerializePayload(ork::OuroStream &stream) const override {
    stream.WriteProperty("hp", m_hp);
    stream.WriteProperty("is_dead", m_is_dead);
}

void DeserializePayload(ork::OuroStream &stream) override {
    stream.ReadProperty("hp", m_hp);
    stream.ReadProperty("is_dead", m_is_dead);
}
```

---

## 🚫 3. 繼承禁令：禁止菱形多重繼承 (Diamond Inheritance Forbidden)

為確保跨模組 CRT 記憶體安全釋放（Deleter）與物件型別精確轉換：
* `T*` 必須能無二義性隱式轉換為 `OuroObject*`。
* 核心在編譯時期透過 `static_assert` 嚴格禁止菱形繼承。

---

---

## 🏷️ 4. 型別系統宣告與安全多型轉型 (CRTP Subclass Type System)

所有受管領域物件均採用 **CRTP 免巨集自動型別系統**，透過繼承 `ork::Subclass<Derived, Base>`，在編譯時期自動萃取類別名稱並向核心型別登錄系統登記繼承樹，**類別體內完全無需撰寫任何侵入性巨集**即可獲得完整的 RTTI 與多型轉型支援：

```cpp
// 1. 基底領域物件（繼承自 ork::Subclass<Creature, ork::OuroObject>，Base 預設為 OuroObject）
class Creature : public ork::Subclass<Creature, ork::OuroObject> {
public:
    Creature() = default;
    explicit Creature(int32_t hp) : m_hp(hp) {}

    int32_t GetHp() const { ork::OuroReadLock lock(*this); return m_hp; }
    void SetHp(int32_t hp) { ork::OuroWriteLock lock(*this); m_hp = hp; }
private:
    int32_t m_hp{100};
};

// 2. 子類別繼承：Base 參數指定直接父類別 Creature
// 支援透過 Subclass(...) 完美轉發參數至父類別建構子！
class Monster : public ork::Subclass<Monster, Creature> {
public:
    Monster() = default;
    Monster(int32_t hp, int32_t rage) : Subclass(hp), m_rage(rage) {}

    int32_t GetRage() const { ork::OuroReadLock lock(*this); return m_rage; }
private:
    int32_t m_rage{50};
};

// 3. 孫類別／曾孫類別多層繼承
class BossMonster : public ork::Subclass<BossMonster, Monster> {
public:
    BossMonster() = default;
    BossMonster(int32_t hp, int32_t rage, std::string skill)
        : Subclass(hp, rage), m_special_skill(std::move(skill)) {}

    void CastUltimateSkill() {
        ork::OuroWriteLock lock(*this);
        // 施放絕招...
    }
private:
    std::string m_special_skill{"Meteor"};
};
```

> [!NOTE]
> **免巨集優勢**：
> - 完全拋棄舊式 `ORK_OBJECT` 巨集，語法更貼近現代標準 C++20。
> - 支援帶參數建構子轉發（透過呼叫 `Subclass(...)`）。
> - 型別識別碼在編譯期與載入時自動計算並註冊，完全杜絕手動漏寫巨集導致的繼承樹斷層。

### 1. 成員呼叫鐵律：僅接受成員函式指標
為徹底消除裸指標逃逸與懸垂指標（UAF）漏洞，`OuroPtr<T>` 徹底拔除了 `operator->`、`operator*` 與 `get()`：
* **標準調用方式**：透過成員指標運算子轉發 `ptr(&ClassName::Method, args...)` 或 `ptr.Invoke(&ClassName::Method, args...)`。
* **嚴禁直接使用裸指標或 Lambda**：`OuroPtr` 的 `operator()` 嚴格限定僅接受成員函式指標，以防止 Lambda 閉包無意捕獲並外洩裸指標；若需在極端效能情境下執行自定義閉包操作，僅限在受控範圍內使用 `ork::WithObject(ptr, lambda)`。
* **原生極速延遲快取**：首次呼叫時透明復水並快取指標，後續呼叫直接以 $O(1)$ 純暫存器原生速度執行。

```cpp
ork::OuroPtr<BossMonster> boss = ork::CreateObject<BossMonster>();

// 正確調用方式：
boss(&BossMonster::CastUltimateSkill);
boss(&BossMonster::SetHp, 9999);

// 錯誤語法（編譯失敗）：
// boss->CastUltimateSkill(); // ❌ OuroPtr 無 operator->
```

### 2. 型別判定（純 ControlBlock 查詢，零 I/O 脫水安全）
使用 `ptr.Is<TargetT>()` 可以檢查物件是否為 `TargetT` 或其派生子類別（支援完整多型繼承樹判定）：
```cpp
ork::OuroPtr<Creature> c = ork::CreateObject<BossMonster>();

// 支援沿著繼承鏈向上判定：
assert(c.Is<BossMonster>() == true);
assert(c.Is<Monster>() == true);
assert(c.Is<Creature>() == true);
assert(c.Is<ork::OuroObject>() == true);

// 脫水保證：物件即使脫水落盤，型別資訊永存於 ControlBlock 墓碑中，
// 呼叫 Is<T>() 為純記憶體比對，絕對不會觸發磁碟 I/O 復水！
```

### 3. 安全向下/向上轉型（Downcasting & Upcasting）
* **左值轉型 (`ptr.As<TargetT>()`)**：
  若型別相符，安全增加一條根引用（Root Edge）並回傳型別為 `OuroPtr<TargetT>` 的新句柄；若型別不符則安全回傳空句柄（可直接作為 `bool` 判空），**絕不拋出未定義行為或記憶體崩潰**。
  ```cpp
  ork::OuroPtr<BossMonster> boss_ptr = c.As<BossMonster>();
  if (boss_ptr) {
      // 轉型成功，安全執行專屬方法
      boss_ptr(&BossMonster::CastUltimateSkill);
  }
  ```
* **右值移動轉型 (`std::move(ptr).As<TargetT>()`，極度推薦)**：
  **零引用計數變更開銷！** 原指標的根引用所有權會直接原子移交給新指標，原指標被安全清空；若轉型失敗，原根引用會自動釋放歸零。
  ```cpp
  // 零開銷原子轉移所有權
  ork::OuroPtr<BossMonster> moved_boss = std::move(c).As<BossMonster>();
  assert(!c); // c 已被掏空
  assert(moved_boss);
  ```

### 4. STL 風格轉型函式
框架亦提供與標準庫慣例相容的模板轉型函式（全面支援左值拷貝與右值移動）：
```cpp
// 動態安全檢查轉型（同 As<T>()）
auto boss1 = ork::dynamic_pointer_cast<BossMonster>(creature_ptr);
auto boss2 = ork::dynamic_pointer_cast<BossMonster>(std::move(creature_ptr));

// 靜態轉型（不檢查 TypeID，極致效能，需由開發者保證型別安全）
auto static_boss = ork::static_pointer_cast<BossMonster>(creature_ptr);
```

### 5. 插槽與弱引用的多型賦值與晉升轉型
* **OwningHandle 協變賦值**：基底類別插槽可直接接收衍生類別指標：
  ```cpp
  ork::OwningHandle<Creature> occupant{"OccupantSlot"};
  occupant = boss_ptr; // 自動註冊擁有權拓撲邊緣
  ```
* **UnboundHandle 晉升轉型**：弱引用在呼叫 `LockAndAcquire` 時可直接模板化指定子型別：
  ```cpp
  ork::UnboundHandle<Creature> visitor = boss_ptr;

  // 晉升時直接轉型為 BossMonster，若物件已銷毀或型別不符則回傳空 OuroPtr
  if (auto boss = visitor.LockAndAcquire<BossMonster>()) {
      boss(&BossMonster::CastUltimateSkill);
  }
  ```


---

## 6. 安全代理與 X-Macro 屬性生成 (OuroProxy & X-Macro)

為了解決物件導向開發中頻繁存取屬性需撰寫 `ptr(&Monster::GetName)` 的繁瑣語法，OuroKore 提供了官方安全代理方案 `OuroProxy`。

### 1. 單一真實來源 (Single Source of Truth)
開發者僅需定義一次屬性清單：
```cpp
#define MONSTER_PROPERTIES(X) \
    X(std::string, Name, "未知魔物") \
    X(int32_t,     Hp,   100) \
    X(int32_t,     Attack, 20)
```

### 2. 宣告領域物件與安全代理
```cpp
// 宣告實體類別
class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    Monster() = default;
    OURO_GEN_ENTITY_PROPERTIES(MONSTER_PROPERTIES)
    OURO_GEN_ENTITY_SERIALIZATION(MONSTER_PROPERTIES)
};

// 一鍵生成代理
OURO_DEFINE_PROXY(MonsterProxy, Monster, MONSTER_PROPERTIES)
```

### 3. 使用端極致體驗（原生點語法 + 100% 零裸指標安全）
```cpp
auto monster = ork::CreateObject<Monster>();
auto proxy = AsProxy(monster);

// 原生點語法賦值與讀取
proxy.SetName("深淵霸主");
proxy.SetHp(5000);
std::cout << proxy.GetName() << " 目前生命值: " << proxy.GetHp() << std::endl;
```
* **執行緒安全保證**：Getter 內建 `OuroReadLock`，Setter 內建 `OuroWriteLock` 且自動於解構時原子標記 Dirty。
* **生命週期保護**：Proxy 禁止從右值臨時物件建構；方法底層全數走 `OuroPtr::operator()`，透明復水完美支援。

### 4. 擴充自訂業務方法與一行轉發巨集 (OURO_PROXY_METHOD)
若實體物件包含自訂業務邏輯（如戰鬥、計算等），可直接在實體類別中撰寫普通 C++ 函數，並在 Proxy 類別中使用 `OURO_PROXY_METHOD` 一鍵轉發：
```cpp
class Boss : public ork::Subclass<Boss, Monster> {
public:
    void Enrage() {
        ork::OuroWriteLock lock(*this);
        m_Hp += 1000;
    }
    int32_t AttackWithMultiplier(int32_t mult) const {
        ork::OuroReadLock lock(*this);
        return GetAttack() * mult;
    }
};

class BossProxy : public ork::OuroProxyBase<Boss> {
public:
    using TargetType = Boss;
    using ork::OuroProxyBase<Boss>::OuroProxyBase;
    MONSTER_PROPERTIES(OURO_GEN_PROXY_PROPERTY)

    // 一行式自動轉發：自動支援任意參數個數、型別與回傳值！
    OURO_PROXY_METHOD(Enrage)
    OURO_PROXY_METHOD(AttackWithMultiplier)
};
OURO_REGISTER_PROXY(BossProxy, Boss)
```

