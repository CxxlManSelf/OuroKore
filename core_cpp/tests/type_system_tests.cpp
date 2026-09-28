#include <cassert>
#include <iostream>
#include <string>

#include "ourokore/c_api/component_api.h"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/builtin/InMemoryStorage.hpp"
#include "ourokore/host/HostContext.hpp"

// 定義測試用繼承階層：
// OuroObject -> Creature -> Monster -> BossMonster
//              Creature -> Human

class Creature : public ork::OuroObject
{
  ORK_OBJECT(Creature, ork::OuroObject)
public:
  int m_hp{100};

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty("hp", m_hp);
  }
  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty("hp", m_hp);
  }
};

class Monster : public Creature
{
  ORK_OBJECT(Monster, Creature)
public:
  int m_rage{50};
};

class BossMonster : public Monster
{
  ORK_OBJECT(BossMonster, Monster)
public:
  std::string m_special_skill{"Meteor"};
};

class Human : public Creature
{
  ORK_OBJECT(Human, Creature)
public:
  std::string m_job{"Warrior"};
};

// 未使用 ORK_OBJECT 巨集的純受管類別（測試 TypeTraits Fallback 相容性）
class SimpleLegacyObject : public ork::OuroObject
{
public:
  int m_val{999};
};

void TestBasicTypeInfoAndMacro()
{
  std::cout << "[測試 1] ORK_OBJECT 靜態與動態型別資訊測試..." << std::endl;

  assert(Creature::StaticTypeName() == std::string("Creature"));
  assert(Monster::StaticTypeName() == std::string("Monster"));
  assert(BossMonster::StaticTypeName() == std::string("BossMonster"));
  assert(Human::StaticTypeName() == std::string("Human"));

  ork::TypeID creature_id = Creature::StaticTypeID();
  ork::TypeID monster_id = Monster::StaticTypeID();
  ork::TypeID boss_id = BossMonster::StaticTypeID();
  ork::TypeID human_id = Human::StaticTypeID();

  assert(creature_id != 0);
  assert(monster_id != 0);
  assert(boss_id != 0);
  assert(human_id != 0);

  // 各型別 ID 必須全域唯一
  assert(creature_id != monster_id);
  assert(monster_id != boss_id);
  assert(creature_id != human_id);

  auto boss = ork::CreateObject<BossMonster>();
  assert(boss.GetTypeID() == boss_id);

  std::cout << "  -> 靜態與動態 TypeID 一致且全域唯一驗證通過！" << std::endl;
}

void TestInheritanceAndCasting()
{
  std::cout << "[測試 2] 多型繼承判定與向下/向上轉型測試..." << std::endl;

  auto boss = ork::CreateObject<BossMonster>();
  boss(&BossMonster::m_hp) = 5000;
  boss(&BossMonster::m_rage) = 100;

  // 1. Is<T>() 判定
  assert(boss.Is<BossMonster>());
  assert(boss.Is<Monster>());
  assert(boss.Is<Creature>());
  assert(boss.Is<ork::OuroObject>());
  assert(!boss.Is<Human>());  // 旁支分支，必須為 false

  // 2. 向上轉型（Upcasting 到 Base OuroPtr）
  ork::OuroPtr<Creature> creature_ptr = boss.As<Creature>();
  assert(creature_ptr);
  assert(creature_ptr.GetTargetID() == boss.GetTargetID());
  assert(creature_ptr->m_hp == 5000);

  // 3. 向下轉型（Downcasting 回 BossMonster）
  ork::OuroPtr<BossMonster> restored_boss = creature_ptr.As<BossMonster>();
  assert(restored_boss);
  assert(restored_boss.GetTargetID() == boss.GetTargetID());
  assert(restored_boss->m_special_skill == "Meteor");

  // 4. 不合法向下轉型（嘗試將 BossMonster 轉為 Human）
  ork::OuroPtr<Human> invalid_human = creature_ptr.As<Human>();
  assert(!invalid_human);
  assert(invalid_human.GetTargetID() == 0);

  // 5. dynamic_pointer_cast 與 static_pointer_cast
  auto dyn_cast_monster = ork::dynamic_pointer_cast<Monster>(creature_ptr);
  assert(dyn_cast_monster);
  assert(dyn_cast_monster->m_rage == 100);

  auto dyn_cast_human = ork::dynamic_pointer_cast<Human>(creature_ptr);
  assert(!dyn_cast_human);

  auto stat_cast_creature = ork::static_pointer_cast<Creature>(boss);
  assert(stat_cast_creature);

  std::cout << "  -> 向上轉型、向下轉型與型別防禦判定全部正確！" << std::endl;
}

void TestMoveCasting()
{
  std::cout << "[測試 3] Move 語意轉型（零計數開銷所有權轉移）測試..." << std::endl;

  auto boss = ork::CreateObject<BossMonster>();
  ork::HandleID boss_id = boss.GetTargetID();

  uint32_t root_count_before = 0;
  ork_get_root_edge_count(boss_id, &root_count_before);
  assert(root_count_before == 1);

  // 右值 As<T>() 轉移所有權
  ork::OuroPtr<Monster> monster = std::move(boss).As<Monster>();
  assert(monster);
  assert(!boss);  // boss 已被掏空
  assert(monster.GetTargetID() == boss_id);

  // 根引用計數應依然精準為 1（無多餘 register/unregister）
  uint32_t root_count_after = 0;
  ork_get_root_edge_count(boss_id, &root_count_after);
  assert(root_count_after == 1);

  // 不合法的右值轉型：轉型失敗時釋放原物件根引用
  ork::OuroPtr<Human> human = std::move(monster).As<Human>();
  assert(!human);
  assert(!monster);

  uint32_t root_count_final = 0;
  ork_get_root_edge_count(boss_id, &root_count_final);
  assert(root_count_final == 0);  // 根引用安全歸零

  std::cout << "  -> 右值轉型與根引用轉移驗證通過！" << std::endl;
}

class DungeonRoom : public ork::OuroObject
{
  ORK_OBJECT(DungeonRoom, ork::OuroObject)
public:
  ork::OwningHandle<Creature> m_occupant{"OccupantSlot"};
  ork::UnboundHandle<Creature> m_visitor;
};

void TestDehydratedTypeCheckingWithoutRehydration()
{
  std::cout << "[測試 4] 脫水狀態型別判定（零 I/O 驗證，絕不觸發復水）測試..." << std::endl;

  auto storage = std::make_shared<ork::InMemoryStorage>();
  ork::HostContext host = ork::Init(storage);
  assert(host.IsValid());

  auto room = ork::CreateObject<DungeonRoom>();
  auto boss = ork::CreateObject<BossMonster>();
  boss(&BossMonster::m_hp) = 9999;
  ork::HandleID boss_id = boss.GetTargetID();

  // 由 room 強持有，確保脱水期間 strong_count > 0（非孤島待垃圾回收）
  room(&DungeonRoom::m_occupant) = boss;

  // 儲存至 Storage
  bool saved = ork::Save(boss);
  assert(saved);

  // 透過右值移動消耗 boss 根引用發起脫水
  bool dehydrated = ork::Dehydrate(std::move(boss));
  assert(dehydrated);
  assert(!boss);

  // 確認物件處於脫水狀態（Payload 記憶體已被釋放）
  uint8_t state = 0;
  ork_get_storage_state(boss_id, &state);
  assert(state == static_cast<uint8_t>(ork::StorageState::Dehydrated));

  // 核心驗證：在不取得物件 Payload 指標的情況下，透過 HandleID 構造 OuroPtr 進行型別判斷
  ork::OuroPtr<Creature> creature(boss_id);

  // 1. 查詢 TypeID
  assert(creature.GetTypeID() == BossMonster::StaticTypeID());

  // 2. 判定 Is<T>
  assert(creature.Is<BossMonster>());
  assert(creature.Is<Monster>());
  assert(creature.Is<Creature>());
  assert(!creature.Is<Human>());

  // 3. 執行向下轉型
  ork::OuroPtr<BossMonster> boss_ptr = creature.As<BossMonster>();
  assert(boss_ptr);

  // ★ 關鍵天條檢驗：上述所有型別查詢與轉型，絕對不可觸發復水！
  ork_get_storage_state(boss_id, &state);
  assert(state == static_cast<uint8_t>(ork::StorageState::Dehydrated));

  // 直到使用者真正存取成員時，才進行透明復水
  assert(boss_ptr(&BossMonster::m_hp) == 9999);
  ork_get_storage_state(boss_id, &state);
  assert(state == static_cast<uint8_t>(ork::StorageState::Clean));

  std::cout << "  -> 脫水狀態查詢零 I/O 且完全不穿透復水驗證通過！" << std::endl;
}

void TestHandleLockAndAcquireTypeSafety()
{
  std::cout << "[測試 5] OwningHandle 與 UnboundHandle 晉升型別安全防禦測試..." << std::endl;

  auto room = ork::CreateObject<DungeonRoom>();
  auto boss = ork::CreateObject<BossMonster>();
  room(&DungeonRoom::m_occupant) = boss;
  room(&DungeonRoom::m_visitor) = boss;

  // 1. 匹配的合法型別晉升
  auto acquired_boss = room(&DungeonRoom::m_occupant).LockAndAcquire<BossMonster>();
  assert(acquired_boss);
  assert(acquired_boss.GetTargetID() == boss.GetTargetID());

  auto acquired_visitor_boss = room(&DungeonRoom::m_visitor).LockAndAcquire<BossMonster>();
  assert(acquired_visitor_boss);

  // 2. 不匹配的型別晉升（傳入 Human）必須安全回傳 null，嚴防 UB
  auto invalid_occupant = room(&DungeonRoom::m_occupant).LockAndAcquire<Human>();
  assert(!invalid_occupant);

  auto invalid_visitor = room(&DungeonRoom::m_visitor).LockAndAcquire<Human>();
  assert(!invalid_visitor);

  std::cout << "  -> Handle 跨型別晉升校驗與防禦全部生效！" << std::endl;
}

void TestPureC_API()
{
  std::cout << "[測試 6] 純 C ABI (FFI 友善介面) 型別系統測試..." << std::endl;

  ork_type_id_t custom_base = 0x1111222233334444ULL;
  ork_type_id_t custom_child = 0x5555666677778888ULL;

  // 1. 註冊型別
  assert(ork_register_type(custom_base, "CustomBase", ORK_INVALID_TYPE_ID) == ORK_STATUS_OK);
  assert(ork_register_type(custom_child, "CustomChild", custom_base) == ORK_STATUS_OK);

  // 2. 測試 ork_is_subclass_of
  int32_t is_sub = 0;
  assert(ork_is_subclass_of(custom_child, custom_base, &is_sub) == ORK_STATUS_OK && is_sub == 1);
  assert(ork_is_subclass_of(custom_base, custom_child, &is_sub) == ORK_STATUS_OK && is_sub == 0);
  assert(ork_is_subclass_of(custom_child, custom_child, &is_sub) == ORK_STATUS_OK && is_sub == 1);

  // 3. 測試 ork_get_object_type 與 ork_is_instance_of
  auto human = ork::CreateObject<Human>();
  ork::HandleID human_id = human.GetTargetID();

  ork_type_id_t obj_type = 0;
  assert(ork_get_object_type(human_id, &obj_type) == ORK_STATUS_OK);
  assert(obj_type == Human::StaticTypeID());

  int32_t is_inst = 0;
  assert(ork_is_instance_of(human_id, Human::StaticTypeID(), &is_inst) == ORK_STATUS_OK && is_inst == 1);
  assert(ork_is_instance_of(human_id, Creature::StaticTypeID(), &is_inst) == ORK_STATUS_OK && is_inst == 1);
  assert(ork_is_instance_of(human_id, Monster::StaticTypeID(), &is_inst) == ORK_STATUS_OK && is_inst == 0);

  std::cout << "  -> 純 C ABI 介面運作正常，100% 滿足多語言 FFI 規範！" << std::endl;
}

void TestFallbackTypeTraitsWithoutMacro()
{
  std::cout << "[測試 7] 未使用 ORK_OBJECT 巨集之類別相容性測試..." << std::endl;

  auto legacy = ork::CreateObject<SimpleLegacyObject>();
  assert(legacy);
  assert(legacy->m_val == 999);

  ork::TypeID tid = legacy.GetTypeID();
  assert(tid != 0);
  assert(legacy.Is<SimpleLegacyObject>());
  assert(legacy.Is<ork::OuroObject>());
  assert(!legacy.Is<Creature>());

  std::cout << "  -> Fallback 機制運作完美，未寫巨集依然安全託管！" << std::endl;
}

int main()
{
  std::cout << "=== 開始執行 OuroKore 型別系統與安全轉型 (Type System & Casting) 單元測試 ===" << std::endl;

  TestBasicTypeInfoAndMacro();
  TestInheritanceAndCasting();
  TestMoveCasting();
  TestDehydratedTypeCheckingWithoutRehydration();
  TestHandleLockAndAcquireTypeSafety();
  TestPureC_API();
  TestFallbackTypeTraitsWithoutMacro();

  std::cout << "\n=== 所有型別系統與安全轉型單元測試 100% 通過！ ===" << std::endl;
  return 0;
}
