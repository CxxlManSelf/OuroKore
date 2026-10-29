#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ourokore/component/BlueprintPackaging.hpp"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"
#include "ourokore/component/builtin/BlueprintStream.hpp"
#include "ourokore/part/OuroPart.hpp"
#include "ourokore/part/PartHandle.hpp"

// -----------------------------------------------------------------------------
// 測試用物件型別定義
// -----------------------------------------------------------------------------

// 1. 純本機私有物件（非次世代零件）
class InternalHeatSink : public ork::Subclass<InternalHeatSink, ork::OuroObject>
{
public:
  int32_t efficiency = 100;

  void SerializeProperties(ork::OuroStream &stream) const
  {
    stream.WriteProperty("efficiency", efficiency);
  }
  bool DeserializeProperties(ork::OuroStream &stream)
  {
    stream.ReadProperty("efficiency", efficiency);
    return true;
  }
};

// 2. 次世代標準配件零件（ScopePart）
class ScopePart : public ork::Subclass<ScopePart, ork::OuroPart>
{
public:
  int32_t zoom_level = 4;

  std::string_view GetModuleTag() const override
  {
    return "OpticsVendor/ScopePlugin/v1.0";
  }

  void SerializeProperties(ork::OuroStream &stream) const
  {
    stream.WriteProperty("zoom", zoom_level);
  }
  bool DeserializeProperties(ork::OuroStream &stream)
  {
    stream.ReadProperty("zoom", zoom_level);
    return true;
  }
};

// 3. 次世代主零件（LaserRiflePart），同時具備第一套私有名冊與第二套次世代名冊
class LaserRiflePart : public ork::Subclass<LaserRiflePart, ork::OuroPart>
{
public:
  int32_t damage = 150;

  // 【第一套名冊：內部私有構件】
  ork::OwningHandle<InternalHeatSink> m_private_sink{"heat_sink"};

  // 【第二套名冊：次世代標準插槽】
  ork::PartHandle<ScopePart> m_scope_slot{"scope_slot"};

  std::string_view GetModuleTag() const override
  {
    return "OuroKore/WeaponFactory/v2.0";
  }

  void SerializeProperties(ork::OuroStream &stream) const
  {
    stream.WriteProperty("damage", damage);
  }
  bool DeserializeProperties(ork::OuroStream &stream)
  {
    stream.ReadProperty("damage", damage);
    return true;
  }
};

// 4. 循環參照測試零件（持有多型 OuroPart 插槽）
class MeshNodePart : public ork::Subclass<MeshNodePart, ork::OuroPart>
{
public:
  std::string node_name;
  ork::PartHandle<ork::OuroPart> m_partner{"partner_slot"};

  MeshNodePart() = default;
  explicit MeshNodePart(std::string name) : node_name(std::move(name)) {}

  std::string_view GetModuleTag() const override
  {
    return "MeshVendor/MeshNode/v1.0";
  }

  void SerializeProperties(ork::OuroStream &stream) const
  {
    stream.WriteProperty("name", node_name);
  }
  bool DeserializeProperties(ork::OuroStream &stream)
  {
    stream.ReadProperty("name", node_name);
    return true;
  }
};

// -----------------------------------------------------------------------------
// 測試實例
// -----------------------------------------------------------------------------

void Test_OuroPart_TwoRostersSeparation()
{
  std::cout << "[Test 1] 驗證 OuroPart 兩套名冊 (m_registered_handles vs m_part_handles) 嚴格物理隔離..." << std::endl;

  auto rifle = ork::CreateObject<LaserRiflePart>();
  assert(rifle.GetTargetID() != 0);

  rifle.WithObject([](LaserRiflePart &r) {
    // 驗證模組標籤
    assert(r.GetModuleTag() == "OuroKore/WeaponFactory/v2.0");

    // 驗證第一套私有名冊：只有一個 "heat_sink"
    const auto &private_roster = r.GetRegisteredHandles();
    assert(private_roster.size() == 1);
    assert(private_roster.find("heat_sink") != private_roster.end());
    assert(private_roster.find("scope_slot") == private_roster.end());

    // 驗證第二套次世代名冊：只有一個 "scope_slot"
    const auto &part_roster = r.GetPartHandles();
    assert(part_roster.size() == 1);
    assert(part_roster.find("scope_slot") != part_roster.end());
    assert(part_roster.find("heat_sink") == part_roster.end());
  });

  std::cout << "  -> 兩套名冊物理分離驗證通過！" << std::endl;
}

void Test_OuroPart_BlueprintPackaging()
{
  std::cout << "[Test 2] 驗證 PackBlueprint / UnpackBlueprint 自動多型打包兩套名冊..." << std::endl;

  ork::BlueprintStream stream;

  // 1. 建立具有私有構件與次世代配件的槍枝
  auto rifle = ork::CreateObject<LaserRiflePart>();
  auto sink = ork::CreateObject<InternalHeatSink>();
  auto scope = ork::CreateObject<ScopePart>();

  sink.WithObject([](InternalHeatSink &s) { s.efficiency = 250; });
  scope.WithObject([](ScopePart &sc) { sc.zoom_level = 8; });

  rifle.WithObject([&](LaserRiflePart &r) {
    r.damage = 300;
    r.m_private_sink = sink;
    r.m_scope_slot = scope;
  });

  ork::HandleID orig_sink_id = sink.GetTargetID();
  ork::HandleID orig_scope_id = scope.GetTargetID();

  // 2. 打包為藍圖
  rifle.WithObject([&](LaserRiflePart &r) {
    ork::PackBlueprint(r, stream);
  });

  // 3. 還原到新實體
  auto restored_rifle = ork::CreateObject<LaserRiflePart>();
  stream.ResetCursors();

  restored_rifle.WithObject([&](LaserRiflePart &r) {
    ork::UnpackBlueprint(r, stream);

    // 驗證數值還原
    assert(r.damage == 300);

    // 驗證第一套私有槽位還原
    assert(r.m_private_sink.GetTargetID() == orig_sink_id);

    // 驗證第二套次世代槽位還原
    assert(r.m_scope_slot.GetTargetID() == orig_scope_id);
  });

  std::cout << "  -> 藍圖打包與兩套名冊原子還原驗證通過！" << std::endl;
}

void Test_OuroPart_CyclicReferences()
{
  std::cout << "[Test 3] 驗證 OuroPart 次世代零件之間自由形成循環參照..." << std::endl;

  auto nodeA = ork::CreateObject<MeshNodePart>("Node_A");
  auto nodeB = ork::CreateObject<MeshNodePart>("Node_B");

  // A 擁有 B，B 也擁有 A (雙向環狀連線)
  nodeA.WithObject([&](MeshNodePart &a) { a.m_partner = nodeB; });
  nodeB.WithObject([&](MeshNodePart &b) { b.m_partner = nodeA; });

  assert(nodeA.GetTargetID() != 0);
  assert(nodeB.GetTargetID() != 0);

  nodeA.WithObject([&](MeshNodePart &a) {
    assert(a.m_partner.GetTargetID() == nodeB.GetTargetID());
  });

  nodeB.WithObject([&](MeshNodePart &b) {
    assert(b.m_partner.GetTargetID() == nodeA.GetTargetID());
  });

  std::cout << "  -> 次世代零件循環參照拓撲運作正常！" << std::endl;
}

void Test_OuroPart_CollisionDefense()
{
  std::cout << "[Test 4] 驗證 Slot 撞名與跨名冊衝突防禦..." << std::endl;

  // 測試 PartHandle 宣告在非 OuroPart 內部的防禦
  bool caught_invalid_part_host = false;
  try
  {
    auto rifle = ork::CreateObject<LaserRiflePart>();
    rifle.WithObject([](LaserRiflePart &r) {
      // 嘗試向第二套名冊註冊一個已存在於第一套私有名冊的同名 slot "heat_sink"
      ork::OwningContainerHandle dummy_handle("heat_sink");
      r.RegisterPartHandle(&dummy_handle);
    });
  }
  catch (const std::runtime_error &e)
  {
    caught_invalid_part_host = true;
  }
  assert(caught_invalid_part_host);

  std::cout << "  -> 撞名防禦驗證通過！" << std::endl;
}

int main()
{
  std::cout << "=== 執行 OuroKore 次世代 OuroPart & PartHandle 兩套名冊單元測試 ===" << std::endl;

  Test_OuroPart_TwoRostersSeparation();
  Test_OuroPart_BlueprintPackaging();
  Test_OuroPart_CyclicReferences();
  Test_OuroPart_CollisionDefense();

  std::cout << "=== 全部 OuroPart 測試圓滿成功！ ===" << std::endl;
  return 0;
}
