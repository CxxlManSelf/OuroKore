#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ourokore/base/DynamicLibrary.hpp"
#include "ourokore/base/HeapTracker.hpp"
#include "ourokore/base/PluginHeap.hpp"
#include "ourokore/base/TrackedNewDelete.hpp"
#include "ourokore/base/heap_api.h"

// 測試用多型自訂結構
class TestEntity
{
public:
  TestEntity() :
      m_name("Default"),
      m_value(0)
  {
    s_alive_count++;
  }

  TestEntity(std::string name, int value) :
      m_name(std::move(name)),
      m_value(value)
  {
    s_alive_count++;
  }

  virtual ~TestEntity()
  {
    s_alive_count--;
  }

  const std::string &GetName() const
  {
    return m_name;
  }
  int GetValue() const
  {
    return m_value;
  }

  static inline int s_alive_count{0};

private:
  std::string m_name;
  int m_value{0};
};

// 衍生類別測試解構式虛擬調用
class DerivedEntity : public TestEntity
{
public:
  DerivedEntity(std::string name, int value, double extra) :
      TestEntity(std::move(name), value),
      m_extra(extra)
  {
  }

  ~DerivedEntity() override = default;

  double GetExtra() const
  {
    return m_extra;
  }

private:
  double m_extra{0.0};
};

int main()
{
  std::cout << "============================================================" << std::endl;
  std::cout << "🚀 開始執行 OuroKore Heap 追蹤與清空檢驗機制單元測試..." << std::endl;
  std::cout << "============================================================" << std::endl;

  // --------------------------------------------------------------------------
  // [Test 1] 獨立 HeapTracker 基礎配置、釋放與統計驗證
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 1] 驗證獨立 HeapTracker 基礎配置與統計..." << std::endl;
    ork::HeapTracker tracker;
    assert(tracker.is_clean());
    auto stats = tracker.get_stats();
    assert(stats.active_allocations == 0);
    assert(stats.active_bytes == 0);

    void *p1 = tracker.allocate(128, "mock_file.cpp", 42);
    assert(p1 != nullptr);
    assert(!tracker.is_clean());
    assert(tracker.get_stats().active_allocations == 1);
    assert(tracker.get_stats().active_bytes == 128);

    void *p2 = tracker.allocate(256, "mock_file.cpp", 55);
    assert(p2 != nullptr);
    assert(tracker.get_stats().active_allocations == 2);
    assert(tracker.get_stats().active_bytes == 384);

    tracker.deallocate(p1);
    assert(tracker.get_stats().active_allocations == 1);
    assert(tracker.get_stats().active_bytes == 256);

    tracker.deallocate(p2);
    assert(tracker.is_clean());
    assert(tracker.get_stats().active_allocations == 0);
    assert(tracker.get_stats().active_bytes == 0);
    assert(tracker.get_stats().total_allocations == 2);
    assert(tracker.get_stats().total_deallocations == 2);
    std::cout << "  ✅ 基礎配置與釋放統計通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 2] 對齊配置與釋放驗證 (C++17 Aligned Alloc)
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 2] 驗證對齊記憶體配置與釋放 (64-byte 對齊)..." << std::endl;
    ork::HeapTracker tracker;
    constexpr size_t alignment = 64;
    void *aligned_ptr = tracker.allocate_aligned(512, alignment, "aligned_test.cpp", 100);
    assert(aligned_ptr != nullptr);
    assert((reinterpret_cast<uintptr_t>(aligned_ptr) % alignment) == 0);

    tracker.deallocate_aligned(aligned_ptr, alignment);
    assert(tracker.is_clean());
    std::cout << "  ✅ 對齊配置與釋放通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 3] 顯式 ORK_NEW 與 ORK_DELETE 巨集、行號追蹤與解構驗證
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 3] 驗證顯式 ORK_NEW 與 ORK_DELETE 巨集及行號精確定位..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();
    assert(tracker.is_clean());

    int initial_line = __LINE__ + 1;
    TestEntity *entity = ORK_NEW(TestEntity, "Hero", 100);
    assert(entity != nullptr);
    assert(entity->GetName() == "Hero");
    assert(entity->GetValue() == 100);
    assert(TestEntity::s_alive_count == 1);
    assert(!tracker.is_clean());

    // 檢查洩漏清單中是否正確記錄檔案與行號
    auto leaks = tracker.get_leaks();
    assert(leaks.size() == 1);
    assert(leaks[0].line == initial_line);
    assert(std::string(leaks[0].file).find("base_heap_tests.cpp") != std::string::npos);

    // 驗證衍生類別與虛擬解構
    TestEntity *derived = ORK_NEW(DerivedEntity, "Boss", 999, 3.14);
    assert(TestEntity::s_alive_count == 2);
    assert(tracker.get_stats().active_allocations == 2);

    // 依序刪除
    ORK_DELETE(derived);
    assert(TestEntity::s_alive_count == 1);
    assert(tracker.get_stats().active_allocations == 1);

    ORK_DELETE(entity);
    assert(TestEntity::s_alive_count == 0);
    assert(tracker.is_clean());
    std::cout << "  ✅ ORK_NEW 與 ORK_DELETE 建構/解構與精確行號記錄通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 4] 陣列配置 ORK_NEW_ARRAY 與 ORK_DELETE_ARRAY 驗證
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 4] 驗證陣列配置 ORK_NEW_ARRAY 與 ORK_DELETE_ARRAY..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();

    constexpr size_t count = 5;
    TestEntity *arr = ORK_NEW_ARRAY(TestEntity, count);
    assert(arr != nullptr);
    assert(TestEntity::s_alive_count == count);
    assert(!tracker.is_clean());

    ORK_DELETE_ARRAY(arr, count);
    assert(TestEntity::s_alive_count == 0);
    assert(tracker.is_clean());
    std::cout << "  ✅ 陣列配置與析構通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 5] STL 容器整合：TrackedAllocator 追蹤驗證
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 5] 驗證 STL 容器整合 (TrackedAllocator)..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();

    {
      std::vector<int, ork::TrackedAllocator<int>> vec;
      for (int i = 0; i < 1000; ++i)
      {
        vec.push_back(i);
      }
      assert(!tracker.is_clean());
      assert(tracker.get_stats().active_bytes > 0);
    }

    // 離開作用域後 vector 記憶體釋放，HeapTracker 應回歸清空
    assert(tracker.is_clean());
    std::cout << "  ✅ TrackedAllocator 與 STL vector 追蹤通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 6] 記憶體洩漏診斷報告 (dump_leaks_to_string) 與 assert_clean 斷言測試
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 6] 驗證外掛結束前洩漏報告輸出與未清空偵測..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();

    void *leak1 = tracker.allocate(64, "PluginCore.cpp", 123);
    void *leak2 = tracker.allocate(1024, "PluginNetwork.cpp", 456);

    assert(!tracker.is_clean());
    std::string report = tracker.dump_leaks_to_string("TestPluginModule");
    assert(report.find("PluginCore.cpp:123") != std::string::npos);
    assert(report.find("PluginNetwork.cpp:456") != std::string::npos);
    assert(report.find("1024 bytes") != std::string::npos);
    std::cout << "  -> 模擬外掛洩漏產生的報告預覽:\n" << report;

    // 驗證未清空時 assert_clean 會拋出例外
    bool threw_exception = false;
    try
    {
      tracker.assert_clean("TestPluginModule");
    }
    catch (const std::runtime_error &e)
    {
      threw_exception = true;
    }
    assert(threw_exception);

    // 清理洩漏
    tracker.deallocate(leak1);
    tracker.deallocate(leak2);
    assert(tracker.is_clean());

    // 清空後 assert_clean 正常通過不拋異常
    tracker.assert_clean("TestPluginModule");
    std::cout << "  ✅ 洩漏診斷報告與斷言防護通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 7] RAII 守衛 PluginHeapGuard 測試
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 7] 驗證 PluginHeapGuard 守衛..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();

    {
      ork::PluginHeapGuard guard("GuardScopeTest", false);
      void *p = tracker.allocate(32, "guard_test.cpp", 88);
      tracker.deallocate(p);
    }
    assert(tracker.is_clean());
    std::cout << "  ✅ PluginHeapGuard 驗證通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 8] 多執行緒並發配置與釋放壓力測試 (Thread Safety)
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 8] 驗證多執行緒並發配置與釋放執行緒安全性..." << std::endl;
    auto &tracker = ork::HeapTracker::get_default_tracker();
    tracker.reset();

    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t)
    {
      threads.emplace_back(
          [&tracker]()
          {
            for (int i = 0; i < ops_per_thread; ++i)
            {
              void *p = tracker.allocate(32 + (i % 64), "multithread.cpp", 10);
              std::this_thread::yield();
              tracker.deallocate(p);
            }
          }
      );
    }

    for (auto &t : threads)
    {
      t.join();
    }

    assert(tracker.is_clean());
    assert(tracker.get_stats().total_allocations == num_threads * ops_per_thread);
    assert(tracker.get_stats().total_deallocations == num_threads * ops_per_thread);
    std::cout << "  ✅ 多執行緒並發壓力測試通過！累計配置/釋放: " << tracker.get_stats().total_allocations << " 次"
              << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 9] 純 C ABI (heap_api.h) 介面跨語言 FFI 驗證
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 9] 驗證純 C ABI 介面 (heap_api.h)..." << std::endl;
    ork_heap_reset();
    assert(ork_heap_is_clean() == 1);
    assert(ork_heap_get_active_allocations() == 0);
    assert(ork_heap_get_active_bytes() == 0);

    void *c_ptr = ork_heap_allocate(200, "c_test.c", 77);
    assert(c_ptr != nullptr);
    assert(ork_heap_is_clean() == 0);
    assert(ork_heap_get_active_allocations() == 1);
    assert(ork_heap_get_active_bytes() == 200);

    char dump_buf[2048] = {0};
    int32_t len = ork_heap_dump_leaks(dump_buf, sizeof(dump_buf));
    assert(len > 0);
    assert(std::string(dump_buf).find("c_test.c:77") != std::string::npos);

    // 未清空時斷言傳回 -1
    assert(ork_heap_assert_clean("C_Context") != 0);

    ork_heap_deallocate(c_ptr);
    assert(ork_heap_is_clean() == 1);
    assert(ork_heap_get_active_allocations() == 0);
    assert(ork_heap_get_active_bytes() == 0);
    assert(ork_heap_assert_clean("C_Context") == 0);

    std::cout << "  ✅ 純 C ABI 介面完整通過！" << std::endl;
  }

  // --------------------------------------------------------------------------
  // [Test 10] 動態外掛模組 (MODULE DLL) 全域 operator new/delete 攔截與結束前清空檢驗
  // --------------------------------------------------------------------------
  {
    std::cout << "[Test 10] 驗證外掛 MODULE 動態庫全域 operator new/delete 攔截與退出前 Heap 檢驗..." << std::endl;

    std::filesystem::path plugin_name_d = ork::DynamicLibrary::format_filename("test_tracked_plugin_d");
    std::filesystem::path plugin_name = ork::DynamicLibrary::format_filename("test_tracked_plugin");
    std::filesystem::path search_paths[] = {
        std::filesystem::current_path() / "build" / "bin" / plugin_name_d,
        std::filesystem::current_path() / "build" / "bin" / plugin_name,
        std::filesystem::current_path() / "bin" / plugin_name_d,
        std::filesystem::current_path() / "bin" / plugin_name,
        plugin_name_d,
        plugin_name
    };

    std::filesystem::path plugin_path;
    for (const auto &p : search_paths)
    {
      if (std::filesystem::exists(p))
      {
        plugin_path = p;
        break;
      }
    }
    if (plugin_path.empty())
    {
      plugin_path = plugin_name;
    }

    std::cout << "  -> 載入外掛模組: " << plugin_path << std::endl;
    auto plugin_lib = ork::DynamicLibrary::load(plugin_path);
    assert(plugin_lib.is_loaded());

    using AllocDataFn = void *(*)(int, const char *, double);
    using FreeDataFn = void (*)(void *);
    using TriggerLeakFn = void (*)();
    using ResolveLeakFn = void (*)();
    using CheckCleanFn = int32_t (*)();
    using GetActiveFn = uint64_t (*)();
    using DumpLeaksFn = int32_t (*)(char *, size_t);
    using ShutdownCheckFn = int32_t (*)();

    auto alloc_fn = plugin_lib.get_symbol<AllocDataFn>("PluginAllocData");
    auto free_fn = plugin_lib.get_symbol<FreeDataFn>("PluginFreeData");
    auto trigger_leak_fn = plugin_lib.get_symbol<TriggerLeakFn>("PluginTriggerLeak");
    auto resolve_leak_fn = plugin_lib.get_symbol<ResolveLeakFn>("PluginResolveLeak");
    auto check_clean_fn = plugin_lib.get_symbol<CheckCleanFn>("PluginCheckHeapIsClean");
    auto get_active_fn = plugin_lib.get_symbol<GetActiveFn>("PluginGetActiveAllocations");
    auto dump_leaks_fn = plugin_lib.get_symbol<DumpLeaksFn>("PluginDumpLeaks");
    auto shutdown_check_fn = plugin_lib.get_symbol<ShutdownCheckFn>("PluginShutdownCheck");

    assert(
        alloc_fn && free_fn && trigger_leak_fn && resolve_leak_fn && check_clean_fn && get_active_fn && dump_leaks_fn &&
        shutdown_check_fn
    );

    // 初始狀態應為清空
    assert(check_clean_fn() == 1);
    assert(get_active_fn() == 0);
    assert(shutdown_check_fn() == 0);

    // 外掛內部 new 物件（包含 std::string STL 容器配置）
    void *data1 = alloc_fn(101, "DragonPluginPayload", 99.5);
    assert(data1 != nullptr);
    assert(check_clean_fn() == 0);  // 存活配置 > 0，未清空
    assert(get_active_fn() >= 1);

    // 釋放該物件
    free_fn(data1);
    assert(check_clean_fn() == 1);
    assert(get_active_fn() == 0);
    assert(shutdown_check_fn() == 0);

    // 模擬外掛內部發生記憶體洩漏
    trigger_leak_fn();
    assert(check_clean_fn() == 0);
    assert(get_active_fn() >= 1);

    // 外掛準備結束/卸載，執行 shutdown 檢驗
    int32_t shutdown_result = shutdown_check_fn();
    assert(shutdown_result == -1);  // 成功攔截並回報未清空！

    char leak_buf[2048] = {0};
    dump_leaks_fn(leak_buf, sizeof(leak_buf));
    std::cout << "  -> 外掛退出前成功攔截洩漏，診斷資訊:\n" << leak_buf;
    assert(std::string(leak_buf).find("TrackedPluginDLL") != std::string::npos);

    // 修復/清理該洩漏
    resolve_leak_fn();
    assert(check_clean_fn() == 1);
    assert(get_active_fn() == 0);

    // 再次執行外掛結束前檢驗：清空通過！
    assert(shutdown_check_fn() == 0);
    std::cout << "  ✅ 外掛 MODULE 動態庫全域 operator new/delete 攔截與退出前檢驗全流程通過！" << std::endl;
  }

  // =========================================================================
  // 測試 12: 驗證 PluginHeapGuard 抗暫時性字串懸空 (Anti-Dangling Temporary String)
  // =========================================================================
  {
    std::cout << "\n[Test 12] 驗證 PluginHeapGuard 抗暫時性字串懸空與零堆配置..." << std::endl;
    std::string mod_name = "AudioEngine";
    {
      // 傳入暫時性字串（陳述式結束後即刻銷毀）
      ork::PluginHeapGuard guard("Test_" + mod_name + "_V2");
      assert(guard.get_context_name() == "Test_AudioEngine_V2");
    }
    // 驗證超過緩衝區長度的超長字串截斷保護
    std::string long_name(200, 'X');
    {
      ork::PluginHeapGuard long_guard(long_name);
      assert(long_guard.get_context_name().size() == 127);
      assert(long_guard.get_context_name().front() == 'X');
    }
    std::cout << "  ✅ PluginHeapGuard 暫時字串安全持有與長度截斷防禦驗證通過！" << std::endl;
  }

  std::cout << "============================================================" << std::endl;
  std::cout << "🎉 所有 Heap 追蹤與清空檢驗測試 100% 全部通過！" << std::endl;
  std::cout << "============================================================" << std::endl;

  return 0;
}
