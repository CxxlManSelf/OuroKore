#include <cassert>
#include <iostream>
#include <vector>

#include "ourokore/component/AsyncResult.hpp"
#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroCore.hpp"
#include "ourokore/component/builtin/InMemoryStorage.hpp"

using namespace ork;

class AsyncTestEntity : public ork::OuroObject
{
public:
  int m_id_val{0};
  std::string m_tag;

  AsyncTestEntity() = default;
  AsyncTestEntity(int id_val, std::string tag) :
      m_id_val(id_val),
      m_tag(std::move(tag))
  {
  }

  void SerializePayload(OuroStream &stream) const override
  {
    stream.WriteProperty("id_val", m_id_val);
    stream.WriteProperty("tag", m_tag);
  }

  void DeserializePayload(OuroStream &stream) override
  {
    stream.ReadProperty("id_val", m_id_val);
    stream.ReadProperty("tag", m_tag);
  }
};

class AsyncEntityContainer : public ork::OuroObject
{
public:
  ork::OwningHandle<AsyncTestEntity> m_child{"child"};

  AsyncEntityContainer() = default;

  void SerializePayload(OuroStream &) const override {}
  void DeserializePayload(OuroStream &) override {}
};

class AsyncDerivedEntity : public AsyncTestEntity
{
public:
  AsyncDerivedEntity() = default;
  AsyncDerivedEntity(int id_val, std::string tag, double extra) :
      AsyncTestEntity(id_val, std::move(tag)),
      m_extra(extra)
  {
  }
  double m_extra{3.14};
};

void test_single_async_save_load()
{
  std::cout << "[測試 1] 單一物件非同步 SaveAsync 與 LoadAsync 測試..." << std::endl;

  auto ptr = CreateObject<AsyncTestEntity>(42, "OriginalTag");
  HandleID id = ptr.GetTargetID();

  // 1. 非同步儲存
  auto fut_save = SaveAsync(ptr);
  auto res_save = fut_save.get();

  assert(res_save.success);
  assert(res_save.id == id);
  assert(res_save.ptr.GetTargetID() == id);
  assert(res_save.error.empty());
  assert(static_cast<bool>(res_save));

  // 2. 修改記憶體中的活體資料
  {
    OuroWriteLock lock(*ptr);
    ptr->m_id_val = 999;
    ptr->m_tag = "ModifiedTag";
  }
  assert(ptr->m_id_val == 999);

  // 3. 非同步載入（刷回儲存體先前的狀態）
  auto fut_load = LoadAsync(ptr);
  auto res_load = fut_load.get();

  assert(res_load.success);
  assert(res_load.id == id);
  assert(res_load.ptr.GetTargetID() == id);
  assert(res_load.error.empty());

  // 驗證狀態已成功還原
  assert(ptr->m_id_val == 42);
  assert(ptr->m_tag == "OriginalTag");
  std::cout << "  -> SaveAsync 與 LoadAsync 驗證通過！" << std::endl;
}

void test_single_async_dehydrate_rehydrate()
{
  std::cout << "[測試 2] 單一物件非同步 DehydrateAsync 與 RehydrateAsync 測試..." << std::endl;

  auto container = CreatePermanentObject<AsyncEntityContainer>();
  container->m_child = CreateObject<AsyncTestEntity>(100, "ChildToDehydrate");
  HandleID child_id = container->m_child.GetTargetID();

  // 1. 非同步脫水（child_id 處於 root_count == 0 狀態）
  auto fut_deh = DehydrateAsync(child_id);
  auto res_deh = fut_deh.get();

  assert(res_deh.success);
  assert(res_deh.id == child_id);
  assert(res_deh.error.empty());

  uint8_t state = 0;
  ork_get_storage_state(child_id, &state);
  assert(static_cast<StorageState>(state) == StorageState::Dehydrated);

  // 2. 非同步復水
  auto fut_reh = RehydrateAsync<AsyncTestEntity>(child_id);
  auto res_reh = fut_reh.get();

  assert(res_reh.success);
  assert(res_reh.id == child_id);
  assert(res_reh.ptr.GetTargetID() == child_id);
  assert(res_reh.error.empty());

  // 驗證復水後資料正確性
  assert(res_reh.ptr->m_id_val == 100);
  assert(res_reh.ptr->m_tag == "ChildToDehydrate");

  std::cout << "  -> DehydrateAsync 與 RehydrateAsync 驗證通過！" << std::endl;
}

void test_failure_handling()
{
  std::cout << "[測試 3] 失敗情境與防呆保證測試..." << std::endl;

  // 嘗試復水不存在的 ID
  HandleID invalid_id = 88888888;
  auto fut = RehydrateAsync<AsyncTestEntity>(invalid_id);
  auto res = fut.get();

  // 核心契約驗證：
  // 1. res.id 必須有效（等於當初請求的 ID）
  assert(res.id == invalid_id);
  // 2. res.success 必須為 false
  assert(!res.success);
  assert(!static_cast<bool>(res));
  // 3. res.ptr 失敗時保證為空指標
  assert(!res.ptr);
  assert(res.ptr.GetTargetID() == 0);
  // 4. res.error 必須記錄具體錯誤原因
  assert(!res.error.empty());

  std::cout << "  -> 失敗防呆與錯誤訊息正確捕獲: \"" << res.error << "\"" << std::endl;
}

void test_parallel_batch_operations()
{
  std::cout << "[測試 4] 多核心批次並行操作（Batch APIs）測試..." << std::endl;

  constexpr size_t BATCH_COUNT = 20;
  std::vector<OuroPtr<AsyncTestEntity>> batch;
  batch.reserve(BATCH_COUNT);

  for (size_t i = 0; i < BATCH_COUNT; ++i)
  {
    batch.push_back(CreateObject<AsyncTestEntity>(static_cast<int>(i), "BatchItem_" + std::to_string(i)));
  }

  // 1. 批次多核心平行儲存
  auto save_results = SaveBatch(batch);
  assert(save_results.size() == BATCH_COUNT);
  for (size_t i = 0; i < BATCH_COUNT; ++i)
  {
    assert(save_results[i].success);
    assert(save_results[i].id == batch[i].GetTargetID());
    assert(save_results[i].ptr.GetTargetID() == batch[i].GetTargetID());
  }

  // 修改所有活體資料
  for (auto &item : batch)
  {
    OuroWriteLock lock(*item);
    item->m_id_val += 1000;
  }

  // 2. 批次多核心平行載入（全部還原）
  auto load_results = LoadBatch(batch);
  assert(load_results.size() == BATCH_COUNT);
  for (size_t i = 0; i < BATCH_COUNT; ++i)
  {
    assert(load_results[i].success);
    assert(batch[i]->m_id_val == static_cast<int>(i));
  }

  std::cout << "  -> 多核心批次 SaveBatch 與 LoadBatch 驗證成功！" << std::endl;
}

void test_async_destruction_and_flush()
{
  std::cout << "[測試 5] 物件銷毀非同步磁碟清理與 FlushStorage 測試..." << std::endl;

  auto storage = GetStorageDriver();
  assert(storage != nullptr);

  HandleID id = 0;
  {
    auto ptr = CreateObject<AsyncTestEntity>(777, "DestructionTest");
    id = ptr.GetTargetID();
    Save(ptr);  // 先儲存至 storage
  }             // ptr 在此作用域結束並被銷毀 (strong_count == 0)

  // 呼叫 FlushStorage 等待背景銷毀任務完全落盤
  FlushStorage();

  // 驗證儲存體中的藍圖已被非同步刪除
  auto read_stream = storage->OpenReadStream(id);
  assert(read_stream == nullptr);

  std::cout << "  -> 非同步物件清理與 FlushStorage 運作正常！" << std::endl;
}

void test_async_result_converting_move()
{
  std::cout << "[測試 6] AsyncResult 多型轉換移動與 Move-Only 語意測試..." << std::endl;

  // 靜態驗證 Move-Only 特性
  static_assert(!std::is_copy_constructible_v<AsyncResult<AsyncDerivedEntity>>, "AsyncResult must be move-only");
  static_assert(!std::is_copy_assignable_v<AsyncResult<AsyncDerivedEntity>>, "AsyncResult must be move-only");
  static_assert(std::is_move_constructible_v<AsyncResult<AsyncDerivedEntity>>, "AsyncResult must be movable");
  static_assert(std::is_move_assignable_v<AsyncResult<AsyncDerivedEntity>>, "AsyncResult must be movable");

  auto derived_ptr = CreateObject<AsyncDerivedEntity>(123, "DerivedAsync", 99.9);
  HandleID id = derived_ptr.GetTargetID();

  auto fut = SaveAsync(derived_ptr);
  AsyncResult<AsyncDerivedEntity> derived_res = fut.get();

  assert(derived_res);
  assert(derived_res.id == id);
  assert(derived_res.ptr->m_extra == 99.9);

  // 1. 測試 AsyncResult<Derived> -> AsyncResult<Base> 轉換移動建構
  AsyncResult<AsyncTestEntity> base_res(std::move(derived_res));
  assert(base_res);
  assert(base_res.id == id);
  assert(base_res.ptr.GetTargetID() == id);
  assert(derived_res.ptr.GetTargetID() == 0);  // 來源 ptr 已被移出清空

  // 2. 測試 AsyncResult<Derived> -> AsyncResult<Base> 轉換移動賦值
  auto derived_ptr2 = CreateObject<AsyncDerivedEntity>(456, "DerivedAsync2", 88.8);
  HandleID id2 = derived_ptr2.GetTargetID();
  auto fut2 = SaveAsync(derived_ptr2);
  AsyncResult<AsyncDerivedEntity> derived_res2 = fut2.get();

  base_res = std::move(derived_res2);
  assert(base_res);
  assert(base_res.id == id2);
  assert(derived_res2.ptr.GetTargetID() == 0);

  // 3. 測試 AsyncResult<Base> -> AsyncResult<void> 型別抹除轉換
  AsyncResult<void> void_res(std::move(base_res));
  assert(void_res);
  assert(void_res.id == id2);

  std::cout << "  -> AsyncResult 多型轉換移動與 Move-Only 驗證通過！" << std::endl;
}

int main()
{
  try
  {
    std::cout << "=== 開始執行 OuroKore 核心非同步與批次並發測試 ===" << std::endl;

    auto storage = std::make_shared<InMemoryStorage>();
    ork::Init(storage);

    test_single_async_save_load();
    test_single_async_dehydrate_rehydrate();
    test_failure_handling();
    test_parallel_batch_operations();
    test_async_destruction_and_flush();
    test_async_result_converting_move();

    ork::Shutdown();

    std::cout << "=== OuroKore 核心非同步與批次所有測試全部通過！ ===" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "測試發生例外: " << e.what() << std::endl;
    return 1;
  }
}
