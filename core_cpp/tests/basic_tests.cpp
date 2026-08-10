#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "ourokore/c_api/core.h"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"

static int g_deconstruct_count = 0;

class SimpleObject : public ork::OuroObject
{
public:
  SimpleObject() = default;
  ~SimpleObject() override { g_deconstruct_count++; }

  int GetValue() const
  {
    ork::OuroReadLock lock(*this);
    return m_value;
  }

  void SetValue(int val)
  {
    ork::OuroWriteLock lock(*this);
    m_value = val;
  }

private:
  int m_value = 0;
};

// Classes for Test 2 (Parent-Child topology)
class ChildObject : public ork::OuroObject
{
public:
  ChildObject() = default;
  ~ChildObject() override { g_deconstruct_count++; }
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

  ork::OwningHandle<ChildObject> m_child{"m_child"};
};

class ParentWithTwoChildren : public ork::OuroObject
{
public:
  ParentWithTwoChildren() = default;
  ~ParentWithTwoChildren() override { g_deconstruct_count++; }

  ork::OwningHandle<SimpleObject> m_child1{"m_child1"};
  ork::OwningHandle<SimpleObject> m_child2{"m_child2"};
};

// Compile-time validation for Test 4 (Diamond inheritance prevention)
class DiamondLeft : public ork::OuroObject
{
public:
};

class DiamondRight : public ork::OuroObject
{
public:
};

class DiamondChild : public DiamondLeft, public DiamondRight
{
public:
};
// 驗證菱形繼承（Diamond Inheritance）因歧義性而無法隱式轉換為 OuroObject*。
// 這確保了 Handles.hpp 中 CreateObject() 函式的編譯期防護（static_assert）能正確阻擋此類不良繼承結構。
static_assert(!std::is_convertible_v<DiamondChild *, ork::OuroObject *>,
              "Diamond inheritance should be detected as ambiguous and forbidden at compile time.");

class ParentDrivenObject : public ork::OuroObject
{
public:
  ParentDrivenObject() = default;
  ~ParentDrivenObject() override { g_deconstruct_count++; }

  void InitChildren() { m_child = ork::CreateObject<ChildObject>(); }

  ork::OwningHandle<ChildObject> m_child{"m_child"};
};

class ParentDrivenCtorObject : public ork::OuroObject
{
public:
  ParentDrivenCtorObject() { m_child = ork::CreateObject<ChildObject>(); }
  ~ParentDrivenCtorObject() override { g_deconstruct_count++; }

  ork::OwningHandle<ChildObject> m_child{"m_child"};
};

class ParentDrivenAdoptObject : public ork::OuroObject
{
public:
  ParentDrivenAdoptObject() = default;
  ~ParentDrivenAdoptObject() override { g_deconstruct_count++; }

  ork::OwningHandle<SimpleObject> m_child{"m_child"};
};

class ParentWithContainer : public ork::OuroObject
{
public:
  ParentWithContainer() = default;
  ~ParentWithContainer() override { g_deconstruct_count++; }

  void AddChildObject(const ork::OuroPtr<SimpleObject> &child) { m_children.AddTarget(child.GetTargetID()); }
  void RemoveChildObject(HandleID child_id) { m_children.RemoveTarget(child_id); }
  size_t GetChildrenCount() const { return m_children.GetTargetCount(); }

  /**
   * @brief 在 Parent 鎖保護狀態下取得所有子物件控制指標
   */
  std::vector<ork::OuroPtr<SimpleObject>> GetChildrenObjects() const
  {
    return m_children.LockAndAcquireAll<SimpleObject>();
  }

private:
  ork::OwningContainerHandle m_children{"children_slot"};
};

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
    assert(g_deconstruct_count == 0);  // Still alive
  }
  assert(g_deconstruct_count == 1);  // Automatically deleted on OuroPtr destruction

  // Sub-test: LockAndAcquire OuroPtr holds root edge and protects object lifetime
  {
    g_deconstruct_count = 0;
    HandleID child_id = 0;
    {
      ork::OuroPtr<ParentWithTwoChildren> parent = ork::CreateObject<ParentWithTwoChildren>();
      ork::OuroPtr<SimpleObject> child_guard;
      {
        ork::OuroPtr<SimpleObject> child = ork::CreateObject<SimpleObject>();
        child_id = child.GetTargetID();
        parent->m_child1 = child;
      }
      // Lock and acquire temporary OuroPtr guard
      child_guard = parent->m_child1.LockAndAcquire();
      assert(child_guard.GetTargetID() == child_id);

      // Parent releases its handle
      parent->m_child1.Release();

      // Object must STILL be alive because child_guard holds an active root edge!
      assert(g_deconstruct_count == 0);
      int32_t alive = 0;
      ork_check_alive(child_id, &alive, 0);
      assert(alive == 1);
    }
    // child_guard and parent out of scope -> object now deconstructed
    assert(g_deconstruct_count == 2);
  }
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
    ork::OwningHandle<SimpleObject> stack_handle{"stack_handle"};
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

  // ==========================================
  // Test 8: ActiveOwnerGuard 容器批次插入測試
  // ==========================================
  std::cout << "Test 8: ActiveOwnerGuard 容器批次插入測試..." << std::endl;
  g_deconstruct_count = 0;
  {
    HandleID fake_parent_id = 8888;
    std::vector<ork::OwningHandle<SimpleObject>> container;

    // 建立 active owner 上下文守護者，模擬 parent 正在對肚子裡的容器進行批次操作
    {
      ork::ActiveOwnerGuard guard(fake_parent_id);

      // 建立數個 SimpleObject 並插入容器
      ork::OuroPtr<SimpleObject> obj1 = ork::CreateObject<SimpleObject>();
      ork::OuroPtr<SimpleObject> obj2 = ork::CreateObject<SimpleObject>();
      ork::OuroPtr<SimpleObject> obj3 = ork::CreateObject<SimpleObject>();

      assert(obj1.GetTargetID() != 0);
      assert(obj2.GetTargetID() != 0);
      assert(obj3.GetTargetID() != 0);

      // 這些 OwningHandle 會因為 Thread-Local 上下文而自動將 m_owner_id 設定為 fake_parent_id
      container.emplace_back("slot1", obj1);
      container.emplace_back("slot2", obj2);
      container.emplace_back("slot3", obj3);
    }  // OuroPtrs 和 guard 出作用域

    // 驗證容器中 OwningHandle 的 Owner ID 是否為 fake_parent_id
    for (const auto &handle : container)
    {
      assert(handle.GetOwnerID() == fake_parent_id);
      assert(handle.GetTargetID() != 0);
    }

    // 由於 container 持有強引用，物件不應被析構
    assert(g_deconstruct_count == 0);
  }
  // container 析構後，所有物件應被正確釋放
  assert(g_deconstruct_count == 3);
  std::cout << "Test 8 Passed." << std::endl;

  // ==========================================
  // Test 9: 容器擴容之同宿主 Zero-Cost Move 測試
  // ==========================================
  std::cout << "Test 9: 容器擴容之同宿主 Zero-Cost Move 測試..." << std::endl;
  g_deconstruct_count = 0;
  {
    HandleID fake_parent_id = 9999;
    std::vector<ork::OwningHandle<SimpleObject>> container;

    {
      ork::ActiveOwnerGuard guard(fake_parent_id);

      // 批次插入大量物件，故意超過 vector 的預設 capacity 以觸發擴容
      // 這會讓 vector 在記憶體重分配時，對原本的 OwningHandle 呼叫移動建構/移動賦值
      for (int i = 0; i < 20; ++i)
      {
        ork::OuroPtr<SimpleObject> obj = ork::CreateObject<SimpleObject>();
        container.emplace_back("slot_" + std::to_string(i), obj);
      }
    }

    // 驗證在動態擴容（同宿主搬移）過程中，完全沒有觸發物件的 Release 析構
    assert(g_deconstruct_count == 0);

    // 驗證所有元素的 owner 依然是 fake_parent_id
    for (const auto &handle : container)
    {
      assert(handle.GetOwnerID() == fake_parent_id);
      assert(handle.GetTargetID() != 0);
    }

    // 手動模擬同宿主之間 Move
    {
      ork::ActiveOwnerGuard guard(fake_parent_id);
      ork::OwningHandle<SimpleObject> handle1 = std::move(container[0]);
      assert(handle1.GetOwnerID() == fake_parent_id);
      assert(container[0].GetTargetID() == 0);  // 來源置零

      // 移動賦值
      container[0] = std::move(handle1);
      assert(container[0].GetOwnerID() == fake_parent_id);
      assert(handle1.GetTargetID() == 0);  // 來源置零
    }

    assert(g_deconstruct_count == 0);  // 過程中依然不觸發析構
  }
  assert(g_deconstruct_count == 20);  // 容器銷毀時全部正確釋放
  std::cout << "Test 9 Passed." << std::endl;

  // ==========================================
  // Test 10: 跨宿主 Move 語意與舊目標 Release 測試
  // ==========================================
  std::cout << "Test 10: 跨宿主 Move 語意與舊目標 Release 測試..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentWithTwoChildren> parent1 = ork::CreateObject<ParentWithTwoChildren>();
    ork::OuroPtr<ParentWithTwoChildren> parent2 = ork::CreateObject<ParentWithTwoChildren>();

    HandleID parent1_id = parent1.GetTargetID();
    HandleID parent2_id = parent2.GetTargetID();

    HandleID childA_id = 0;
    HandleID childB_id = 0;

    {
      ork::OuroPtr<SimpleObject> childA = ork::CreateObject<SimpleObject>();
      childA_id = childA.GetTargetID();
      parent1->m_child1 = childA;
    }

    {
      ork::OuroPtr<SimpleObject> childB = ork::CreateObject<SimpleObject>();
      childB_id = childB.GetTargetID();
      parent2->m_child1 = childB;
    }

    assert(parent1->m_child1.GetOwnerID() == parent1_id);
    assert(parent2->m_child1.GetOwnerID() == parent2_id);
    assert(g_deconstruct_count == 0);

    // 跨宿主 Move Assignment：把 parent2 的子物件移交給 parent1 的 Handle
    // 依據三步合約，這會：
    // 1. 釋放 parent1->m_child1 的舊目標 (childA) -> 觸發 childA 析構
    // 2. 將 childB 的 Target ID 移交，並把 parent2->m_child1 的 target 設為 0
    // 3. 偵測到 Owner ID 不同 (parent1_id != parent2_id)，呼叫 Registry 註冊新邊 (parent1_id -> childB_id) 並解除舊邊
    // (parent2_id -> childB_id)
    parent1->m_child1 = std::move(parent2->m_child1);

    // 1. 驗證 childA 已經被釋放並析構
    assert(g_deconstruct_count == 1);

    // 2. 驗證 parent1->m_child1 現在持有 childB，且 owner 依然是 parent1_id
    assert(parent1->m_child1.GetTargetID() == childB_id);
    assert(parent1->m_child1.GetOwnerID() == parent1_id);

    // 3. 驗證 parent2->m_child1 的 target 被安全置零
    assert(parent2->m_child1.GetTargetID() == 0);

    // 4. 驗證 childB 依然存活，且其 Owner Roster 被 Registry 正確更新為僅有 parent1_id
    int32_t alive = 0;
    ork_check_alive(childB_id, &alive, 0);
    assert(alive == 1);
  }
  // parent1 & parent2 析構，childB 釋放。總析構數為：childA + parent1 + parent2 + childB = 4。
  assert(g_deconstruct_count == 4);
  std::cout << "Test 10 Passed." << std::endl;

  // ==========================================
  // Test 11: Parent-Driven Child Creation
  // ==========================================
  std::cout << "Test 11: Parent-Driven Child Creation..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentDrivenObject> parent = ork::CreateObject<ParentDrivenObject>();
    HandleID parent_id = parent.GetTargetID();

    // Create child post-construction
    parent->InitChildren();
    HandleID child_id = parent->m_child.GetTargetID();

    assert(parent_id != 0);
    assert(child_id != 0);
    assert(parent->m_child.GetOwnerID() == parent_id);

    int32_t alive = 0;
    ork_check_alive(child_id, &alive, 0);
    assert(alive == 1);
  }
  // Parent and Child deconstructed
  assert(g_deconstruct_count == 2);

  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentDrivenCtorObject> parent = ork::CreateObject<ParentDrivenCtorObject>();
    assert(parent.GetTargetID() != 0);
    assert(parent->m_child.GetTargetID() != 0);
    assert(parent->m_child.GetOwnerID() == parent.GetTargetID());
  }
  assert(g_deconstruct_count == 2);
  std::cout << "Test 11 Passed." << std::endl;

  // ==========================================
  // Test 12: Parent-Driven Adoption
  // ==========================================
  std::cout << "Test 12: Parent-Driven Adoption..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentDrivenAdoptObject> parent = ork::CreateObject<ParentDrivenAdoptObject>();
    HandleID parent_id = parent.GetTargetID();
    HandleID child_id = 0;
    {
      ork::OuroPtr<SimpleObject> child = ork::CreateObject<SimpleObject>();
      child_id = child.GetTargetID();

      // Adopt via OuroPtr assignment
      parent->m_child = child;
      assert(parent->m_child.GetTargetID() == child_id);
      assert(parent->m_child.GetOwnerID() == parent_id);
    }
    // child OuroPtr goes out of scope, but parent still owns it
    assert(g_deconstruct_count == 0);

    int32_t alive = 0;
    ork_check_alive(child_id, &alive, 0);
    assert(alive == 1);
  }
  assert(g_deconstruct_count == 2);
  std::cout << "Test 12 Passed." << std::endl;

  // ==========================================
  // Test 13: Same-Target Move-Assignment Safety
  // ==========================================
  std::cout << "Test 13: Same-Target Move-Assignment Safety..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentWithTwoChildren> parent = ork::CreateObject<ParentWithTwoChildren>();
    {
      ork::OuroPtr<SimpleObject> child = ork::CreateObject<SimpleObject>();
      parent->m_child1 = child;
      parent->m_child2 = child;
    }
    assert(g_deconstruct_count == 0);

    // Move assign: same target, same owner
    parent->m_child1 = std::move(parent->m_child2);
    assert(parent->m_child1.GetTargetID() != 0);
    assert(parent->m_child2.GetTargetID() == 0);
    assert(g_deconstruct_count == 0);  // Target should not be deconstructed!
  }
  assert(g_deconstruct_count == 2);  // Parent and Child deconstructed
  std::cout << "Test 13 Passed." << std::endl;

  // ==========================================
  // Test 14: OwningContainerHandle 多態與多重子物件拓撲測試
  // ==========================================
  std::cout << "Test 14: OwningContainerHandle 多態與多重子物件拓撲測試..." << std::endl;
  g_deconstruct_count = 0;
  {
    ork::OuroPtr<ParentWithContainer> parent = ork::CreateObject<ParentWithContainer>();
    HandleID parent_id = parent.GetTargetID();
    assert(parent_id != 0);

    // 驗證 RegisterHandle 成功將私有 OwningContainerHandle 註冊進父物件的名冊
    const auto &roster = parent->GetRegisteredHandles();
    assert(roster.find("children_slot") != roster.end());
    assert(roster.at("children_slot")->GetSlotName() == "children_slot");

    HandleID child1_id = 0;
    HandleID child2_id = 0;
    HandleID child3_id = 0;

    {
      ork::OuroPtr<SimpleObject> child1 = ork::CreateObject<SimpleObject>();
      ork::OuroPtr<SimpleObject> child2 = ork::CreateObject<SimpleObject>();
      ork::OuroPtr<SimpleObject> child3 = ork::CreateObject<SimpleObject>();

      child1_id = child1.GetTargetID();
      child2_id = child2.GetTargetID();
      child3_id = child3.GetTargetID();

      parent->AddChildObject(child1);
      parent->AddChildObject(child2);
      parent->AddChildObject(child3);

      assert(parent->GetChildrenCount() == 3);
    }
    // 3 個臨時 CreateObject 的 OuroPtr 寫鎖離開作用域並釋放，但 parent 的 OwningContainerHandle 持有拓撲強引用，皆未析構
    assert(g_deconstruct_count == 0);

    // 測試透過 Parent 封裝介面在 Parent 鎖定下無衝突打包與鎖定所有子物件
    {
      auto acquired_all = parent->GetChildrenObjects();
      assert(acquired_all.size() == 3);
      assert(acquired_all[0].GetTargetID() == child1_id);
      assert(acquired_all[1].GetTargetID() == child2_id);
      assert(acquired_all[2].GetTargetID() == child3_id);
    }

    // 測試從容器中透過 Parent 封裝介面個別移除子物件
    parent->RemoveChildObject(child1_id);
    assert(parent->GetChildrenCount() == 2);
    // child1 失去所有 Owner 引用，觸發即時析構
    assert(g_deconstruct_count == 1);

    int32_t alive1 = 0, alive2 = 0, alive3 = 0;
    ork_check_alive(child1_id, &alive1, 0);
    ork_check_alive(child2_id, &alive2, 0);
    ork_check_alive(child3_id, &alive3, 0);
    assert(alive1 == 0);
    assert(alive2 == 1);
    assert(alive3 == 1);
  }
  // parent 出作用域，ReleaseAll() 觸發釋放剩餘 2 個子物件 + parent 本身析構
  assert(g_deconstruct_count == 4);
  std::cout << "Test 14 Passed." << std::endl;

  // ==========================================
  // Test 15: 生命週期 OuroPtr、Const 重載與自主鎖定 (OuroReadLock/OuroWriteLock) 測試
  // ==========================================
  std::cout << "\nTest 15: 生命週期 OuroPtr、Const 重載與自主鎖定 (OuroReadLock/OuroWriteLock) 測試..." << std::endl;
  {
    ork::OuroPtr<ParentWithTwoChildren> parent = ork::CreateObject<ParentWithTwoChildren>();

    {
      ork::OuroPtr<SimpleObject> child_ptr = ork::CreateObject<SimpleObject>();
      // 測試由 SimpleObject 成員函式內部自主呼叫 OuroWriteLock 修改屬性
      child_ptr->SetValue(42);
      assert(child_ptr->GetValue() == 42);
      std::cout << "  [1. 物件建立與寫鎖更新] 初始化 Value: " << child_ptr->GetValue() << std::endl;

      parent->m_child1 = child_ptr;
    }

    // 1. 測試在 const 上下文 (const Handle) 呼叫 LockAndAcquire()
    const auto &const_parent = parent;
    {
      // const Handle 自動發放 OuroPtr<const SimpleObject>
      ork::OuroPtr<const SimpleObject> const_ptr = const_parent->m_child1.LockAndAcquire();
      assert(const_ptr->GetValue() == 42);
      std::cout << "  [2. Const 重載] const Handle 匯出 OuroPtr<const T>，唯讀 Value: " << const_ptr->GetValue()
                << std::endl;
      // 說明：此時若嘗試呼叫 const_ptr->SetValue(100)，將在編譯期直接觸發編譯錯誤！
    }

    // 2. 測試在非 const 上下文 (非 const Handle) 呼叫 LockAndAcquire()
    {
      // 非 const Handle 自動發放 OuroPtr<SimpleObject> (可變內容)
      ork::OuroPtr<SimpleObject> mutable_ptr = parent->m_child1.LockAndAcquire();
      mutable_ptr->SetValue(100);
      assert(mutable_ptr->GetValue() == 100);
      std::cout << "  [3. 可變重載] 可變 Handle 匯出 OuroPtr<T>，更新 Value 為: " << mutable_ptr->GetValue()
                << std::endl;
    }

    // 3. 測試 OwningHandle 及 WeakHandle 接受 OuroPtr<const SimpleObject> 指派與建構
    {
      ork::OuroPtr<const SimpleObject> const_ptr = parent->m_child1.LockAndAcquire();

      // OwningHandle &operator=(const OuroPtr<U>&) 支援 const T / OuroPtr<const T>
      parent->m_child2 = const_ptr;
      assert(parent->m_child2.GetTargetID() == const_ptr.GetTargetID());

      // WeakHandle 支援從 OuroPtr<const T> 建構與指派
      ork::WeakHandle<SimpleObject> weak_from_const(const_ptr);
      assert(weak_from_const.GetTargetID() == const_ptr.GetTargetID());

      ork::WeakHandle<SimpleObject> weak_assigned;
      weak_assigned = const_ptr;
      assert(weak_assigned.GetTargetID() == const_ptr.GetTargetID());

      std::cout << "  [4. const T 接受度測試] OwningHandle 與 WeakHandle 皆可順利接受 OuroPtr<const T>" << std::endl;
    }
  }
  std::cout << "Test 15 Passed." << std::endl;

  std::cout << "\n=== All Tests Passed Successfully! ===" << std::endl;
  return 0;
}
