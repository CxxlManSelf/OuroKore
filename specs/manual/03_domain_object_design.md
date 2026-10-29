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

## 📦 2. 序列化協議實作 (Pure Payload Serialization & Automatic Chaining)

OuroKore 嚴格實施「純資料（Pure Payload）」與「關聯拓撲（Edge Roster）」分離打包原則，並透過 `ork::Subclass<Derived, Base>` 在編譯期以 NVI 模式提供**全自動繼承鏈連鎖呼叫與錯誤熔斷機制**：

* **您只需要負責自身的專屬屬性資料**：宣告 `SerializeProperties` 與 `DeserializeProperties`。
* **絕對不需手動呼叫上層類別**：`Subclass` 在呼叫子類前，會自動依序由頂至底遞迴完成父類別的序列化與還原，完全杜絕漏寫 `Base::...` 導致的屬性丟失！
* **無屬性子類零代碼**：若衍生類別純粹擴充業務邏輯方法而無新增資料欄位，這兩個函式均可完全省略不寫。
* **輕量級失敗回報**：`DeserializeProperties` 回傳 `bool`（成功返回 `true`，失敗返回 `false`）。若缺少必要屬性或資料損毀只需 `return false;`，框架將自動中止並拋出 `OuroCorruptedStreamException` 觸發安全交易回滾。
* **所有成員插槽（`OwningHandle`）均由框架自動名冊打包**：絕對不需要也不可以在屬性序列化函式裡手動處理 Handle！

```cpp
// 1. 基底類別實作專屬屬性
class Monster : public ork::Subclass<Monster, ork::OuroObject> {
public:
    void SerializeProperties(ork::OuroStream &stream) const {
        stream.WriteProperty("hp", m_hp);
    }

    bool DeserializeProperties(ork::OuroStream &stream) {
        stream.ReadProperty("hp", m_hp);
        return true;
    }
private:
    int32_t m_hp{100};
};

// 2. 衍生類別（自動享受繼承連鎖，絕不需寫 Monster::SerializeProperties！）
class Boss : public ork::Subclass<Boss, Monster> {
public:
    void SerializeProperties(ork::OuroStream &stream) const {
        stream.WriteProperty("rage", m_rage);
    }

    bool DeserializeProperties(ork::OuroStream &stream) {
        stream.ReadProperty("rage", m_rage);
        return true;
    }
private:
    int32_t m_rage{0};
};
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

## 🧩 5. 次世代可替換零件設計 (Next-Gen OuroPart & PartHandle)

在高度模組化、可熱插拔零件或動態裝備系統中，OuroKore 提供專屬之次世代零件基底 `ork::OuroPart` 與專用句柄 `ork::PartHandle<T>` / `ork::PartContainerHandle<T>`：

### 1. 雙套名冊嚴格物理隔離 (Two Distinct Rosters Invariant)
* **傳統骨幹名冊 (`m_registered_handles`)**：負責物件本體不可或缺的核心生命週期插槽。
* **次世代零件名冊 (`m_part_handles`)**：負責次世代動態組裝之獨立零件槽位，兩套名冊物理分離，互不污染，杜絕命名衝突。
* **編譯期型別約束**：`PartHandle<T>` 與 `PartContainerHandle<T>` 透過 `static_assert` 強制要求 `T` 必須繼承自 `ork::OuroPart`。

### 2. 次世代零件定義與宣告範例
```cpp
#include <ourokore/part/OuroPart.hpp>
#include <ourokore/part/PartHandle.hpp>

// 1. 定義次世代零件（繼承自 Subclass<T, ork::OuroPart>）
class EnginePart : public ork::Subclass<EnginePart, ork::OuroPart> {
public:
    void SerializeProperties(ork::OuroStream &stream) const {
        stream.WriteProperty("thrust", m_thrust);
    }
    bool DeserializeProperties(ork::OuroStream &stream) {
        stream.ReadProperty("thrust", m_thrust);
        return true;
    }
private:
    float m_thrust{1000.0f};
};

// 2. 裝配次世代零件的主體物件
class Spaceship : public ork::Subclass<Spaceship, ork::OuroPart> {
public:
    // 次世代專用零件插槽（自動登記至次世代名冊）
    ork::PartHandle<EnginePart> m_main_engine{"MainEngineSlot"};
};
```

### 3. 多型責任鏈藍圖打包 (Polymorphic Handle Chaining)
當呼叫 `PackBlueprint` / `UnpackBlueprint` 時，`OuroPart::SerializeHandles` 與 `OuroPart::DeserializeHandles` 會自動先委派基類 `OuroObject` 打包第一套核心名冊，隨後自動序列化與還原第二套零件名冊，呼叫端與主程式完全無需介入！