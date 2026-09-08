#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/InMemoryStorage.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/OuroLRUAutoDehydrator.hpp"

using namespace ork;

class TestItem : public ork::OuroObject
{
public:
  int m_value{0};
  std::string m_name;

  TestItem() = default;
  TestItem(int val, std::string name) :
      m_value(val),
      m_name(std::move(name))
  {
  }

  void SerializePayload(OuroStream &stream) const override
  {
    stream.WriteProperty("value", m_value);
    stream.WriteProperty("name", m_name);
  }

  void DeserializePayload(OuroStream &stream) override
  {
    stream.ReadProperty("value", m_value);
    stream.ReadProperty("name", m_name);
  }
};

class TestContainer : public ork::OuroObject
{
public:
  ork::OwningHandle<TestItem> m_item0{"item0"};
  ork::OwningHandle<TestItem> m_item1{"item1"};
  ork::OwningHandle<TestItem> m_item2{"item2"};
  ork::OwningHandle<TestItem> m_item3{"item3"};

  TestContainer() = default;

  void SerializePayload(OuroStream &) const override {}
  void DeserializePayload(OuroStream &) override {}
};

void test_lru_order_and_access()
{
  std::cout << "[測試 1] LRU 建立、復水熱度更新與淘汰順序測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();

  // 1. 建立三個子物件：A、B、C
  // 建立時依序放入 Head，目前順序為：C (頭/最熱) -> B -> A (尾/最冷)
  root->m_item0 = CreateObject<TestItem>(1, "ItemA");
  root->m_item1 = CreateObject<TestItem>(2, "ItemB");
  root->m_item2 = CreateObject<TestItem>(3, "ItemC");

  HandleID id_a = root->m_item0.GetTargetID();
  HandleID id_b = root->m_item1.GetTargetID();
  HandleID id_c = root->m_item2.GetTargetID();

  assert(dehydrator->IsTracked(id_a));
  assert(dehydrator->IsTracked(id_b));
  assert(dehydrator->IsTracked(id_c));
  assert(dehydrator->GetTrackedMemoryBytes() == sizeof(TestItem) * 3);

  // 2. 觸發單個物件脫水（batch_size = 1）
  // 預期最先建立的最冷物件 A 被脫水
  dehydrator->SetBatchSize(1);
  size_t count = dehydrator->TriggerDehydration();
  assert(count == 1);

  uint8_t state_a = 0;
  ork_get_storage_state(id_a, &state_a);
  assert(static_cast<StorageState>(state_a) == StorageState::Dehydrated);

  // 3. 將 A 復水（Rehydrate 自動將 A 拉回 MRU 頭端，成為最熱物件）
  // 隊列順序應轉變為：A (最熱) -> C -> B (最冷)
  auto ptr_a = Rehydrate<TestItem>(id_a);
  assert(ptr_a);
  assert(dehydrator->GetTrackedMemoryBytes() == sizeof(TestItem) * 3);

  // 4. 再次觸發脫水，此時最冷的 B 應該被脫水
  count = dehydrator->TriggerDehydration();
  assert(count == 1);
  uint8_t state_b = 0;
  ork_get_storage_state(id_b, &state_b);
  assert(static_cast<StorageState>(state_b) == StorageState::Dehydrated);

  // 5. 再次觸發脫水，次冷的 C 應該被脫水
  count = dehydrator->TriggerDehydration();
  assert(count == 1);
  uint8_t state_c = 0;
  ork_get_storage_state(id_c, &state_c);
  assert(static_cast<StorageState>(state_c) == StorageState::Dehydrated);

  // 6. 釋放 A 的持有指標以允許 A 脫水
  ptr_a.Release();

  // 最後觸發脫水，最熱的 A 終於被脫水
  count = dehydrator->TriggerDehydration();
  assert(count == 1);
  ork_get_storage_state(id_a, &state_a);
  assert(static_cast<StorageState>(state_a) == StorageState::Dehydrated);

  // 7. 驗證全部脫水後列管活體記憶體歸零
  assert(dehydrator->GetTrackedMemoryBytes() == 0);
  std::cout << "  -> LRU 淘汰順序完全符合預期（A -> B -> C -> A(Rehydrated)）！" << std::endl;
}

void test_memory_quota_eviction()
{
  std::cout << "[測試 2] 記憶體配額（Memory Quota）約束脫水測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();
  root->m_item0 = CreateObject<TestItem>(1, "QuotaA");
  root->m_item1 = CreateObject<TestItem>(2, "QuotaB");
  root->m_item2 = CreateObject<TestItem>(3, "QuotaC");
  root->m_item3 = CreateObject<TestItem>(4, "QuotaD");

  size_t single_size = sizeof(TestItem);
  assert(dehydrator->GetTrackedMemoryBytes() == single_size * 4);

  // 設定配額為 2 個物件的大小：應觸發脫水直到記憶體 <= 2 個物件大小
  dehydrator->SetMemoryLimit(single_size * 2);

  size_t freed = dehydrator->TriggerDehydration();
  assert(freed == 2);
  assert(dehydrator->GetTrackedMemoryBytes() == single_size * 2);

  // 再次觸發脫水：因為未超標，脫水數量應為 0
  freed = dehydrator->TriggerDehydration();
  assert(freed == 0);
  assert(dehydrator->GetTrackedMemoryBytes() == single_size * 2);

  std::cout << "  -> 記憶體配額控制完全符合預期！" << std::endl;
}

void test_in_flight_protection()
{
  std::cout << "[測試 3] In-Flight 活躍物件安全保護測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();
  // 先建立 hot_item（在串列尾端/較冷），後建立 cold_item（在串列頭端/較熱）
  root->m_item0 = CreateObject<TestItem>(1, "HotItem");
  root->m_item1 = CreateObject<TestItem>(2, "ColdItem");

  HandleID hot_id = root->m_item0.GetTargetID();
  HandleID cold_id = root->m_item1.GetTargetID();

  // 主執行緒此時正持有處於尾端之 hot 物件的活躍 OuroPtr（In-Flight，root_count == 1）
  auto hot_ptr = root->m_item0.LockAndAcquire();
  assert(hot_ptr);

  // 觸發脫水：hot 物件處於 LRU 尾端，但由於正在使用中，脫水必須安全略過它，轉而脫水 cold 物件
  dehydrator->SetBatchSize(10);
  size_t freed = dehydrator->TriggerDehydration();

  // hot 物件免疫於脫水
  uint8_t state_hot = 0;
  ork_get_storage_state(hot_id, &state_hot);
  assert(static_cast<StorageState>(state_hot) != StorageState::Dehydrated);

  // cold 物件被脫水
  uint8_t state_cold = 0;
  ork_get_storage_state(cold_id, &state_cold);
  assert(static_cast<StorageState>(state_cold) == StorageState::Dehydrated);

  assert(freed == 1);
  std::cout << "  -> In-Flight 活躍物件安全略過驗證成功！" << std::endl;
}

void test_background_thread_and_stop()
{
  std::cout << "[測試 4] 背景定時排程與即時停止（Stop）測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();
  root->m_item0 = CreateObject<TestItem>(100, "BackgroundTest");
  HandleID id = root->m_item0.GetTargetID();

  // 啟動背景執行緒，每隔 30 毫秒掃描一次
  assert(dehydrator->Start(std::chrono::milliseconds(30)));
  assert(dehydrator->IsRunning());

  // 等待約 100 毫秒，背景執行緒應該已自動執行脫水
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  uint8_t state = 0;
  ork_get_storage_state(id, &state);
  assert(static_cast<StorageState>(state) == StorageState::Dehydrated);
  assert(dehydrator->GetTotalRuns() > 0);

  // 測試即時退出性能（呼叫 Stop 應在 100ms 內迅速退出，不死鎖、不等待完整週期）
  auto start_stop = std::chrono::steady_clock::now();
  dehydrator->Stop();
  auto elapsed_stop =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_stop);

  assert(!dehydrator->IsRunning());
  assert(elapsed_stop.count() < 100);
  std::cout << "  -> 背景排程運作正常，且 Stop() 在 " << elapsed_stop.count() << " ms 內極速退出！" << std::endl;
}

void test_failed_dehydration_requeue()
{
  std::cout << "[測試 5] 脫水失敗物件重排（防止隊頭阻塞 Head-of-Line Blocking）測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();
  root->m_item0 = CreateObject<TestItem>(1, "BusyTailItem");
  root->m_item1 = CreateObject<TestItem>(2, "IdleItem");

  HandleID busy_id = root->m_item0.GetTargetID();
  HandleID idle_id = root->m_item1.GetTargetID();

  // busy_id 在建立時較早，處於 LRU 最冷端 (Tail)；idle_id 在 MRU 頭端
  // 主執行緒此時鎖定並持有 busy_id (In-Flight)
  auto busy_ptr = root->m_item0.LockAndAcquire();
  assert(busy_ptr);

  // 設定批次大小為 1
  dehydrator->SetBatchSize(1);

  // 第一輪脫水：評估 Tail (busy_id)，因 In-Flight 脫水失敗
  // 機制應將其重排至 MRU 頭端，使得 idle_id 晉升至尾端
  size_t freed_round1 = dehydrator->TriggerDehydration();
  assert(freed_round1 == 0);

  // 驗證 busy_id 依然存活
  uint8_t state_busy = 0;
  ork_get_storage_state(busy_id, &state_busy);
  assert(static_cast<StorageState>(state_busy) != StorageState::Dehydrated);

  // 第二輪脫水：現在尾端是 idle_id，應能順利脫水，絕不被卡死！
  size_t freed_round2 = dehydrator->TriggerDehydration();
  assert(freed_round2 == 1);

  uint8_t state_idle = 0;
  ork_get_storage_state(idle_id, &state_idle);
  assert(static_cast<StorageState>(state_idle) == StorageState::Dehydrated);

  std::cout << "  -> 脫水失敗重排機制驗證成功，完美防止隊頭阻塞（Head-of-Line Blocking）！" << std::endl;
}

int main()
{
  try
  {
    std::cout << "=== 開始執行 OuroLRUAutoDehydrator 單元測試 ===" << std::endl;

    auto storage = std::make_shared<InMemoryStorage>();
    auto initial_dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
    ork::Init(storage, initial_dehydrator);

    test_lru_order_and_access();
    test_memory_quota_eviction();
    test_in_flight_protection();
    test_background_thread_and_stop();
    test_failed_dehydration_requeue();

    std::cout << "=== OuroLRUAutoDehydrator 所有測試全部通過！ ===" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "測試發生例外: " << e.what() << std::endl;
    return 1;
  }
}
