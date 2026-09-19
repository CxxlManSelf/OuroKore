#include "ourokore/host/OuroHost.hpp"
#include <iostream>
#include <cassert>
#include <atomic>

static std::atomic<int> g_cycle_node_dtor{0};

class CycleNode : public ork::OuroObject
{
public:
  CycleNode() = default;
  ~CycleNode() override
  {
    g_cycle_node_dtor.fetch_add(1, std::memory_order_relaxed);
  }

  ork::OwningHandle<ork::OuroObject> next{"next"};
};

void Test1_SelfLoop(ork::HostContext &host)
{
  std::cout << "[Test 1: 單節點自環回收 (Self-Loop Cycle Collection)]" << std::endl;
  g_cycle_node_dtor.store(0);

  {
    ork::OuroPtr<CycleNode> node = ork::CreateObject<CycleNode>();
    node->next = node; // 建立自環 edge: node -> node
    assert(g_cycle_node_dtor.load() == 0);
  } // node 離開作用域，Root edge 斷開，因自環計數仍為 1

  // 顯式執行循環參照收集
  host.CollectCycles();

  // 清空延遲銷毀佇列
  host.FlushDeferredDeletions();

  assert(g_cycle_node_dtor.load() == 1);
  std::cout << "  Test 1 通過: 自環物件成功由 CycleCollector 外科手術斷鏈並物理銷毀！\n" << std::endl;
}

void Test2_MutualCycle(ork::HostContext &host)
{
  std::cout << "[Test 2: 雙向互指閉環回收 (Mutual Cycle Collection, A <-> B)]" << std::endl;
  g_cycle_node_dtor.store(0);

  {
    ork::OuroPtr<CycleNode> a = ork::CreateObject<CycleNode>();
    ork::OuroPtr<CycleNode> b = ork::CreateObject<CycleNode>();

    a->next = b; // a 擁有 b
    b->next = a; // b 擁有 a
    assert(g_cycle_node_dtor.load() == 0);
  } // a, b 離開作用域，Root edges 斷開，彼此互指孤島

  host.CollectCycles();
  host.FlushDeferredDeletions();

  assert(g_cycle_node_dtor.load() == 2);
  std::cout << "  Test 2 通過: 雙向互指孤島成功被完整識別並全數回收！\n" << std::endl;
}

void Test3_ExternalRootProtection(ork::HostContext &host)
{
  std::cout << "[Test 3: 外部根存活保證防誤殺 (External Root Anti-False-Positive)]" << std::endl;
  g_cycle_node_dtor.store(0);

  ork::OuroPtr<CycleNode> root_keeper;
  {
    ork::OuroPtr<CycleNode> a = ork::CreateObject<CycleNode>();
    ork::OuroPtr<CycleNode> b = ork::CreateObject<CycleNode>();

    a->next = b;
    b->next = a;

    // 保留 a 的強引用在外部 root_keeper
    root_keeper = std::move(a);
  } // b 離開作用域，但因 a 持有 b，且 a 受到 root_keeper 持有

  // 嘗試觸發循環收集
  host.CollectCycles();
  host.FlushDeferredDeletions();
  assert(g_cycle_node_dtor.load() == 0);

  // 接著釋放外部根
  root_keeper = ork::OuroPtr<CycleNode>();

  // 此時 a 與 b 失去所有外部根，成為閉環孤島
  host.CollectCycles();
  host.FlushDeferredDeletions();
  assert(g_cycle_node_dtor.load() == 2);
  std::cout << "  Test 3 通過: 外部根可達時絕不誤殺，失去根後即刻被精確回收！\n" << std::endl;
}

void Test4_DeepTreeStackOverflowPrevention(ork::HostContext &host)
{
  std::cout << "[Test 4: 極深物件樹延遲銷毀非遞迴防爆棧測試 (Trampoline / Deep-Tree Anti-Stack-Overflow)]" << std::endl;
  g_cycle_node_dtor.store(0);

  const int DEPTH = 5000;
  std::cout << "  正在建構深度為 " << DEPTH << " 層之深層鏈表..." << std::endl;

  ork::OuroPtr<CycleNode> head = ork::CreateObject<CycleNode>();
  ork::OuroPtr<CycleNode> curr = ork::CreateObject<CycleNode>();
  head->next = curr;

  for (int i = 0; i < DEPTH - 1; ++i)
  {
    ork::OuroPtr<CycleNode> next_node = ork::CreateObject<CycleNode>();
    curr->next = next_node;
    curr = std::move(next_node);
  }

  std::cout << "  鏈表建構完畢，即將釋放 head 與 curr 指標引發連鎖解構..." << std::endl;
  curr = ork::OuroPtr<CycleNode>();
  head = ork::OuroPtr<CycleNode>();

  // 若採用傳統遞迴解構，5000+ 層呼叫棧將引發致命的 Stack Overflow 崩潰。
  // DeferredDeleteQueue 將其壓平並分流處理。
  host.FlushDeferredDeletions();

  std::cout << "  已銷毀節點總數: " << g_cycle_node_dtor.load() << " (預期 " << (DEPTH + 1) << ")" << std::endl;
  assert(g_cycle_node_dtor.load() == DEPTH + 1);
  std::cout << "  Test 4 通過: 巨樹連鎖解構順利由延遲銷毀隊列安全壓平，完全無崩潰！\n" << std::endl;
}

void Test5_AntiZombieReanimation(ork::HostContext &host)
{
  std::cout << "[Test 5: Destructing 狀態防加邊與弱引用防殭屍復活 (Anti-Zombie Reanimation)]" << std::endl;

  assert(ORK_STATUS_ERROR_DESTRUCTING == -6);

  ork::WeakHandle<CycleNode> weak;
  {
    ork::OuroPtr<CycleNode> node = ork::CreateObject<CycleNode>();
    node->next = node; // 建立自環
    weak = node;       // 建立弱引用
    assert(weak.IsAlive());
  } // node 離開作用域，失去外部根

  // 執行循環收集與延遲銷毀
  host.CollectCycles();
  host.FlushDeferredDeletions();

  // 驗證 WeakHandle 絕無法晉升復活已拆解之物件 (Anti-Zombie)
  ork::OuroPtr<CycleNode> acquired = weak.LockAndAcquire();
  assert(!acquired);
  assert(!weak.IsAlive());

  std::cout << "  Test 5 通過: 弱引用晉升防禦與 ORK_STATUS_ERROR_DESTRUCTING 規範正確！\n" << std::endl;
}

int main()
{
  std::cout << "==================================================" << std::endl;
  std::cout << "   OuroKore 循環參照追蹤與延遲銷毀子系統專屬測試   " << std::endl;
  std::cout << "==================================================\n" << std::endl;

  auto host = ork::Init();
  assert(host.IsValid());

  Test1_SelfLoop(host);
  Test2_MutualCycle(host);
  Test3_ExternalRootProtection(host);
  Test4_DeepTreeStackOverflowPrevention(host);
  Test5_AntiZombieReanimation(host);

  std::cout << "==================================================" << std::endl;
  std::cout << "   所有循環參照追蹤與延遲銷毀測試順利通過 (PASSED)! " << std::endl;
  std::cout << "==================================================" << std::endl;

  host.Shutdown();
  return 0;
}
