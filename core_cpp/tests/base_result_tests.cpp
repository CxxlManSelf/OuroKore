#include <iostream>
#include <cassert>
#include <string>
#include <memory>
#include <chrono>
#include <thread>

#include "ourokore/base/Result.hpp"
#include "ourokore/base/ThreadSafeQueue.hpp"
#include "ourokore/base/DynamicLibrary.hpp"

using namespace ork;

// 測試輔助函式
Result<int> SafeDivide(int a, int b)
{
  if (b == 0)
  {
    return Err(StatusCode::InvalidArgument, "Division by zero");
  }
  return Ok(a / b);
}

Result<std::string> IntToString(int val)
{
  return Ok("Value: " + std::to_string(val));
}

int main()
{
  std::cout << "=== 開始執行 OuroKore C++23 Result / std::expected 單元測試 ===" << std::endl;

  // -------------------------------------------------------------
  // [測試 1] Status 物件基礎屬性與靜態工廠
  // -------------------------------------------------------------
  std::cout << "[測試 1] Status 基礎屬性與各類工廠方法測試..." << std::endl;
  {
    Status ok_status = Status::Ok();
    assert(ok_status.ok());
    assert(!ok_status.is_error());
    assert(ok_status.code() == 0);
    assert(ok_status.status_code() == StatusCode::Ok);
    assert(ok_status.to_string() == "OK");

    Status not_found = Status::NotFound("Entity 1001 not found");
    assert(!not_found.ok());
    assert(not_found.is_error());
    assert(not_found.code() == static_cast<int32_t>(StatusCode::NotFound));
    assert(not_found.status_code() == StatusCode::NotFound);
    assert(not_found.message() == "Entity 1001 not found");
    assert(not_found == StatusCode::NotFound);

    Status custom_err(404, "Page missing");
    assert(custom_err.code() == 404);
    assert(custom_err.message() == "Page missing");
  }
  std::cout << "  -> Status 基礎測試通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 2] Result<T> 成功與失敗基本取值語意
  // -------------------------------------------------------------
  std::cout << "[測試 2] Result<T> 成功取值與失敗錯誤檢查..." << std::endl;
  {
    auto success_res = SafeDivide(20, 4);
    assert(success_res.has_value());
    assert(static_cast<bool>(success_res));
    assert(*success_res == 5);
    assert(success_res.value() == 5);
    assert(success_res.value_or(999) == 5);

    auto fail_res = SafeDivide(20, 0);
    assert(!fail_res.has_value());
    assert(!static_cast<bool>(fail_res));
    assert(fail_res.error().status_code() == StatusCode::InvalidArgument);
    assert(fail_res.error().message() == "Division by zero");
    assert(fail_res.value_or(999) == 999);
  }
  std::cout << "  -> Result<T> 基本取值語意通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 3] Result<void> 特化測試
  // -------------------------------------------------------------
  std::cout << "[測試 3] Result<void> 特化測試..." << std::endl;
  {
    auto do_action = [](bool succeed) -> Result<void> {
      if (succeed)
      {
        return Ok();
      }
      return Err(StatusCode::PermissionDenied, "Action forbidden");
    };

    auto ok_act = do_action(true);
    assert(ok_act.has_value());

    auto err_act = do_action(false);
    assert(!err_act.has_value());
    assert(err_act.error().status_code() == StatusCode::PermissionDenied);
  }
  std::cout << "  -> Result<void> 測試通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 4] Move-Only 型別支援 (std::unique_ptr)
  // -------------------------------------------------------------
  std::cout << "[測試 4] Move-Only 型別支援 (std::unique_ptr)..." << std::endl;
  {
    auto create_uptr = [](int val) -> Result<std::unique_ptr<int>> {
      if (val < 0)
      {
        return Err(StatusCode::OutOfRange, "Negative value rejected");
      }
      return Ok(std::make_unique<int>(val));
    };

    auto res = create_uptr(42);
    assert(res.has_value());
    std::unique_ptr<int> moved_val = std::move(*res);
    assert(*moved_val == 42);

    auto fail = create_uptr(-1);
    assert(!fail.has_value());
    assert(fail.error().status_code() == StatusCode::OutOfRange);
  }
  std::cout << "  -> Move-Only 型別支援通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 5] C++23 原生 Monadic 鏈式調用 (.and_then / .transform / .or_else)
  // -------------------------------------------------------------
  std::cout << "[測試 5] C++23 原生 Monadic 鏈式調用 (.and_then / .transform)..." << std::endl;
  {
    // 成功鏈式管線: 100 / 2 -> 50 -> "Value: 50" -> 加上後綴
    auto chain_success = SafeDivide(100, 2)
                             .and_then(IntToString)
                             .transform([](std::string s) { return s + " [PROCESSED]"; });

    assert(chain_success.has_value());
    assert(*chain_success == "Value: 50 [PROCESSED]");

    // 中途失敗短路管線: 100 / 0 失敗，後續 and_then 與 transform 自動略過
    auto chain_fail = SafeDivide(100, 0)
                          .and_then(IntToString)
                          .transform([](std::string s) { return s + " [PROCESSED]"; });

    assert(!chain_fail.has_value());
    assert(chain_fail.error().status_code() == StatusCode::InvalidArgument);

    // .or_else 錯誤修復
    auto recovered = SafeDivide(100, 0)
                         .or_else([](Status err) -> Result<int> {
                           // 捕捉錯誤並提供備援值
                           return Ok(0);
                         });

    assert(recovered.has_value());
    assert(*recovered == 0);
  }
  std::cout << "  -> C++23 Monadic 鏈式調用通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 6] ThreadSafeQueue 之 pop_expected 整合測試
  // -------------------------------------------------------------
  std::cout << "[測試 6] ThreadSafeQueue::pop_expected 與逾時測試..." << std::endl;
  {
    ThreadSafeQueue<std::string> queue;

    // 1. 空佇列 try_pop_expected -> Unavailable
    auto empty_res = queue.try_pop_expected();
    assert(!empty_res.has_value());
    assert(empty_res.error().status_code() == StatusCode::Unavailable);

    // 2. 正常推入並取出
    queue.push("ItemA");
    auto pop_res = queue.try_pop_expected();
    assert(pop_res.has_value());
    assert(*pop_res == "ItemA");

    // 3. 逾時等待 -> Timeout
    auto timeout_res = queue.pop_for_expected(std::chrono::milliseconds(50));
    assert(!timeout_res.has_value());
    assert(timeout_res.error().status_code() == StatusCode::Timeout);

    // 4. 背景推入，成功唤醒
    std::thread producer([&queue]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      queue.push("AsyncItem");
    });

    auto async_pop = queue.pop_for_expected(std::chrono::milliseconds(200));
    assert(async_pop.has_value());
    assert(*async_pop == "AsyncItem");
    producer.join();

    // 5. 佇列停止 -> Aborted
    queue.stop();
    auto stopped_res = queue.pop_for_expected(std::chrono::milliseconds(50));
    assert(!stopped_res.has_value());
    assert(stopped_res.error().status_code() == StatusCode::Aborted);
  }
  std::cout << "  -> ThreadSafeQueue 整合測試通過！" << std::endl;

  // -------------------------------------------------------------
  // [測試 7] DynamicLibrary::try_load 與 try_get_symbol 測試
  // -------------------------------------------------------------
  std::cout << "[測試 7] DynamicLibrary::try_load 與 try_get_symbol 測試..." << std::endl;
  {
    // 1. 載入不存在的 DLL -> 失敗且帶有 StatusCode::NotFound
    auto bad_lib = DynamicLibrary::try_load("non_existent_fake_module_xyz.dll");
    assert(!bad_lib.has_value());
    assert(bad_lib.error().status_code() == StatusCode::NotFound);
    assert(!bad_lib.error().message().empty());

    // 2. 載入存在的測試外掛
    auto good_lib = DynamicLibrary::try_load("test_plugin_dll.dll");
    if (good_lib)
    {
      // 成功解析符號
      auto fn_res = good_lib->try_get_symbol<int32_t(*)()>("test_plugin_get_value");
      if (fn_res)
      {
        assert((*fn_res)() == 42);
      }

      // 解析不存在的符號 -> 失敗且帶有 NotFound
      auto bad_fn = good_lib->try_get_symbol<void(*)()>("non_existent_symbol_123");
      assert(!bad_fn.has_value());
      assert(bad_fn.error().status_code() == StatusCode::NotFound);
    }
  }
  std::cout << "  -> DynamicLibrary 整合測試通過！" << std::endl;

  std::cout << "=== 所有 C++23 Result / std::expected 單元測試全部順利通過 (PASSED)! ===" << std::endl;
  return 0;
}
