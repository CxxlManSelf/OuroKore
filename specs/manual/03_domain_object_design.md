# 03. 領域物件設計規範 (Domain Object Design)

本章節介紹如何遵循 OuroKore 規範設計高效能、多執行緒安全的領域模型物件。

---

## 🔒 1. 執行緒安全與自動 Dirty 標記（重要！）

在多執行緒併發環境下，手動維護「物件是否被修改（Dirty 狀態）」非常容易遺漏或產生 Data Race。OuroKore 採用 **RAII 獨占寫鎖與原子標髒** 的一體化設計：

```cpp
class Character : public ork::OuroObject {
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

## 🏷️ 4. 型別系統宣告與安全多型轉型 (Type System & Safe Casting)

所有領域物件強烈建議在類別定義內使用 `ORK_OBJECT(Derived, Base)` 巨集宣告靜態與動態型別資訊：

```cpp
class Creature : public ork::OuroObject {
    ORK_OBJECT(Creature, ork::OuroObject)
public:
    int32_t GetHp() const { ork::OuroReadLock lock(*this); return m_hp; }
    void SetHp(int32_t hp) { ork::OuroWriteLock lock(*this); m_hp = hp; }
private:
    int32_t m_hp{100};
};

// 繼承時，第二個參數必須準確指定「直接父類別」，核心自動構建繼承鏈
class Monster : public Creature {
    ORK_OBJECT(Monster, Creature)
public:
    int32_t GetRage() const { ork::OuroReadLock lock(*this); return m_rage; }
private:
    int32_t m_rage{50};
};

class BossMonster : public Monster {
    ORK_OBJECT(BossMonster, Monster)
public:
    void CastUltimateSkill() {
        ork::OuroWriteLock lock(*this);
        // 施放絕招...
    }
};
```

### 1. 成員呼叫鐵律：嚴禁使用 `operator->`
為徹底消除裸指標逃逸與懸垂指標（UAF）漏洞，`OuroPtr<T>` 徹底拔除了 `operator->`、`operator*` 與 `get()`：
* **標準調用方式**：透過運算子轉發 `ptr(&ClassName::Method, args...)`。
* **Lambda 批次操作**：`ptr([](ClassName &obj) { obj.DoSomething(); })`。
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
