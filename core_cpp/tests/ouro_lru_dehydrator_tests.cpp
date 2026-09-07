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
  std::cout << "[測試 1] LRU 存取熱度與淘汰順序測試..." << std::endl;

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

  // 2. 存取 A（提升 A 的熱度到最前頭）
  // 預期順序轉變為：A (最熱) -> C -> B (最冷)
  dehydrator->OnObjectAccess(id_a);

  // 3. 觸發單個物件脫水（batch_size = 1）
  dehydrator->SetBatchSize(1);
  size_t count = dehydrator->TriggerDehydration();
  assert(count == 1);

  // 驗證最冷的 B 被脫水了
  uint8_t state_b = 0;
  ork_get_storage_state(id_b, &state_b);
  assert(static_cast<StorageState>(state_b) == StorageState::Dehydrated);

  // 驗證 A 與 C 尚未被脫水
  uint8_t state_c = 0;
  ork_get_storage_state(id_c, &state_c);
  assert(static_cast<StorageState>(state_c) != StorageState::Dehydrated);

  // 4. 再次觸發脫水，次冷的 C 應該被脫水
  count = dehydrator->TriggerDehydration();
  assert(count == 1);
  ork_get_storage_state(id_c, &state_c);
  assert(static_cast<StorageState>(state_c) == StorageState::Dehydrated);

  // 5. 最後觸發脫水，最熱的 A 終於被脫水
  count = dehydrator->TriggerDehydration();
  assert(count == 1);
  uint8_t state_a = 0;
  ork_get_storage_state(id_a, &state_a);
  assert(static_cast<StorageState>(state_a) == StorageState::Dehydrated);

  // 6. 驗證全部脫水後列管活體記憶體歸零
  assert(dehydrator->GetTrackedMemoryBytes() == 0);
  std::cout << "  -> LRU 淘汰順序完全符合預期（B -> C -> A）！" << std::endl;
}

void test_memory_quota_eviction()
{
  std::cout << "[測試 2] 記憶體配額（Memory Quota）約束脫水測試..." << std::endl;

  auto dehydrator = std::make_shared<OuroLRUAutoDehydrator>();
  detail::GetAutoDehydratorRef() = dehydrator;

  auto root = CreatePermanentObject<TestContainer>();
  root->m_item0 = CreateObject<TestItem>(0, "Q0");
  root->m_item1 = CreateObject<TestItem>(1, "Q1");
  root->m_item2 = CreateObject<TestItem>(2, "Q2");
  root->m_item3 = CreateObject<TestItem>(3, "Q3");

  size_t single_size = sizeof(TestItem);
  assert(dehydrator->GetTrackedMemoryBytes() == single_size * 4);

  // 設定配額為 2 個物件大小
  dehydrator->SetMemoryLimit(single_size * 2);

  // 觸發脫水：應該剛好脫水 2 個物件，使活體記憶體降至 single_size * 2
  size_t freed = dehydrator->TriggerDehydration();
  assert(freed == 2);
  assert(dehydrator->GetTrackedMemoryBytes() == single_size * 2);

  // 再次觸發：因為已經小於等於配額，應該不脫水任何物件
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
  root->m_item0 = CreateObject<TestItem>(1, "ColdItem");
  root->m_item1 = CreateObject<TestItem>(2, "HotItem");

  HandleID cold_id = root->m_item0.GetTargetID();
  HandleID hot_id = root->m_item1.GetTargetID();

  // 故意將 cold_id 移到 MRU 頭端，使得 hot_id 處於 LRU 尾端
  dehydrator->OnObjectAccess(cold_id);

  // 但主執行緒此時正持有 hot 物件的活躍 OuroPtr（In-Flight，root_count == 1）
  auto hot_ptr = root->m_item1.LockAndAcquire();
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

    std::cout << "=== OuroLRUAutoDehydrator 所有測試全部通過！ ===" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "測試發生例外: " << e.what() << std::endl;
    return 1;
  }
}
