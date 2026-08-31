#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/IStorageDriver.hpp"
#include "ourokore/component/InMemoryStorage.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"

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

  void ReadAndSleep(int ms) const
  {
    ork::OuroReadLock lock(*this);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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

  void ReadAndSleep(int ms) const
  {
    ork::OuroReadLock lock(*this);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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

class WorldObject : public ork::OuroObject
{
public:
  WorldObject()
  {
    m_character = ork::CreateObject<ParentCharacter>();
  }

  ork::OwningHandle<ParentCharacter> m_character{"m_character"};

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty(u8"WorldObject::dummy", int32_t(1));
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    int32_t dummy = 0;
    stream.ReadProperty(u8"WorldObject::dummy", dummy);
  }
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
  std::cout << "[Test 2] Pure Payload & Edge Roster Packaging (OuroStream Interface)..." << std::endl;

  auto player = ork::CreateObject<PlayerObject>();
  player->SetHp(150);
  player->SetName("Excalibur");

  // 1. Test direct BlueprintStream
  ork::BlueprintStream stream1;
  ork::PackBlueprint(*player, stream1);
  assert(stream1.GetSize() > 0);

  auto restored = ork::CreateObject<PlayerObject>();
  ork::UnpackBlueprint(*restored, stream1);

  assert(restored->GetHp() == 150);
  assert(restored->GetName() == "Excalibur");

  // 2. Test direct OuroStream& polymorphic interface
  ork::BlueprintStream direct_stream;
  ork::OuroStream &stream_ref = direct_stream;
  ork::PackBlueprint(*player, stream_ref);
  assert(direct_stream.GetSize() == stream1.GetSize());

  auto restored_via_stream = ork::CreateObject<PlayerObject>();
  ork::UnpackBlueprint(*restored_via_stream, stream_ref);
  assert(restored_via_stream->GetHp() == 150);
  assert(restored_via_stream->GetName() == "Excalibur");

  std::cout << "  Test 2 Passed!\n" << std::endl;
}

void Test3_SingleObject_Dehydration_Rehydration()
{
  std::cout << "[Test 3] Single Object Dehydration & Rehydration with StorageState..." << std::endl;
  std::cout.flush();

  auto storage = std::dynamic_pointer_cast<ork::InMemoryStorage>(ork::GetStorageDriver());
  assert(storage != nullptr);

  auto parent = ork::CreateObject<ParentCharacter>();
  auto weapon = parent->m_weapon.LockAndAcquire();
  ork::HandleID original_id = weapon.GetTargetID();
  assert(weapon->GetStorageState() == ork::StorageState::UnsavedNew);

  weapon->SetDamage(250);

  // Perform Dehydrate on weapon (owned by parent->m_weapon, strong_count == 1)
  ork::Dehydrate(std::move(weapon));
  assert(!weapon);  // Original object pointer is consumed and can NO longer be used!
  assert(storage->Contains(original_id));

  // Verify ControlBlock retains StorageState::Dehydrated even after payload memory is freed!
  uint8_t state_val = 0;
  ork_get_storage_state(original_id, &state_val);
  assert(static_cast<ork::StorageState>(state_val) == ork::StorageState::Dehydrated);

  // Rehydrate: reload payload and re-bind to same HandleID!
  auto rehydrated_weapon = ork::Rehydrate<WeaponObject>(original_id);
  assert(rehydrated_weapon.GetTargetID() == original_id);
  assert(rehydrated_weapon->GetDamage() == 250);
  assert(rehydrated_weapon->GetStorageState() == ork::StorageState::Clean);

  // Modify property via WriteLock -> automatically marked Dirty!
  rehydrated_weapon->SetDamage(300);
  assert(rehydrated_weapon->GetStorageState() == ork::StorageState::Dirty);

  std::cout << "  Test 3 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test4_ParentChild_Dehydration_Rehydration()
{
  std::cout << "[Test 4] Parent-Child Topology Dehydration & Rehydration..." << std::endl;
  std::cout.flush();

  auto storage = std::dynamic_pointer_cast<ork::InMemoryStorage>(ork::GetStorageDriver());
  assert(storage != nullptr);

  std::cout << "  Creating WorldObject..." << std::endl;
  std::cout.flush();

  auto world = ork::CreateObject<WorldObject>();
  auto parent = world->m_character.LockAndAcquire();
  ork::HandleID parent_id = parent.GetTargetID();
  ork::HandleID child_id = parent->m_weapon.GetTargetID();
  std::cout << "  Step 1: Created parent ID=" << parent_id << ", child ID=" << child_id << std::endl;
  std::cout.flush();

  parent->SetLevel(25);

  {
    auto weapon_ptr = parent->m_weapon.LockAndAcquire();
    std::cout << "  Step 2: Acquired weapon_ptr, valid=" << (bool)weapon_ptr << std::endl;
    std::cout.flush();

    weapon_ptr->SetDamage(120);
    std::cout << "  Step 3: Set damage to 120" << std::endl;
    std::cout.flush();

    // Dehydrate child weapon first (consumes weapon_ptr)
    ork::Dehydrate(std::move(weapon_ptr));
    assert(!weapon_ptr);
  }
  // weapon_ptr is now released (0 root edges on child). Parent holds sole edge.
  ork::Dehydrate(std::move(parent));
  assert(!parent);
  std::cout << "  Step 4: Dehydrated parent & weapon" << std::endl;
  std::cout.flush();

  assert(storage->Contains(parent_id));
  assert(storage->Contains(child_id));

  // Rehydrate parent via world handle
  auto rehydrated_parent = world->m_character.LockAndAcquire();
  std::cout << "  Step 5: Rehydrated parent, targetID=" << rehydrated_parent.GetTargetID() << std::endl;
  std::cout.flush();

  assert(rehydrated_parent.GetTargetID() == parent_id);
  assert(rehydrated_parent->GetLevel() == 25);

  // Step 6: Verify child handle inside parent has correct TargetID
  ork::HandleID rehydrated_child_id = rehydrated_parent->m_weapon.GetTargetID();
  std::cout << "  Step 7: Rehydrated parent m_weapon targetID=" << rehydrated_child_id << " (expected " << child_id << ")"
            << std::endl;
  std::cout.flush();
  assert(rehydrated_child_id == child_id);

  // Step 7: Access child via Parent's handle -> lock & verify weapon state
  auto rehydrated_weapon = rehydrated_parent->m_weapon.LockAndAcquire();
  std::cout << "  Step 8: Rehydrated weapon valid=" << (bool)rehydrated_weapon << std::endl;
  std::cout.flush();

  assert(static_cast<bool>(rehydrated_weapon));
  assert(rehydrated_weapon.GetTargetID() == child_id);
  assert(rehydrated_weapon->GetDamage() == 120);

  std::cout << "  Test 4 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test5_InMemoryStorage_Save_And_Load()
{
  std::cout << "[Test 5] Core Global ork::Save & ork::Load (Clean Skip & Sub-Object Edge Lifecycle)..." << std::endl;
  std::cout.flush();

  auto storage = std::dynamic_pointer_cast<ork::InMemoryStorage>(ork::GetStorageDriver());
  assert(storage != nullptr);

  // 1. Single Object Save & Load
  auto hero = ork::CreateObject<PlayerObject>();
  hero->SetHp(500);
  hero->SetName("Lancelot");

  // Save 1: UnsavedNew/Dirty -> saves to storage
  assert(ork::Save(hero) == true);
  assert(storage->Contains(hero.GetTargetID()));
  assert(hero->GetStorageState() == ork::StorageState::Clean);

  // Save 2: Clean -> fast skip
  assert(ork::Save(hero) == true);

  // Modify hero property via WriteLock -> automatically marked Dirty
  hero->SetHp(50);
  assert(hero->GetStorageState() == ork::StorageState::Dirty);

  // Load hero from storage -> reverts hp back to 500!
  assert(ork::Load(hero) == true);
  assert(hero->GetHp() == 500);
  assert(hero->GetName() == "Lancelot");
  assert(hero->GetStorageState() == ork::StorageState::Clean);

  // Load non-existent ID -> returns false
  auto stranger = ork::CreateObject<PlayerObject>();
  assert(ork::Load(stranger) == false);

  // 2. Sub-Object Edge Replacement & Load Rollback
  auto parent = ork::CreateObject<ParentCharacter>();
  ork::HandleID weapon1_id = parent->m_weapon.GetTargetID();
  auto weapon1 = parent->m_weapon.LockAndAcquire();
  weapon1->SetDamage(77);

  // Save parent and weapon1 independently
  assert(ork::Save(parent) == true);
  assert(ork::Save(weapon1) == true);

  // Now replace weapon1 with weapon2 (e.g. gameplay equip new weapon)
  auto weapon2 = ork::CreateObject<WeaponObject>();
  ork::HandleID weapon2_id = weapon2.GetTargetID();
  weapon2->SetDamage(999);
  parent->m_weapon = weapon2;
  assert(parent->m_weapon.GetTargetID() == weapon2_id);

  // Load parent from storage (roll back to saved snapshot)
  assert(ork::Load(parent) == true);
  assert(parent->m_weapon.GetTargetID() == weapon1_id);

  // 3. Child Destructed/Deleted Defense Verification
  auto char_a = ork::CreateObject<ParentCharacter>();
  ork::Save(char_a);

  // Explicitly release sword, causing sword strong count to drop to 0 and get deleted from Registry
  char_a->m_weapon.Release();

  // Load char_a from storage: char_a blueprint has sword_id, but sword is dead in Registry
  assert(ork::Load(char_a) == true);
  // Verified: m_weapon safely skips dead ID and remains empty (0), preventing phantom dangling references!
  assert(char_a->m_weapon.GetTargetID() == 0);
  assert((bool)char_a->m_weapon.LockAndAcquire() == false);

  std::cout << "  Test 5 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test6_Stream_Exception_Safety_And_Void_API()
{
  std::cout << "[Test 6] Stream Exception Safety & Void API Verification..." << std::endl;
  std::cout.flush();

  ork::BlueprintStream stream;
  assert(stream.HasRemainingBytes() == false);
  assert(stream.GetRemainingBytes() == 0);

  // Verify void WriteProperty and ReadProperty
  stream.WriteProperty("key::int", 12345);
  stream.WriteProperty("key::str", std::string("test_void"));

  assert(stream.HasRemainingBytes() == true);
  assert(stream.GetRemainingBytes() > 0);

  int val_int = 0;
  std::string val_str;
  stream.ReadProperty("key::int", val_int);
  stream.ReadProperty("key::str", val_str);
  assert(val_int == 12345);
  assert(val_str == "test_void");
  assert(stream.HasRemainingBytes() == false);

  // Verify OuroCorruptedStreamException on stream exhaustion
  bool out_of_range_caught = false;
  try
  {
    int extra = 0;
    stream.ReadProperty("key::extra", extra);
  }
  catch (const ork::OuroCorruptedStreamException &ex)
  {
    out_of_range_caught = true;
    std::cout << "  Captured expected OuroCorruptedStreamException on stream exhaustion: " << ex.what() << std::endl;
  }
  assert(out_of_range_caught);

  // Verify Truncated Edge Roster error reporting in UnpackBlueprint
  {
    auto parent = ork::CreateObject<ParentCharacter>();
    ork::BlueprintStream stream_valid;
    ork::PackBlueprint(*parent, stream_valid);
    const auto &valid_packed = stream_valid.GetBuffer();
    assert(valid_packed.size() > 8);

    // Truncate the buffer right in the middle of Edge Roster
    std::vector<uint8_t> truncated_packed(valid_packed.begin(), valid_packed.end() - 4);
    ork::BlueprintStream truncated_stream(truncated_packed);
    auto test_target = ork::CreateObject<ParentCharacter>();

    bool unpack_truncated_caught = false;
    try
    {
      ork::UnpackBlueprint(*test_target, truncated_stream);
    }
    catch (const ork::OuroCorruptedStreamException &ex)
    {
      unpack_truncated_caught = true;
      std::cout << "  Captured expected OuroCorruptedStreamException on truncated edge roster: " << ex.what()
                << std::endl;
    }
    assert(unpack_truncated_caught);
  }

  // Verify Rehydrate Exception Safety with Corrupted Blueprint Data
  auto storage = std::dynamic_pointer_cast<ork::InMemoryStorage>(ork::GetStorageDriver());
  assert(storage != nullptr);

  auto dummy = ork::CreateObject<PlayerObject>();
  ork::HandleID dummy_id = dummy.GetTargetID();
  dummy->SetName("CorruptedTest");

  // Save invalid/mismatched corrupted payload to storage raw buffer
  std::vector<uint8_t> corrupted_data = {0xFF, 0xFE, 0xFD, 0xFC};
  storage->SaveRawBuffer(dummy_id, corrupted_data);

  // Releasing payload from registry to simulate rehydration requirement
  ork_bind_object_payload(dummy_id, nullptr);
  ork_set_storage_state(dummy_id, static_cast<uint8_t>(ork::StorageState::Dehydrated));

  bool rehydrate_failed = false;
  try
  {
    ork::Rehydrate<PlayerObject>(dummy_id);
  }
  catch (const ork::OuroSerializationException &ex)
  {
    rehydrate_failed = true;
    std::cout << "  Captured expected OuroSerializationException during Rehydrate corrupted data: " << ex.what()
              << std::endl;
  }
  assert(rehydrate_failed);

  std::cout << "  Test 6 Passed!\n" << std::endl;
  std::cout.flush();
}

// Mock third-party custom stream that implements OuroStream directly without using BlueprintStream
class CustomThirdPartyStream : public ork::OuroStream
{
private:
  std::vector<uint8_t> m_raw_data;
  size_t m_read_pos = 0;

public:
  CustomThirdPartyStream() = default;

  void WriteBytes(const uint8_t *buffer, size_t size) override
  {
    if (buffer && size > 0)
    {
      m_raw_data.insert(m_raw_data.end(), buffer, buffer + size);
    }
  }

  void ReadBytes(uint8_t *buffer, size_t size) override
  {
    if (!buffer || size == 0) return;
    if (m_read_pos + size > m_raw_data.size())
    {
      throw ork::OuroCorruptedStreamException("CustomThirdPartyStream Read out of bounds");
    }
    std::memcpy(buffer, m_raw_data.data() + m_read_pos, size);
    m_read_pos += size;
  }

  void WriteStringRaw(const std::string &value) override
  {
    uint32_t len = static_cast<uint32_t>(value.size());
    WriteBytes(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
    if (len > 0)
    {
      WriteBytes(reinterpret_cast<const uint8_t *>(value.data()), len);
    }
  }

  std::string ReadStringRaw() override
  {
    uint32_t len = 0;
    ReadBytes(reinterpret_cast<uint8_t *>(&len), sizeof(len));
    if (len == 0) return "";
    std::string str(len, '\0');
    ReadBytes(reinterpret_cast<uint8_t *>(str.data()), len);
    return str;
  }

  bool HasRemainingBytes() const override
  {
    return m_read_pos < m_raw_data.size();
  }

  size_t GetRemainingBytes() const override
  {
    return (m_read_pos < m_raw_data.size()) ? (m_raw_data.size() - m_read_pos) : 0;
  }

  void ResetCursors() override
  {
    m_read_pos = 0;
  }

  void CheckAndRegisterKey(std::string_view /*key*/) override {}
  void VerifyKey(std::string_view expected_key) override
  {
    std::string actual_key = ReadStringRaw();
    if (actual_key != expected_key)
    {
      throw ork::OuroKeyMismatchException("CustomThirdPartyStream Key mismatch");
    }
  }
  void ClearDupGuard() override {}
};

void Test7_ThirdParty_Custom_Stream_Implementation()
{
  std::cout << "[Test 7] Third-Party Custom OuroStream Implementation Compatibility..." << std::endl;
  std::cout.flush();

  auto parent = ork::CreateObject<ParentCharacter>();
  parent->SetLevel(99);

  // Third party creates their own stream implementation
  CustomThirdPartyStream custom_stream;

  // 1. Pack object into third-party custom stream
  ork::PackBlueprint(*parent, custom_stream);
  assert(custom_stream.HasRemainingBytes() == true);

  // 2. Unpack into a new instance using third-party custom stream
  auto restored = ork::CreateObject<ParentCharacter>();
  ork::UnpackBlueprint(*restored, custom_stream);

  assert(restored->GetLevel() == 99);
  assert(restored->m_weapon.GetTargetID() == parent->m_weapon.GetTargetID());

  // 3. ResetCursors and load again into a second instance
  custom_stream.ResetCursors();
  auto restored2 = ork::CreateObject<ParentCharacter>();
  ork::UnpackBlueprint(*restored2, custom_stream);
  assert(restored2->GetLevel() == 99);

  std::cout << "  Test 7 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test8_Transparent_Auto_Rehydration()
{
  std::cout << "[Test 8] Transparent Auto-Rehydration via ControlBlock Function Hook..." << std::endl;
  std::cout.flush();

  // 1. Single Object Transparent Auto-Rehydration
  {
    auto parent = ork::CreateObject<ParentCharacter>();
    auto weapon = parent->m_weapon.LockAndAcquire();
    weapon->SetDamage(350);
    ork::HandleID weapon_id = weapon.GetTargetID();

    // Dehydrate the weapon object via move (consumes weapon pointer)
    ork::Dehydrate(std::move(weapon));
    assert(!weapon);  // Original pointer is reset and cannot be used anymore

    // Verify storage state is Dehydrated
    uint8_t state = 0;
    ork_get_storage_state(weapon_id, &state);
    assert(static_cast<ork::StorageState>(state) == ork::StorageState::Dehydrated);

    // Client accesses weapon via parent handle -> triggers transparent auto-rehydration!
    auto auto_weapon = parent->m_weapon.LockAndAcquire();
    assert(static_cast<bool>(auto_weapon));
    assert(auto_weapon->GetDamage() == 350);

    // StorageState should automatically be Clean now while alive
    ork_get_storage_state(weapon_id, &state);
    assert(static_cast<ork::StorageState>(state) == ork::StorageState::Clean);
  }

  // 2. Parent-Child Hierarchy Transparent Auto-Rehydration
  {
    auto root = ork::CreateObject<ParentCharacter>();
    root->SetLevel(77);
    ork::HandleID root_id = root.GetTargetID();

    auto weapon = root->m_weapon.LockAndAcquire();
    assert(static_cast<bool>(weapon));
    weapon->SetDamage(888);
    ork::HandleID weapon_id = weapon.GetTargetID();

    // Dehydrate child weapon first
    ork::Dehydrate(std::move(weapon));
    assert(!weapon);

    uint8_t w_state = 0;
    ork_get_storage_state(weapon_id, &w_state);
    assert(static_cast<ork::StorageState>(w_state) == ork::StorageState::Dehydrated);

    // Access weapon through parent's handle -> Auto Rehydrate Weapon!
    auto auto_weapon = root->m_weapon.LockAndAcquire();
    assert(static_cast<bool>(auto_weapon));
    assert(auto_weapon->GetDamage() == 888);

    ork_get_storage_state(weapon_id, &w_state);
    assert(static_cast<ork::StorageState>(w_state) == ork::StorageState::Clean);
  }

  // 3. Concurrent Multi-Thread Auto-Rehydration Safety Test
  {
    auto parent = ork::CreateObject<ParentCharacter>();
    auto weapon = parent->m_weapon.LockAndAcquire();
    weapon->SetDamage(999);
    ork::HandleID weapon_id = weapon.GetTargetID();

    ork::Dehydrate(std::move(weapon));
    assert(!weapon);

    constexpr int kNumThreads = 8;
    std::vector<std::thread> workers;
    std::atomic<int> success_count{0};

    for (int i = 0; i < kNumThreads; ++i)
    {
      workers.emplace_back([weapon_id, &success_count]() {
        ork::OuroPtr<WeaponObject> ptr(weapon_id);
        if (ptr && ptr->GetDamage() == 999)
        {
          success_count.fetch_add(1);
        }
      });
    }

    for (auto &t : workers)
    {
      t.join();
    }

    assert(success_count.load() == kNumThreads);
  }

  std::cout << "  Test 8 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test9_InFlight_And_Concurrent_Dehydration_Protection()
{
  std::cout << "[Test 9] In-Flight Root Edge Guard & Concurrent Dehydration Lock Safety..." << std::endl;
  std::cout.flush();

  // 1. In-Flight Root Edge Guard (Safe rejection when multiple active OuroPtr instances exist)
  {
    auto parent = ork::CreateObject<ParentCharacter>();
    auto weapon1 = parent->m_weapon.LockAndAcquire();
    weapon1->SetDamage(500);
    ork::HandleID wid = weapon1.GetTargetID();

    // Another function / stack frame creates a second active OuroPtr to the same object
    ork::OuroPtr<WeaponObject> weapon2(wid);

    // Attempting to dehydrate weapon1 while weapon2 is active safely returns false (in-flight protection)
    bool dehydrate_res1 = ork::Dehydrate(std::move(weapon1));
    assert(dehydrate_res1 == false);

    // Release the second OuroPtr
    weapon2.Release();

    // Now Dehydrate succeeds via weapon1 (since weapon2 is no longer holding it)
    bool dehydrate_res2 = ork::Dehydrate(std::move(weapon1));
    assert(dehydrate_res2 == true);
    assert(!weapon1);  // weapon1 is now consumed and cannot be used anymore

    uint8_t state = 0;
    ork_get_storage_state(wid, &state);
    assert(static_cast<ork::StorageState>(state) == ork::StorageState::Dehydrated);

    // Auto-rehydrate on access via parent handle
    auto reloaded = parent->m_weapon.LockAndAcquire();
    assert(reloaded->GetDamage() == 500);
  }

  // 2. Concurrent Reader vs Dehydrator Lock Safety
  {
    auto parent = ork::CreateObject<ParentCharacter>();
    auto weapon = parent->m_weapon.LockAndAcquire();
    weapon->SetDamage(888);
    ork::HandleID wid = weapon.GetTargetID();

    std::atomic<bool> reader_started{false};
    std::atomic<bool> reader_finished{false};

    // Thread 1 holds a read lock via member method and simulates work
    std::thread reader_thread([wid, &reader_started, &reader_finished]() {
      ork::OuroPtr<WeaponObject> ptr(wid);
      reader_started.store(true);
      ptr->ReadAndSleep(50);
      reader_finished.store(true);
    });

    while (!reader_started.load())
    {
      std::this_thread::yield();
    }

    // While reader is active in-flight, Dehydrate safely returns false without crashing
    assert(ork::Dehydrate(wid) == false);

    reader_thread.join();
    assert(reader_finished.load() == true);

    // After reader finishes, Dehydrate succeeds!
    assert(ork::Dehydrate(std::move(weapon)) == true);
    assert(!weapon);

    // Verify state is Dehydrated
    uint8_t state = 0;
    ork_get_storage_state(wid, &state);
    assert(static_cast<ork::StorageState>(state) == ork::StorageState::Dehydrated);

    // Subsequent access auto-rehydrates seamlessly
    auto reloaded = parent->m_weapon.LockAndAcquire();
    assert(reloaded->GetDamage() == 888);
  }

  std::cout << "  Test 9 Passed!\n" << std::endl;
  std::cout.flush();
}

void Test10_Concurrent_Rehydration_Thread_Safety()
{
  std::cout << "[Test 10] Concurrent Multi-Threaded Rehydrate Safety..." << std::endl;
  std::cout.flush();

  auto parent = ork::CreateObject<ParentCharacter>();
  auto weapon = parent->m_weapon.LockAndAcquire();
  weapon->SetDamage(777);
  ork::HandleID wid = weapon.GetTargetID();

  // Dehydrate the weapon
  ork::Dehydrate(std::move(weapon));
  assert(!weapon);

  uint8_t state = 0;
  ork_get_storage_state(wid, &state);
  assert(static_cast<ork::StorageState>(state) == ork::StorageState::Dehydrated);

  // Spawn 8 concurrent threads all attempting to Rehydrate the SAME HandleID simultaneously
  const int thread_count = 8;
  std::vector<std::thread> threads;
  std::atomic<bool> start_signal{false};
  std::atomic<int> success_count{0};

  for (int i = 0; i < thread_count; ++i)
  {
    threads.emplace_back([wid, &start_signal, &success_count]() {
      while (!start_signal.load())
      {
        std::this_thread::yield();
      }

      auto ptr = ork::Rehydrate<WeaponObject>(wid);
      if (ptr && ptr->GetDamage() == 777)
      {
        success_count.fetch_add(1);
      }
    });
  }

  // Release all threads at the exact same instant
  start_signal.store(true);

  for (auto &t : threads)
  {
    t.join();
  }

  assert(success_count.load() == thread_count);

  // Re-acquiring via main thread rehydrates it to Clean
  {
    auto final_ptr = ork::Rehydrate<WeaponObject>(wid);
    assert(final_ptr->GetDamage() == 777);
    ork_get_storage_state(wid, &state);
    assert(static_cast<ork::StorageState>(state) == ork::StorageState::Clean);
  }

  std::cout << "  Test 10 Passed!\n" << std::endl;
  std::cout.flush();
}

// Mock Auto-Dehydrator Plugin for testing
class MockAutoDehydrator : public ork::IAutoDehydrator
{
public:
  std::unordered_map<ork::HandleID, size_t> m_tracked;
  mutable std::mutex m_mutex;
  std::atomic<size_t> m_access_count{0};

  void Register(ork::HandleID id, size_t size_bytes) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracked[id] = size_bytes;
  }

  void Unregister(ork::HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tracked.erase(id);
  }

  bool IsTracked(ork::HandleID id) const override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tracked.find(id) != m_tracked.end();
  }

  size_t GetTrackedMemoryBytes() const override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t total = 0;
    for (const auto &[id, sz] : m_tracked)
    {
      total += sz;
    }
    return total;
  }

  void OnObjectAccess(ork::HandleID /*id*/) override
  {
    m_access_count.fetch_add(1);
  }

  size_t TriggerDehydration() override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    std::vector<ork::HandleID> to_dehydrate;
    for (const auto &[id, sz] : m_tracked)
    {
      to_dehydrate.push_back(id);
    }
    for (ork::HandleID id : to_dehydrate)
    {
      if (ork::DehydrateByID(id))
      {
        count++;
      }
    }
    return count;
  }
};

void Test11_AutoDehydrator_Plugin_And_Core_Communication()
{
  std::cout << "[Test 11] Auto-Dehydrator Plugin SPI, Size Tracking & One-Way Host Defense..." << std::endl;
  std::cout.flush();

  auto mock_dehydrator = std::dynamic_pointer_cast<MockAutoDehydrator>(ork::GetAutoDehydrator());
  assert(mock_dehydrator != nullptr);

  // 1. Verify One-Way Host Initialization Defense:
  // Any subsequent call to ork::Init (e.g. from plugin) returns false and is safely ignored
  auto fake_driver = std::make_shared<ork::InMemoryStorage>();
  auto fake_dehydrator = std::make_shared<MockAutoDehydrator>();
  assert(ork::Init(fake_driver, fake_dehydrator) == false);  // Rejection verified!
  assert(ork::GetAutoDehydrator() == mock_dehydrator);       // Existing plugin is protected!

  // 2. Named Factory 1: Create managed object in ParentCharacter (m_weapon is managed via CreateObject)
  auto parent = ork::CreateObject<ParentCharacter>();
  ork::HandleID parent_id = parent.GetTargetID();
  ork::HandleID managed_weapon_id = parent->m_weapon.GetTargetID();

  // Verify both parent and managed weapon are tracked by the dehydrator
  assert(mock_dehydrator->IsTracked(parent_id) == true);
  assert(mock_dehydrator->IsTracked(managed_weapon_id) == true);
  assert(mock_dehydrator->GetTrackedMemoryBytes() >= sizeof(WeaponObject));

  // 3. Named Factory 2: Create permanent object (CreatePermanentObject) -> NOT registered
  ork::HandleID permanent_id = 0;
  auto perm_player = ork::CreatePermanentObject<PlayerObject>();
  perm_player->SetHp(999);
  perm_player->SetName("PermanentHero");
  permanent_id = perm_player.GetTargetID();

  assert(mock_dehydrator->IsTracked(permanent_id) == false);

  // 4. In-Flight Protection:
  // `parent` is currently held by active OuroPtr (root_count == 1) -> DehydrateByID safely skips it (returns false)
  // `managed_weapon_id` has root_count == 0 (no active OuroPtr) and strong_count == 1 (held by parent) -> Dehydrated successfully!
  size_t dehydrated_count = mock_dehydrator->TriggerDehydration();
  assert(dehydrated_count == 1);  // Only managed_weapon_id was dehydrated; parent was busy in-flight!

  // Verify managed weapon is dehydrated in core
  uint8_t state_weapon = 0;
  ork_get_storage_state(managed_weapon_id, &state_weapon);
  assert(static_cast<ork::StorageState>(state_weapon) == ork::StorageState::Dehydrated);

  // Verify parent is still alive (in-flight) and permanent object is NOT in dehydrator
  uint8_t state_parent = 0;
  ork_get_storage_state(parent_id, &state_parent);
  assert(static_cast<ork::StorageState>(state_parent) != ork::StorageState::Dehydrated);

  uint8_t state_perm = 0;
  ork_get_storage_state(permanent_id, &state_perm);
  assert(static_cast<ork::StorageState>(state_perm) != ork::StorageState::Dehydrated);

  // 5. Auto-rehydration test for managed weapon when accessed via parent
  {
    auto weapon_ptr = parent->m_weapon.LockAndAcquire();
    assert((bool)weapon_ptr);
    assert(weapon_ptr->GetDamage() == 50);  // Default damage
  }

  // 6. Test Unregister (e.g. converting to permanent or on deletion)
  mock_dehydrator->Unregister(managed_weapon_id);
  assert(mock_dehydrator->IsTracked(managed_weapon_id) == false);

  std::cout << "  Test 11 Passed!\n" << std::endl;
  std::cout.flush();
}

int main()
{
  std::cout << "=== OuroKore Phase 3 Serialization & Dehydration/Rehydration Tests ===" << std::endl;
  std::cout.flush();

  try
  {
    // Host One-Way Initialization
    auto global_storage = std::make_shared<ork::InMemoryStorage>();
    auto mock_dehydrator = std::make_shared<MockAutoDehydrator>();
    bool init_ok = ork::Init(global_storage, mock_dehydrator);
    assert(init_ok == true);

    Test1_BlueprintStream_Basic_And_DupGuard();
    Test2_PurePayload_And_EdgeRoster();
    Test3_SingleObject_Dehydration_Rehydration();
    Test4_ParentChild_Dehydration_Rehydration();
    Test5_InMemoryStorage_Save_And_Load();
    Test6_Stream_Exception_Safety_And_Void_API();
    Test7_ThirdParty_Custom_Stream_Implementation();
    Test8_Transparent_Auto_Rehydration();
    Test9_InFlight_And_Concurrent_Dehydration_Protection();
    Test10_Concurrent_Rehydration_Thread_Safety();
    Test11_AutoDehydrator_Plugin_And_Core_Communication();

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

