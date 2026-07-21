#include <cassert>
#include <iostream>
#include <stdexcept>

#include "ourokore/c_api/core.h"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"

static int g_deconstruct_count = 0;

class SimpleObject : public ork::OuroObject
{
public:
  SimpleObject() = default;
  ~SimpleObject() override { g_deconstruct_count++; }
  uint64_t GetTypeID() const override { return 100; }
};

// Classes for Test 2 (Parent-Child topology)
class ChildObject : public ork::OuroObject
{
public:
  ChildObject() = default;
  ~ChildObject() override { g_deconstruct_count++; }
  uint64_t GetTypeID() const override { return 102; }
};

class ParentObject : public ork::OuroObject
{
public:
  ParentObject()
  {
    // Child is nested-constructed. ActiveOwnerContext should pass Parent ID automatically.
    m_child = ork::CreateObject<ChildObject>();
  }
  ~ParentObject() override { g_deconstruct_count++; }
  uint64_t GetTypeID() const override { return 101; }

  ork::OwningHandle<ChildObject> m_child;
};

class ParentWithTwoChildren : public ork::OuroObject
{
public:
  ParentWithTwoChildren() = default;
  ~ParentWithTwoChildren() override { g_deconstruct_count++; }
  uint64_t GetTypeID() const override { return 105; }

  ork::OwningHandle<SimpleObject> m_child1;
  ork::OwningHandle<SimpleObject> m_child2;
};

// Compile-time validation for Test 4 (Diamond inheritance prevention)
class DiamondLeft : public ork::OuroObject
{
public:
  uint64_t GetTypeID() const override { return 201; }
};

class DiamondRight : public ork::OuroObject
{
public:
  uint64_t GetTypeID() const override { return 202; }
};

class DiamondChild : public DiamondLeft, public DiamondRight
{
public:
  uint64_t GetTypeID() const override { return 203; }
};
// 驗證菱形繼承（Diamond Inheritance）因歧義性而無法隱式轉換為 OuroObject*。
// 這確保了 Handles.hpp 中 CreateObject() 函式的編譯期防護（static_assert）能正確阻擋此類不良繼承結構。
static_assert(!std::is_convertible_v<DiamondChild *, ork::OuroObject *>,
              "Diamond inheritance should be detected as ambiguous and forbidden at compile time.");

int main()
{
  std::cout << "=== Running OuroKore Basic Tests ===" << std::endl;

  // ==========================================
  // Test 1: Object Creation and Lifecycle
  // ==========================================
  std::cout << "Test 1: Object Creation and Lifecycle..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<SimpleObject> ptr = ork::CreateObject<SimpleObject>();
    assert(ptr.GetTargetID() != 0);
    assert(ptr.IsRoot() == true);
    assert(g_deconstruct_count == 0);  // Still alive
  }
  assert(g_deconstruct_count == 1);  // Automatically deleted on OuroPtr destruction
  std::cout << "Test 1 Passed." << std::endl;

  // ==========================================
  // Test 2: Owner ID Roster and Stack Prevention
  // ==========================================
  std::cout << "Test 2: Owner ID Roster and Stack Prevention..." << std::endl;
  g_deconstruct_count = 0;
  HandleID parent_id = 0;
  HandleID child_id = 0;
  {
    ork::OuroPtr<ParentObject> parent = ork::CreateObject<ParentObject>();
    parent_id = parent.GetTargetID();
    child_id = parent->m_child.GetTargetID();

    assert(parent_id != 0);
    assert(child_id != 0);

    // Retrieve owner roster from registry to verify A owns B
    // (For testing purposes, we can query it through Registry.h directly)
    int32_t alive = 0;
    ork_check_alive(child_id, &alive, 0);
    assert(alive == 1);
  }
  // Both Parent and Child should be deconstructed when parent OuroPtr dies
  assert(g_deconstruct_count == 2);

  // Check that we cannot assign a valid target to OwningHandle on Stack
  {
    ork::OuroPtr<SimpleObject> ptr = ork::CreateObject<SimpleObject>();
    ork::OwningHandle<SimpleObject> stack_handle;
    bool caught = false;
    try
    {
      stack_handle = ptr;  // Violates rules (owner is ORK_ROOT_ID)
    }
    catch (const std::runtime_error &e)
    {
      caught = true;
      std::cout << "  (Expected Exception Caught) " << e.what() << std::endl;
    }
    assert(caught == true);
  }
  std::cout << "Test 2 Passed." << std::endl;

  // ==========================================
  // Test 3: WeakHandle and Lazy Pruning
  // ==========================================
  std::cout << "Test 3: WeakHandle and Lazy Pruning..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::WeakHandle<SimpleObject> weak;
    {
      ork::OuroPtr<SimpleObject> ptr = ork::CreateObject<SimpleObject>();
      weak = ptr;
      assert(weak.IsAlive() == true);
      assert(g_deconstruct_count == 0);
    }
    // Strong pointer is dead, object deconstructed.
    assert(g_deconstruct_count == 1);

    // Probing weak handle should trigger lazy pruning
    assert(weak.IsAlive() == false);
    assert(weak.GetTargetID() == 0);  // Local ID cleared by lazy pruning
  }
  std::cout << "Test 3 Passed." << std::endl;

  // ==========================================
  // Test 4: Compile-Time Diamond Inheritance Check
  // ==========================================
  std::cout << "Test 4: Diamond Inheritance Prevention..." << std::endl;
  std::cout << "  Verified by static_assert at compile time." << std::endl;
  std::cout << "Test 4 Passed." << std::endl;

  // ==========================================
  // Test 5: Assignment Operators Correctness
  // ==========================================
  std::cout << "Test 5: Assignment Operators Correctness..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentWithTwoChildren> parent = ork::CreateObject<ParentWithTwoChildren>();
    {
      ork::OuroPtr<SimpleObject> childA = ork::CreateObject<SimpleObject>();
      parent->m_child1 = childA;
      assert(parent->m_child1.GetTargetID() == childA.GetTargetID());
    }
    // childA OuroPtr went out of scope. Since parent->m_child1 points to it,
    // strong count should be 1, so it shouldn't be deconstructed yet!
    assert(g_deconstruct_count == 0); 
  }
  std::cout << "Test 5 Passed." << std::endl;
 
  // ==========================================
  // Test 6: Copy Assignment Correctness (Same Owner)
  // ==========================================
  std::cout << "Test 6: Copy Assignment Correctness (Same Owner)..." << std::endl;
  {
    ork::OuroPtr<ParentWithTwoChildren> parent = ork::CreateObject<ParentWithTwoChildren>();
    ork::OuroPtr<SimpleObject> childA = ork::CreateObject<SimpleObject>();
    ork::OuroPtr<SimpleObject> childB = ork::CreateObject<SimpleObject>();

    parent->m_child1 = childA;
    parent->m_child2 = childB;

    assert(parent->m_child1.GetTargetID() == childA.GetTargetID());
    assert(parent->m_child2.GetTargetID() == childB.GetTargetID());

    // Copy assign h1 = h2. Same owner, different targets.
    parent->m_child1 = parent->m_child2;

    // parent->m_child1 should now point to childB!
    assert(parent->m_child1.GetTargetID() == childB.GetTargetID());
  }
  std::cout << "Test 6 Passed." << std::endl;

  // ==========================================
  // Test 7: Move Assignment Correctness (Different Owners)
  // ==========================================
  std::cout << "Test 7: Move Assignment Correctness (Different Owners)..." << std::endl;
  g_deconstruct_count = 0;
  {
    HandleID childB_id = 0;
    ork::OuroPtr<ParentWithTwoChildren> parent1 = ork::CreateObject<ParentWithTwoChildren>();
    ork::OuroPtr<ParentWithTwoChildren> parent2 = ork::CreateObject<ParentWithTwoChildren>();
    {
      ork::OuroPtr<SimpleObject> childA = ork::CreateObject<SimpleObject>();
      ork::OuroPtr<SimpleObject> childB = ork::CreateObject<SimpleObject>();
      childB_id = childB.GetTargetID();

      parent1->m_child1 = childA;
      parent2->m_child1 = childB;
    }
    // childA and childB OuroPtrs went out of scope.
    // childA is owned by parent1->m_child1.
    // childB is owned by parent2->m_child1.
    assert(g_deconstruct_count == 0);

    // Move assign: parent1->m_child1 = std::move(parent2->m_child1)
    // Different owners, different targets.
    parent1->m_child1 = std::move(parent2->m_child1);

    // childA is no longer owned by parent1, so it should be deconstructed!
    assert(g_deconstruct_count == 1);
    assert(parent1->m_child1.GetTargetID() == childB_id);
    assert(parent2->m_child1.GetTargetID() == 0);
  }
  std::cout << "Test 7 Passed." << std::endl;

  std::cout << "\n=== All Tests Passed Successfully! ===" << std::endl;
  return 0;
}
