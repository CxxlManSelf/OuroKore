#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

#include "ourokore/base/ThreadPool.hpp"

using namespace ork::base;

void test_fixed_thread_pool()
{
  std::cout << "[測試] FixedThreadPool 基本功能與 Future 運算..." << std::endl;
  FixedThreadPool pool(4);
  assert(pool.get_worker_count() == 4);
  assert(pool.is_running());

  // 1. Future 提交與結果回收
  auto f1 = pool.submit([]() { return 42; });
  auto f2 = pool.submit([](int a, int b) { return a + b; }, 10, 20);

  assert(f1.get() == 42);
  assert(f2.get() == 30);

  // 2. 例外捕捉與傳遞
  auto f_err = pool.submit([]() -> int { throw std::runtime_error("ThreadPool exception test"); });

  bool caught = false;
  try
  {
    f_err.get();
  }
  catch (const std::runtime_error &e)
  {
    caught = true;
  }
  assert(caught);

  // 3. 併發大量任務求和
  constexpr int N = 1000;
  std::vector<std::future<int>> futures;
  futures.reserve(N);

  for (int i = 0; i < N; ++i)
  {
    futures.push_back(pool.submit([i]() { return i; }));
  }

  long long sum = 0;
  for (auto &f : futures)
  {
    sum += f.get();
  }
  long long expected_sum = (static_cast<long long>(N - 1) * N) / 2;
  assert(sum == expected_sum);

  // 4. Detached 任務與 wait_idle
  std::atomic<int> counter{0};
  for (int i = 0; i < 50; ++i)
  {
    pool.submit_detached(
        [&counter]()
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          counter.fetch_add(1);
        }
    );
  }

  pool.wait_idle();
  assert(counter.load() == 50);

  // 5. 停止後拒絕新任務
  pool.stop();
  assert(!pool.is_running());

  bool submit_rejected = false;
  try
  {
    pool.submit([]() {});
  }
  catch (const std::exception &)
  {
    submit_rejected = true;
  }
  assert(submit_rejected);
}

void test_dynamic_thread_pool()
{
  std::cout << "[測試] DynamicThreadPool 動態伸縮與空閒縮容..." << std::endl;

  // 核心 2 個線程，最大 6 個線程，空閒逾時 200ms
  DynamicThreadPool dynamic_pool(2, 6, std::chrono::milliseconds(200));

  assert(dynamic_pool.get_min_threads() == 2);
  assert(dynamic_pool.get_max_threads() == 6);
  assert(dynamic_pool.get_current_worker_count() == 2);

  // 1. 提交 6 個並發阻塞任務，觀察動態擴展
  std::atomic<bool> release_gate{false};
  std::atomic<int> started_count{0};
  std::vector<std::future<void>> futures;

  for (int i = 0; i < 6; ++i)
  {
    futures.push_back(dynamic_pool.submit(
        [&]()
        {
          started_count.fetch_add(1);
          while (!release_gate.load())
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }
    ));
  }

  // 等待所有 6 個任務皆已啟動運行
  while (started_count.load() < 6)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // 驗證 Worker 數量已動態擴展至上限 6
  size_t expanded_workers = dynamic_pool.get_current_worker_count();
  std::cout << "  -> 高負載時擴展後的 Worker 數: " << expanded_workers << std::endl;
  assert(expanded_workers == 6);

  // 釋放閘門讓任務結束
  release_gate.store(true);
  for (auto &f : futures)
  {
    f.get();
  }

  // 等待空閒逾時（> 200ms），驗證自動縮容回核心線程數（2）
  size_t shrunk_workers = 0;
  for (int retry = 0; retry < 20; ++retry)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    shrunk_workers = dynamic_pool.get_current_worker_count();
    if (shrunk_workers == 2)
    {
      break;
    }
  }
  std::cout << "  -> 空閒縮容後的 Worker 數: " << shrunk_workers << std::endl;
  assert(shrunk_workers == 2);

  // 2. 停止動態池
  dynamic_pool.stop();
  assert(!dynamic_pool.is_running());
  assert(dynamic_pool.get_current_worker_count() == 0);
}

int main()
{
  try
  {
    std::cout << "=== 開始執行 ThreadPool 單元測試 ===" << std::endl;
    test_fixed_thread_pool();
    test_dynamic_thread_pool();
    std::cout << "=== ThreadPool 所有測試全部通過！ ===" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "測試發生例外: " << e.what() << std::endl;
    return 1;
  }
}
