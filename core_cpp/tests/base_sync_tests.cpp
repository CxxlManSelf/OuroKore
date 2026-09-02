#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "ourokore/base/Semaphore.hpp"

using namespace ork::base;

void test_semaphore_basic()
{
  std::cout << "[測試] Semaphore 基本計數與獲取/釋放..." << std::endl;
  Semaphore sem(0);
  assert(sem.available() == 0);
  assert(!sem.try_acquire());

  sem.release(2);
  assert(sem.available() == 2);

  assert(sem.try_acquire());
  assert(sem.available() == 1);

  sem.acquire();
  assert(sem.available() == 0);
  assert(!sem.try_acquire());
}

void test_semaphore_timeout()
{
  std::cout << "[測試] Semaphore 逾時獲取機制..." << std::endl;
  Semaphore sem(0);

  auto start = std::chrono::steady_clock::now();
  bool acquired = sem.try_acquire_for(std::chrono::milliseconds(50));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  assert(!acquired);
  assert(elapsed.count() >= 40);

  std::thread t([&sem]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    sem.release();
  });

  acquired = sem.try_acquire_for(std::chrono::milliseconds(200));
  assert(acquired);

  t.join();
}

void test_event_autoreset()
{
  std::cout << "[測試] Event (AutoReset 模式)..." << std::endl;
  Event ev(EventResetMode::AutoReset, false);

  assert(!ev.is_set());
  assert(!ev.wait_for(std::chrono::milliseconds(30)));

  std::atomic<int> woken_count{0};
  std::thread t1([&]() {
    ev.wait();
    woken_count.fetch_add(1);
  });

  std::thread t2([&]() {
    if (ev.wait_for(std::chrono::milliseconds(50)))
    {
      woken_count.fetch_add(1);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ev.set();  // AutoReset: 應該只有一個線程被喚醒

  t1.join();
  t2.join();

  assert(woken_count.load() == 1);
  assert(!ev.is_set());
}

void test_event_manualreset()
{
  std::cout << "[測試] Event (ManualReset 模式廣播與重設)..." << std::endl;
  Event ev(EventResetMode::ManualReset, false);

  std::atomic<int> count{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
  {
    threads.emplace_back([&]() {
      ev.wait();
      count.fetch_add(1);
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(count.load() == 0);

  ev.set();
  for (auto &t : threads)
  {
    t.join();
  }

  assert(count.load() == 4);
  assert(ev.is_set());

  ev.reset();
  assert(!ev.is_set());
  assert(!ev.wait_for(std::chrono::milliseconds(30)));
}

int main()
{
  try
  {
    std::cout << "=== 開始執行 Semaphore 與 Event 同步原語單元測試 ===" << std::endl;
    test_semaphore_basic();
    test_semaphore_timeout();
    test_event_autoreset();
    test_event_manualreset();
    std::cout << "=== Semaphore 與 Event 所有測試全部通過！ ===" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "測試發生例外: " << e.what() << std::endl;
    return 1;
  }
}
