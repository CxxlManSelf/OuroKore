#include <cassert>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroProxy.hpp"
#include "ourokore/component/builtin/InMemoryStorage.hpp"
#include "ourokore/host/HostContext.hpp"

// -------------------------------------------------------------------
// 1. 定義測試領域物件屬性清單 (Single Source of Truth)
// -------------------------------------------------------------------

#define MONSTER_PROPERTIES(X) \
  X(std::string, Name, "未知魔物") \
  X(int32_t, Hp, 100) \
  X(int32_t, Attack, 20)

// -------------------------------------------------------------------
// 2. 領域實體類別：Monster
// -------------------------------------------------------------------
class Monster : public ork::Subclass<Monster, ork::OuroObject>
{
public:
  Monster() = default;

  // 一鍵展開私有屬性欄位與線程安全 Getter/Setter
  OURO_GEN_ENTITY_PROPERTIES(MONSTER_PROPERTIES)

  // 一鍵展開 SerializePayload 與 DeserializePayload 序列化支援
  OURO_GEN_ENTITY_SERIALIZATION(MONSTER_PROPERTIES)
};

// 一鍵全自動宣告 MonsterProxy 類別並註冊 AsProxy 轉換重載
OURO_DEFINE_PROXY(MonsterProxy, Monster, MONSTER_PROPERTIES)

// -------------------------------------------------------------------
// 3. 領域實體類別：Boss（手動擴充自訂業務方法與自訂 Proxy）
// -------------------------------------------------------------------
#define BOSS_PROPERTIES(X) \
  X(std::string, Title, "深淵領主") \
  X(int32_t, Rage, 50)

class Boss : public ork::Subclass<Boss, Monster>
{
public:
  Boss() = default;

  OURO_GEN_ENTITY_PROPERTIES(BOSS_PROPERTIES)

  void SerializePayload(ork::OuroStream &stream) const override
  {
    Monster::SerializePayload(stream);
    BOSS_PROPERTIES(OURO_GEN_ENTITY_SERIALIZE_PROPERTY)
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    Monster::DeserializePayload(stream);
    BOSS_PROPERTIES(OURO_GEN_ENTITY_DESERIALIZE_PROPERTY)
  }

  // 額外的自訂業務方法
  void Enrage()
  {
    ork::OuroWriteLock lock(*this);
    m_Rage += 50;
  }

  int32_t CalcTotalPower() const
  {
    ork::OuroReadLock lock(*this);
    return GetAttack() * 2 + m_Rage;
  }

  int32_t AttackWithMultiplier(int32_t mult) const
  {
    ork::OuroReadLock lock(*this);
    return GetAttack() * mult;
  }
};

// 手動定義 BossProxy，展示擴充業務方法轉發巨集
class BossProxy : public ork::OuroProxyBase<Boss>
{
public:
  using TargetType = Boss;
  using ork::OuroProxyBase<Boss>::OuroProxyBase;

  // 展開繼承自 Monster 與 Boss 的屬性
  MONSTER_PROPERTIES(OURO_GEN_PROXY_PROPERTY)
  BOSS_PROPERTIES(OURO_GEN_PROXY_PROPERTY)

  // 一行式自動轉發成員方法（完美轉發任意參數與回傳值）
  OURO_PROXY_METHOD(Enrage)
  OURO_PROXY_METHOD(CalcTotalPower)
  OURO_PROXY_METHOD_EX(Boss, AttackWithMultiplier)
};

OURO_REGISTER_PROXY(BossProxy, Boss)

// 測試用容器持有者（用於脫水測試）
class DungeonWorld : public ork::Subclass<DungeonWorld, ork::OuroObject>
{
public:
  DungeonWorld()
  {
    m_guardian = ork::CreateObject<Monster>();
  }

  ork::OwningHandle<Monster> m_guardian{"GuardianSlot"};
};

// ===================================================================
// 單元測試函式實作
// ===================================================================

void TestBasicProxyOperations()
{
  std::cout << "[測試 1] Proxy 點呼叫基本讀寫與預設值測試..." << std::endl;

  auto monster = ork::CreateObject<Monster>();
  assert(monster);

  // 取得安全 Proxy
  auto proxy = AsProxy(monster);

  // 1. 驗證預設值
  assert(proxy.GetName() == "未知魔物");
  assert(proxy.GetHp() == 100);
  assert(proxy.GetAttack() == 20);

  // 2. 透過原生點呼叫賦值（Setter）
  proxy.SetName("遠古紅龍");
  proxy.SetHp(3500);
  proxy.SetAttack(450);

  // 3. 透過原生點呼叫讀取（Getter）
  assert(proxy.GetName() == "遠古紅龍");
  assert(proxy.GetHp() == 3500);
  assert(proxy.GetAttack() == 450);

  // 4. 驗證底層實體物件狀態同步
  assert(monster(&Monster::GetName) == "遠古紅龍");
  assert(monster(&Monster::GetHp) == 3500);
  assert(monster(&Monster::GetAttack) == 450);

  std::cout << "  -> Proxy 基本屬性點呼叫讀寫與預設值驗證通過！" << std::endl;
}

void TestCustomBusinessMethodsAndGenericInvoke()
{
  std::cout << "[測試 2] 自訂業務方法轉發與通用 Invoke / WithObject 測試..." << std::endl;

  auto boss = ork::CreateObject<Boss>();
  assert(boss);

  auto proxy = AsProxy(boss);

  // 測試繼承的屬性與專屬屬性
  proxy.SetName("黑曜石毀滅者");
  proxy.SetAttack(300);
  proxy.SetTitle("終焉領主");
  assert(proxy.GetTitle() == "終焉領主");
  assert(proxy.GetRage() == 50);

  // 呼叫自訂業務方法
  int32_t power_before = proxy.CalcTotalPower();
  assert(power_before == (300 * 2 + 50));

  proxy.Enrage();
  assert(proxy.GetRage() == 100);
  assert(proxy.CalcTotalPower() == (300 * 2 + 100));

  // 測試帶參數成員方法巨集轉發
  int32_t attack_boosted = proxy.AttackWithMultiplier(3);
  assert(attack_boosted == 300 * 3);

  // 測試通用 Invoke 轉發成員指標
  int32_t power_via_invoke = proxy.Invoke(&Boss::CalcTotalPower);
  assert(power_via_invoke == proxy.CalcTotalPower());

  // 測試 WithObject 閉包通道
  proxy.WithObject([](Boss &b) {
    b.SetHp(8888);
  });
  assert(proxy.GetHp() == 8888);

  std::cout << "  -> 自訂業務方法轉發與通用通道驗證通過！" << std::endl;
}

void TestReadWriteLocksAndDirtyTracking(ork::HostContext &host)
{
  std::cout << "[測試 3] 讀寫鎖隔離與自動 Dirty 標記測試..." << std::endl;

  auto monster = ork::CreateObject<Monster>();
  ork::HandleID id = monster.GetTargetID();
  auto proxy = AsProxy(monster);

  // 透過 Host 特權白盒測試介面將物件狀態設為 Clean
  host.SetStorageState(id, ork::StorageState::Clean);
  assert(ork::GetStorageState(id) == ork::StorageState::Clean);

  // 1. 調用 Getter（內部使用 OuroReadLock）：驗證物件仍為 Clean，無副作用
  std::string name = proxy.GetName();
  int32_t hp = proxy.GetHp();
  (void)name;
  (void)hp;
  assert(ork::GetStorageState(id) == ork::StorageState::Clean);

  // 2. 調用 Setter（內部使用 OuroWriteLock）：解構時應自動原子標記為 Dirty
  proxy.SetHp(1500);
  assert(ork::GetStorageState(id) == ork::StorageState::Dirty);

  std::cout << "  -> 讀鎖無副作用、寫鎖自動觸發 Dirty 驗證通過！" << std::endl;
}

void TestDehydrationAndTransparentRehydration(ork::HostContext &host)
{
  std::cout << "[測試 4] 脫水與透明復水整合運作測試..." << std::endl;

  auto storage = std::dynamic_pointer_cast<ork::InMemoryStorage>(host.GetStorageDriver());
  assert(storage != nullptr);

  // 建立拓撲：DungeonWorld 持有 Monster
  auto world = ork::CreateObject<DungeonWorld>();
  auto monster = world(&DungeonWorld::m_guardian).LockAndAcquire();
  ork::HandleID monster_id = monster.GetTargetID();

  // 透過 Proxy 設定屬性
  {
    auto proxy = AsProxy(monster);
    proxy.SetName("守門地獄犬");
    proxy.SetHp(7777);
    proxy.SetAttack(666);
  }

  // 將物件脫水落盤
  ork::Dehydrate(std::move(monster));
  assert(!monster);
  assert(storage->Contains(monster_id));
  assert(ork::GetStorageState(monster_id) == ork::StorageState::Dehydrated);

  // 從世界容器重新取得句柄並轉換為 Proxy
  auto reloaded_monster = world(&DungeonWorld::m_guardian).LockAndAcquire();
  assert(reloaded_monster);
  assert(reloaded_monster.GetTargetID() == monster_id);

  auto proxy = AsProxy(reloaded_monster);

  // 呼叫 Proxy 的 Getter：此時物件應透明自 StorageDriver 復水！
  assert(proxy.GetName() == "守門地獄犬");
  assert(proxy.GetHp() == 7777);
  assert(proxy.GetAttack() == 666);
  assert(ork::GetStorageState(monster_id) == ork::StorageState::Clean);

  // 復水後再次寫入屬性
  proxy.SetHp(9999);
  assert(proxy.GetHp() == 9999);
  assert(ork::GetStorageState(monster_id) == ork::StorageState::Dirty);

  std::cout << "  -> 脫水落盤與 Proxy 透明復水讀寫 100% 成功！" << std::endl;
}

void TestTypeSystemAndLifecycleQueries()
{
  std::cout << "[測試 5] 型別與生命週期狀態查詢測試..." << std::endl;

  auto boss = ork::CreateObject<Boss>();
  auto proxy = AsProxy(boss);

  // 驗證 ID 與存活
  assert(proxy.GetTargetID() == boss.GetTargetID());
  assert(proxy.IsAlive());
  assert(static_cast<bool>(proxy));

  // 驗證型別查詢
  assert(proxy.GetTypeID() == boss.GetTypeID());
  assert(proxy.Is<Boss>());
  assert(proxy.Is<Monster>());
  assert(proxy.Is<ork::OuroObject>());
  assert(!proxy.Is<DungeonWorld>());

  std::cout << "  -> 型別系統與生命週期查詢驗證通過！" << std::endl;
}

void TestCompileTimeSafetyInvariants()
{
  std::cout << "[測試 6] 編譯期安全性防禦不變量驗證 (Zero Raw Pointer Guarantee)..." << std::endl;

  // 1. 驗證禁止從右值臨時 OuroPtr 建構 Proxy（杜絕懸垂引用）
  static_assert(!std::is_constructible_v<MonsterProxy, ork::OuroPtr<Monster> &&>,
                "OuroProxy must NOT be constructible from rvalue temporary OuroPtr!");

  static_assert(!std::is_constructible_v<ork::OuroProxyBase<Monster>, ork::OuroPtr<Monster> &&>,
                "OuroProxyBase must NOT be constructible from rvalue temporary OuroPtr!");

  // 2. 驗證禁止拷貝賦值（因為持有引用）
  static_assert(!std::is_copy_assignable_v<MonsterProxy>,
                "MonsterProxy must NOT be copy assignable!");

  std::cout << "  -> 編譯期防禦不變量驗證通過！" << std::endl;
}

int main()
{
  std::cout << "=== 開始執行 OuroKore 安全 Proxy 與巨集生成系統 (OuroProxy) 單元測試 ===" << std::endl;

  auto storage = std::make_shared<ork::InMemoryStorage>();
  ork::HostContext host = ork::Init(storage);
  assert(host.IsValid());

  TestBasicProxyOperations();
  TestCustomBusinessMethodsAndGenericInvoke();
  TestReadWriteLocksAndDirtyTracking(host);
  TestDehydrationAndTransparentRehydration(host);
  TestTypeSystemAndLifecycleQueries();
  TestCompileTimeSafetyInvariants();

  host.FlushDeferredDeletions();
  host.Shutdown();

  std::cout << "\n=== 所有 OuroProxy 單元測試 100% 通過！ ===" << std::endl;
  return 0;
}
