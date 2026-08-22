#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"
#include "ourokore/component/StorageBackend.hpp"

static int g_deconstruct_count = 0;

// Simple test component
class PlayerObject : public ork::OuroObject
{
public:
  PlayerObject() = default;
  ~PlayerObject() override
  {
    g_deconstruct_count++;
  }

  int64_t GetHp() const
  {
    ork::OuroReadLock lock(*this);
    return m_hp;
  }

  void SetHp(int64_t hp)
  {
    ork::OuroWriteLock lock(*this);
    m_hp = hp;
  }

  std::string GetName() const
  {
    ork::OuroReadLock lock(*this);
    return m_name;
  }

  void SetName(const std::string &name)
  {
    ork::OuroWriteLock lock(*this);
    m_name = name;
  }

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty(u8"PlayerObject::hp", m_hp);
    stream.WriteProperty(u8"PlayerObject::name", m_name);
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty(u8"PlayerObject::hp", m_hp);
    stream.ReadProperty(u8"PlayerObject::name", m_name);
  }

private:
  int64_t m_hp = 100;
  std::string m_name = "DefaultHero";
};

class WeaponObject : public ork::OuroObject
{
public:
  WeaponObject() = default;
  ~WeaponObject() override
  {
    g_deconstruct_count++;
  }

  int32_t GetDamage() const
  {
    ork::OuroReadLock lock(*this);
    return m_damage;
  }

  void SetDamage(int32_t atk)
  {
    ork::OuroWriteLock lock(*this);
    m_damage = atk;
  }

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty(u8"WeaponObject::damage", m_damage);
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty(u8"WeaponObject::damage", m_damage);
  }

private:
  int32_t m_damage = 50;
};

class ParentCharacter : public ork::OuroObject
{
public:
  ParentCharacter()
  {
    m_weapon = ork::CreateObject<WeaponObject>();
  }

  ~ParentCharacter() override
  {
    g_deconstruct_count++;
  }

  ork::OwningHandle<WeaponObject> m_weapon{"m_weapon"};

  int32_t GetLevel() const
  {
    ork::OuroReadLock lock(*this);
    return m_level;
  }

  void SetLevel(int32_t lvl)
  {
    ork::OuroWriteLock lock(*this);
    m_level = lvl;
  }

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty(u8"ParentCharacter::level", m_level);
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty(u8"ParentCharacter::level", m_level);
  }

private:
  int32_t m_level = 10;
};

// Duplicate property key object for Fail-Fast test
class DupKeyObject : public ork::OuroObject
{
public:
  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty("same_key", 100);
    stream.WriteProperty("same_key", 200);  // Fail-Fast trigger
  }
};

void Test1_BlueprintStream_Basic_And_DupGuard()
{
  std::cout << "[Test 1] BlueprintStream Basic Read/Write & Fail-Fast Dup Guard..." << std::endl;

  ork::BlueprintStream stream;
  stream.WriteProperty(u8"test::int", 42);
  stream.WriteProperty(u8"test::float", 3.14f);
  stream.WriteProperty(u8"test::str", std::string("OuroKore"));

  int32_t i_val = 0;
  float f_val = 0.0f;
  std::string s_val;
  stream.ReadProperty(u8"test::int", i_val);
  stream.ReadProperty(u8"test::float", f_val);
  stream.ReadProperty(u8"test::str", s_val);

  assert(i_val == 42);
  assert(std::abs(f_val - 3.14f) < 0.001f);
  assert(s_val == "OuroKore");

  // Fail-Fast Dup Guard test
  bool dup_caught = false;
  try
  {
    ork::BlueprintStream dup_stream;
    DupKeyObject dup_obj;
    dup_obj.SerializePayload(dup_stream);
  }
  catch (const std::exception &ex)
  {
    dup_caught = true;
    std::cout << "  Captured expected Fail-Fast exception: " << ex.what() << std::endl;
  }
  assert(dup_caught);

  std::cout << "  Test 1 Passed!\n" << std::endl;
}

void Test2_PurePayload_And_EdgeRoster()
{
  std::cout << "[Test 2] Pure Payload & Edge Roster Packaging..." << std::endl;

  auto player = ork::CreateObject<PlayerObject>();
  player->SetHp(150);
  player->SetName("Excalibur");

  std::vector<uint8_t> buffer = ork::PackBlueprint(*player);
  assert(!buffer.empty());

  auto restored = ork::CreateObject<PlayerObject>();
  ork::UnpackBlueprint(*restored, buffer);

  assert(restored->GetHp() == 150);
  assert(restored->GetName() == "Excalibur");

  std::cout << "  Test 2 Passed!\n" << std::endl;
}

void Test3_SingleObject_Dehydration_Rehydration()
{
  std::cout << "[Test 3] Single Object Dehydration & Rehydration with StorageState..." << std::endl;
  std::cout.flush();

  auto storage = std::make_shared<ork::InMemoryStorageBackend>();
  ork::Init(storage);

  auto player = ork::CreateObject<PlayerObject>();
  ork::HandleID original_id = player.GetTargetID();
  assert(player->GetStorageState() == ork::StorageState::UnsavedNew);

  player->SetHp(250);
  player->SetName("Arthur");

  // Perform Dehydrate: save payload and free payload memory
  ork::Dehydrate(player);
  assert(storage->GetCount() == 1);
  assert(storage->Contains(original_id));

  // Verify ControlBlock retains StorageState::Dehydrated even after payload memory is freed!
  uint8_t state_val = 0;
  ork_get_storage_state(original_id, &state_val);
  assert(static_cast<ork::StorageState>(state_val) == ork::StorageState::Dehydrated);

  // Rehydrate: create empty shell via template new T(), load payload, re-bind to same HandleID!
  auto rehydrated_player = ork::Rehydrate<PlayerObject>(original_id);
  assert(rehydrated_player.GetTargetID() == original_id);
  assert(rehydrated_player->GetHp() == 250);
  assert(rehydrated_player->GetName() == "Arthur");
  assert(rehydrated_player->GetStorageState() == ork::StorageState::Clean);

  // Modify property via WriteLock -> automatically marked Dirty!
  rehydrated_player->SetHp(300);
  assert(rehydrated_player->GetStorageState() == ork::StorageState::Dirty);

  ork::Shutdown();
  std::cout << "  Test 3 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test4_ParentChild_Dehydration_Rehydration()
{
  std::cout << "[Test 4] Parent-Child Topology Dehydration & Rehydration..." << std::endl;
  std::cout.flush();

  auto storage = std::make_shared<ork::InMemoryStorageBackend>();
  ork::Init(storage);

  std::cout << "  Creating ParentCharacter..." << std::endl;
  std::cout.flush();

  auto parent = ork::CreateObject<ParentCharacter>();
  ork::HandleID parent_id = parent.GetTargetID();
  ork::HandleID child_id = parent->m_weapon.GetTargetID();
  std::cout << "  Step 1: Created parent ID=" << parent_id << ", child ID=" << child_id << std::endl;
  std::cout.flush();

  parent->SetLevel(25);
  auto weapon_ptr = parent->m_weapon.LockAndAcquire();
  std::cout << "  Step 2: Acquired weapon_ptr, valid=" << (bool)weapon_ptr << std::endl;
  std::cout.flush();

  weapon_ptr->SetDamage(120);
  std::cout << "  Step 3: Set damage to 120" << std::endl;
  std::cout.flush();

  // Dehydrate child weapon first, then dehydrate parent
  ork::Dehydrate(weapon_ptr);
  ork::Dehydrate(parent);
  std::cout << "  Step 4: Dehydrated parent & weapon" << std::endl;
  std::cout.flush();

  assert(storage->Contains(parent_id));
  assert(storage->Contains(child_id));

  // Rehydrate parent
  auto rehydrated_parent = ork::Rehydrate<ParentCharacter>(parent_id);
  std::cout << "  Step 5: Rehydrated parent, targetID=" << rehydrated_parent.GetTargetID() << std::endl;
  std::cout.flush();

  assert(rehydrated_parent.GetTargetID() == parent_id);
  assert(rehydrated_parent->GetLevel() == 25);

  // Check child handle connection
  std::cout << "  Step 7: Rehydrated parent m_weapon targetID=" << rehydrated_parent->m_weapon.GetTargetID()
            << " (expected " << child_id << ")" << std::endl;
  std::cout.flush();

  assert(rehydrated_parent->m_weapon.GetTargetID() == child_id);

  // Rehydrate child weapon
  auto rehydrated_weapon = ork::Rehydrate<WeaponObject>(child_id);
  std::cout << "  Step 8: Rehydrated weapon valid=" << (bool)rehydrated_weapon << std::endl;
  std::cout.flush();

  assert(rehydrated_weapon.GetTargetID() == child_id);
  assert(rehydrated_weapon->GetDamage() == 120);

  ork::Shutdown();
  std::cout << "  Test 4 Passed!\n" << std::endl;
  std::cout.flush();
}

int main()
{
  std::cout << "=== OuroKore Phase 3 Serialization & Dehydration/Rehydration Tests ===" << std::endl;
  std::cout.flush();

  try
  {
    Test1_BlueprintStream_Basic_And_DupGuard();
    Test2_PurePayload_And_EdgeRoster();
    Test3_SingleObject_Dehydration_Rehydration();
    Test4_ParentChild_Dehydration_Rehydration();

    std::cout << "ALL PHASE 3 TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout.flush();
  }
  catch (const std::exception &ex)
  {
    std::cout << "❌ TEST FAILED with exception: " << ex.what() << std::endl;
    std::cout.flush();
    return 1;
  }

  return 0;
}
