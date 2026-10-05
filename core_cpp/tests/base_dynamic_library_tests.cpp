#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "ourokore/base/DynamicLibrary.hpp"

// 定義與 test_plugin_dll 相容的抽象介面
class ITestPlugin
{
public:
  virtual ~ITestPlugin() = default;
  virtual int Multiply(int a, int b) = 0;
  virtual const char *GetName() const = 0;
};

using AddNumbersFn = int (*)(int, int);
using CreatePluginFn = ITestPlugin *(*)();
using DestroyPluginFn = void (*)(ITestPlugin *);
using GetCountFn = int (*)();

int main(int argc, char *argv[])
{
  std::cout << "============================================================" << std::endl;
  std::cout << "🚀 開始執行 DynamicLibrary 跨平台動態庫載入器單元測試..." << std::endl;
  std::cout << "============================================================" << std::endl;

  // 1. 測試：格式化檔名 (format_filename)
  {
    std::cout << "[Test 1] 驗證跨平台檔名格式化..." << std::endl;
    auto formatted = ork::DynamicLibrary::format_filename("my_plugin");
#if defined(_WIN32)
    assert(formatted == "my_plugin.dll");
#elif defined(__APPLE__)
    assert(formatted == "libmy_plugin.dylib");
#else
    assert(formatted == "libmy_plugin.so");
#endif
    std::cout << "  ✅ format_filename 測試通過: " << formatted << std::endl;
  }

  // 2. 測試：載入無效路徑與錯誤訊息回報
  {
    std::cout << "[Test 2] 驗證無效路徑載入防禦與錯誤處理..." << std::endl;
    auto invalid_lib = ork::DynamicLibrary::load("non_existent_library_123456.dll");
    assert(!invalid_lib.is_loaded());
    assert(!invalid_lib);
    assert(!invalid_lib.get_last_error().empty());
    assert(invalid_lib.get_symbol<void (*)()>("some_symbol") == nullptr);
    assert(invalid_lib.use_count() == 0);
    std::cout << "  ✅ 無效載入安全失敗，捕獲錯誤訊息: " << invalid_lib.get_last_error() << std::endl;
  }

  // 尋找測試動態庫路徑（優先搜尋 Debug 後綴 _d，確保加載最新編譯輸出）
  std::filesystem::path plugin_filename_d = ork::DynamicLibrary::format_filename("test_plugin_dll_d");
  std::filesystem::path plugin_filename = ork::DynamicLibrary::format_filename("test_plugin_dll");
  std::filesystem::path exe_dir;
  if (argc > 0 && argv[0])
  {
    exe_dir = std::filesystem::path(argv[0]).parent_path();
  }

  std::filesystem::path search_paths[] = {
      exe_dir / plugin_filename_d,
      exe_dir / plugin_filename,
      exe_dir / "../lib" / plugin_filename_d,
      exe_dir / "../lib" / plugin_filename,
      std::filesystem::current_path() / "build" / "bin" / plugin_filename_d,
      std::filesystem::current_path() / "build" / "bin" / plugin_filename,
      std::filesystem::current_path() / "build" / "lib" / plugin_filename_d,
      std::filesystem::current_path() / "build" / "lib" / plugin_filename,
      std::filesystem::current_path() / "bin" / plugin_filename_d,
      std::filesystem::current_path() / "bin" / plugin_filename,
      std::filesystem::current_path() / "lib" / plugin_filename_d,
      std::filesystem::current_path() / "lib" / plugin_filename,
      std::filesystem::path("build") / "bin" / plugin_filename_d,
      std::filesystem::path("build") / "bin" / plugin_filename,
      plugin_filename_d,
      plugin_filename};

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
    // 若相對路徑未找到，嘗試直接使用格式化後的檔名由 OS 搜尋
    plugin_path = plugin_filename;
  }

  std::cout << "📦 準備載入測試動態庫: " << plugin_path << std::endl;

  // 3. 測試：載入真實動態庫與解析純函式符號
  {
    std::cout << "[Test 3] 驗證符號解析與純函式調用..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    if (!lib.is_loaded())
    {
      std::cerr << "❌ 載入測試庫失敗: " << lib.get_last_error() << std::endl;
      return 1;
    }

    assert(lib.is_loaded());
    assert(lib.use_count() == 1);

    auto add_fn = lib.get_symbol<AddNumbersFn>("AddNumbers");
    assert(add_fn != nullptr);
    int sum = add_fn(40, 2);
    assert(sum == 42);
    std::cout << "  ✅ 純函式符號 AddNumbers(40, 2) 執行成功，結果: " << sum << std::endl;

    auto not_found = lib.get_symbol<void (*)()>("SymbolDoesNotExist");
    assert(not_found == nullptr);
    std::cout << "  ✅ 查詢不存在的符號安全傳回 nullptr" << std::endl;
  }

  // 4. 測試：核心驗證 —— 物件生命週期反向錨定（Life-Bound Retention）與自動卸載
  // 驗證重點：
  // 1. DynamicLibrary 不提供手動 unload()，由物件生命週期錨定自動卸載。
  // 2. load() 回傳值本身已持有引用；當應用端放棄該句柄（如離開局部作用域或 reset），
  //    且由其建立的綁定物件全部銷毀歸零時，動態庫才在底層安全卸載！
  {
    std::cout << "[Test 4] 核心驗證：物件生命週期反向錨定 (Life-Bound Retention)..." << std::endl;

    std::shared_ptr<ITestPlugin> managed_plugin;

    {
      // 在局部作用域建立 DynamicLibrary 實例（load 回傳值已持有引用計數 1）
      auto scoped_lib = ork::DynamicLibrary::load(plugin_path);
      assert(scoped_lib.is_loaded());
      assert(scoped_lib.use_count() == 1);

      // 檢查初始活體計數
      auto get_count = scoped_lib.get_symbol<GetCountFn>("GetActiveInstanceCount");
      assert(get_count != nullptr);
      assert(get_count() == 0);

      // 使用端自訂工廠函式（由使用端自行主導指標型別與 Deleter 閉包，自由度 100%）
      auto user_factory = [](const ork::DynamicLibrary &lib) -> std::shared_ptr<ITestPlugin> {
        auto create_fn = lib.get_symbol<CreatePluginFn>("CreateTestPlugin");
        auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyTestPlugin");
        if (!create_fn || !destroy_fn) return nullptr;

        ITestPlugin *raw = create_fn();
        return std::shared_ptr<ITestPlugin>(raw, [lib, destroy_fn](ITestPlugin *p) {
          if (p) destroy_fn(p);
        });
      };

      managed_plugin = user_factory(scoped_lib);

      assert(managed_plugin != nullptr);
      // scoped_lib 自身 1 + managed_plugin Deleter 閉包持有 1 = 2
      assert(scoped_lib.use_count() == 2);
      assert(get_count() == 1);

      // 驗證物件方法執行
      assert(managed_plugin->Multiply(6, 7) == 42);
      assert(std::string(managed_plugin->GetName()) == "TestPluginInstance");

      std::cout << "  -> 離開局部作用域，放棄 scoped_lib 句柄本體..." << std::endl;
      // 離開此處時，scoped_lib 變數解構，放棄持有！但 managed_plugin 依然存活！
    }

    std::cout << "  -> 此時 scoped_lib 句柄已解構，但受管物件仍持有動態庫存活權杖！" << std::endl;
    // 關鍵時刻：若 DLL 已被提前卸載，呼叫虛擬函式 Multiply 或 GetName 將立即引發 0xC0000005 崩潰！
    int result = managed_plugin->Multiply(9, 9);
    assert(result == 81);
    const char *name = managed_plugin->GetName();
    assert(std::string(name) == "TestPluginInstance");
    std::cout << "  ✅ [PASS] 成功調用虛擬函式 Multiply(9, 9) = " << result << "，代碼段完好！" << std::endl;
    std::cout << "  ✅ [PASS] 成功調用虛擬函式 GetName() = " << name << "，vtable 與字串常數完好！" << std::endl;

    // 銷毀受管物件
    std::cout << "  -> 銷毀最後一個受管物件 managed_plugin..." << std::endl;
    managed_plugin.reset();
    assert(managed_plugin == nullptr);
    std::cout << "  ✅ [PASS] 所有受管物件銷毀，Deleter 成功執行且底層動態庫安全自動卸載無崩潰！" << std::endl;
  }

  // 5. 測試：使用 bind_lifecycle 綁定手動獲取的指標，並透過 reset() 明確放棄 load() 初始句柄
  {
    std::cout << "[Test 5] 驗證 bind_lifecycle 綁定生命週期與 reset() 主動放棄句柄..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    assert(lib.use_count() == 1);

    auto create_fn = lib.get_symbol<CreatePluginFn>("CreateTestPlugin");
    auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyTestPlugin");
    assert(create_fn && destroy_fn);

    ITestPlugin *raw = create_fn();
    auto plugin2 = lib.bind_lifecycle(raw, destroy_fn);
    assert(plugin2 != nullptr);
    assert(lib.use_count() == 2);

    // 關鍵驗證：load() 的回傳值已經將其綁定，若不放棄則 DLL 不會卸載。
    // 此處呼叫 reset() 明確放棄 load() 回傳的句柄持有：
    lib.reset();
    assert(!lib.is_loaded());
    assert(lib.use_count() == 0);

    // 外部 lib 放棄後，plugin2 依然能正常調用成員函式
    assert(plugin2->Multiply(3, 4) == 12);
    plugin2.reset(); // 當產生的最後一個物件銷毀，DLL 自動卸載
    std::cout << "  ✅ bind_lifecycle 與 reset() 放棄初始句柄自動卸載測試通過！" << std::endl;
  }

  // 6. 測試：路徑標準化驗證
  {
    std::cout << "[Test 6] 驗證相對路徑載入後自動標準化為絕對路徑..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    assert(lib.get_path().is_absolute());
    std::cout << "  ✅ 路徑標準化驗證通過，正規路徑: " << lib.get_path() << std::endl;
  }

  // 7. 測試：UTF-8 專屬路徑字串介面驗證
  {
    std::cout << "[Test 7] 驗證 UTF-8 路徑介面 load(std::string_view) 與 get_path_utf8()..." << std::endl;
    auto u8 = plugin_path.u8string();
    std::string utf8_path_str(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    auto lib = ork::DynamicLibrary::load(std::string_view(utf8_path_str));
    assert(lib.is_loaded());
    std::string loaded_utf8 = lib.get_path_utf8();
    assert(!loaded_utf8.empty());
    assert(lib.get_path().is_absolute());

    auto add_fn = lib.get_symbol<AddNumbersFn>("AddNumbers");
    assert(add_fn != nullptr);
    assert(add_fn(20, 22) == 42);
    std::cout << "  ✅ UTF-8 介面載入與符號解析成功，UTF-8 路徑: " << loaded_utf8 << std::endl;
  }

  // 8. 測試：WeakDynamicLibrary 弱引用、reset() 後晉升重獲 (lock()) 與自動卸載全流程驗證
  {
    std::cout << "[Test 8] 驗證 WeakDynamicLibrary 弱引用機制與 reset() 後重獲 (lock())..." << std::endl;

    // 8.1 驗證空弱引用初始狀態
    ork::WeakDynamicLibrary empty_weak;
    assert(empty_weak.expired());
    assert(!empty_weak);
    assert(empty_weak.use_count() == 0);
    assert(!empty_weak.lock().is_loaded());

    // 8.2 載入動態庫並獲取弱引用觀察者
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    assert(lib.use_count() == 1);

    ork::WeakDynamicLibrary weak_lib = lib.to_weak();
    assert(!weak_lib.expired());
    assert(weak_lib);
    assert(weak_lib.use_count() == 1);

    // 8.3 透過 bind_lifecycle 建立第一個受管物件
    auto create_fn = lib.get_symbol<CreatePluginFn>("CreateTestPlugin");
    auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyTestPlugin");
    assert(create_fn && destroy_fn);

    std::shared_ptr<ITestPlugin> plugin1 = lib.bind_lifecycle(create_fn(), destroy_fn);
    assert(plugin1 != nullptr);
    assert(weak_lib.use_count() == 2); // lib (1) + plugin1 (1)

    // 8.4 關鍵場景：主程式主動呼叫 reset() 放棄初始句柄以利後續自動卸載
    std::cout << "  -> 主程式呼叫 lib.reset() 放棄初始強引用句柄..." << std::endl;
    lib.reset();
    assert(!lib.is_loaded());
    assert(lib.use_count() == 0);

    // 此時主程式的 lib 已為空，但因為 plugin1 仍存活，DLL 尚未卸載
    assert(!weak_lib.expired());
    assert(weak_lib);
    assert(weak_lib.use_count() == 1); // 僅剩 plugin1 持有
    assert(plugin1->Multiply(2, 3) == 6);

    // 8.5 核心驗證：主程式日後再次需要使用該 DLL 時，透過 weak_lib.lock() 成功晉升重新獲取強引用！
    std::cout << "  -> 核心驗證：透過 weak_lib.lock() 晉升重新獲取有效 DynamicLibrary..." << std::endl;
    auto locked_lib = weak_lib.lock();
    assert(locked_lib.is_loaded());
    assert(locked_lib);
    assert(locked_lib.use_count() == 2); // plugin1 (1) + locked_lib (1)

    // 透過晉升重獲的 locked_lib 正常調用符號並建立第二個受管物件
    auto add_fn = locked_lib.get_symbol<AddNumbersFn>("AddNumbers");
    assert(add_fn != nullptr);
    assert(add_fn(10, 32) == 42);

    auto create_fn2 = locked_lib.get_symbol<CreatePluginFn>("CreateTestPlugin");
    auto destroy_fn2 = locked_lib.get_symbol<DestroyPluginFn>("DestroyTestPlugin");
    assert(create_fn2 && destroy_fn2);

    std::shared_ptr<ITestPlugin> plugin2 = locked_lib.bind_lifecycle(create_fn2(), destroy_fn2);
    assert(plugin2 != nullptr);
    assert(plugin2->Multiply(7, 8) == 56);
    assert(locked_lib.use_count() == 3); // plugin1 (1) + locked_lib (1) + plugin2 (1)

    // 8.6 主程式用完後，再次放棄 locked_lib 句柄
    locked_lib.reset();
    assert(!locked_lib.is_loaded());
    assert(weak_lib.use_count() == 2); // plugin1 (1) + plugin2 (1)

    // 8.7 逐步釋放受管物件，驗證弱引用計數同步遞減與最終自動過期
    std::cout << "  -> 銷毀第一個受管物件 plugin1..." << std::endl;
    plugin1.reset();
    assert(weak_lib.use_count() == 1);
    assert(!weak_lib.expired());

    // 仍能從 weak_lib.lock() 成功重獲
    assert(weak_lib.lock().is_loaded());

    std::cout << "  -> 銷毀最後一個受管物件 plugin2，觸發底層動態庫安全自動卸載..." << std::endl;
    plugin2.reset();

    // 此時所有活體物件與強引用全數歸零，底層 DLL 已自動卸載！
    assert(weak_lib.use_count() == 0);
    assert(weak_lib.expired());
    assert(!weak_lib);

    // 再次嘗試 lock() 應安全返回無效實例
    auto failed_lock = weak_lib.lock();
    assert(!failed_lock.is_loaded());
    assert(!failed_lock);
    assert(!failed_lock.get_last_error().empty());
    std::cout << "  ✅ 驗證弱引用在 DLL 自動卸載後安全過期，lock() 傳回無效實例並包含錯誤訊息: "
              << failed_lock.get_last_error() << std::endl;
    std::cout << "  ✅ WeakDynamicLibrary 與 to_weak() / lock() 弱引用全流程測試通過！" << std::endl;
  }

  // 9. 測試：動態庫多重載入分辨（首次載入 vs 重複載入）與生命週期啟始/收尾
  {
    std::cout << "[Test 9] 驗證首次/重複載入分辨、initialize_once 單次初始化與 add_cleanup_hook 卸載收尾..." << std::endl;
    int host_cleanup_counter = 0;

    // 9.1 首次載入 (0 -> 1)
    auto lib_first = ork::DynamicLibrary::load(plugin_path);
    assert(lib_first.is_loaded());
    assert(lib_first.is_first_loaded() == true); // 💡 精準識別為首次載入
    assert(lib_first.use_count() == 1);

    // 執行首次初始化
    bool init_ok = lib_first.initialize_once<int()>("PluginInit");
    assert(init_ok == true);

    auto get_init_cnt = lib_first.get_symbol<int()>("GetInitCallCount");
    assert(get_init_cnt != nullptr);
    assert(get_init_cnt() == 1);

    // 註冊 Host 端收尾閉包與外掛 DLL 內部收尾函式
    lib_first.add_cleanup_hook([&host_cleanup_counter]() {
      host_cleanup_counter++;
    });
    bool reg_shutdown_ok = lib_first.register_shutdown_symbol("PluginShutdown");
    assert(reg_shutdown_ok == true);

    // 9.2 重複請求載入 (1 -> 2)
    auto lib_second = ork::DynamicLibrary::load(plugin_path);
    assert(lib_second.is_loaded());
    assert(lib_second.is_first_loaded() == false); // 💡 精準識別為重複載入，非首次載入！
    assert(lib_first.use_count() == 2);
    assert(lib_second.use_count() == 2);

    // 嘗試對重複載入實例再次調用 initialize_once 應被自動安全略過
    bool second_init_ok = lib_second.initialize_once<int()>("PluginInit");
    assert(second_init_ok == false);
    assert(get_init_cnt() == 1); // 💡 計數依然為 1，保證不被多次呼叫！

    // 9.3 釋放第一個句柄 (2 -> 1)
    std::cout << "  -> 放棄第一個句柄 lib_first，尚有 lib_second 存活..." << std::endl;
    lib_first.reset();
    assert(lib_second.use_count() == 1);
    assert(host_cleanup_counter == 0); // 💡 尚未徹底卸載，收尾函式不可提前被執行

    // 9.4 釋放第二個句柄 (1 -> 0)，觸發全域唯一收尾與底層卸載
    std::cout << "  -> 放棄第二個句柄 lib_second，觸發底層動態庫安全卸載與收尾..." << std::endl;
    lib_second.reset();

    // 💡 驗證收尾回呼剛好被執行了 1 次！
    assert(host_cleanup_counter == 1);
    std::cout << "  ✅ host_cleanup_counter 成功觸發且僅執行 1 次 (計數: " << host_cleanup_counter << ")" << std::endl;

    // 9.5 驗證完全卸載後再次載入 (0 -> 1) 能再次被識別為首次載入
    std::cout << "  -> 驗證卸載後再次載入新輪迴..." << std::endl;
    auto lib_third = ork::DynamicLibrary::load(plugin_path);
    assert(lib_third.is_loaded());
    assert(lib_third.is_first_loaded() == true); // 💡 新生命週期再次被正確識別為首次載入！
    lib_third.reset();

    std::cout << "  ✅ 模組首次/重複載入分辨與收尾回呼全流程驗證通過！" << std::endl;
  }

  // 10. 測試：純生命週期存活權杖 (create_lifetime_token) 與多方共享
  {
    std::cout << "[Test 10] 驗證純生命週期權杖 (create_lifetime_token) 與多節點共享存活..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    auto weak_lib = lib.to_weak();

    // 模擬節點群（例如整棵樹的多個節點）持有此權杖
    std::shared_ptr<const void> root_token = lib.create_lifetime_token();
    assert(root_token != nullptr);
    assert(lib.use_count() == 2); // lib 句柄 + root_token

    // 子節點共享複製權杖
    std::shared_ptr<const void> child_node_token = root_token;
    assert(lib.use_count() == 3);

    // 宿主主動放棄 DynamicLibrary 初始句柄
    lib.reset();
    assert(!weak_lib.expired());
    assert(weak_lib.use_count() == 2); // 尚有 2 個節點權杖持有

    // 模擬 Root 釋放（假結束）
    std::cout << "  -> 模擬 Root 釋放，尚有子節點持有權杖..." << std::endl;
    root_token.reset();
    assert(!weak_lib.expired());
    assert(weak_lib.use_count() == 1); // 還有 child_node_token

    // 模擬最後一個子節點釋放（真結束）
    std::cout << "  -> 模擬最後一個子節點釋放，權杖歸零..." << std::endl;
    child_node_token.reset();
    assert(weak_lib.expired()); // 底層安全自動卸載！
    std::cout << "  ✅ 純生命週期權杖多節點共享與最後釋放自動卸載測試通過！" << std::endl;
  }

  // 11. 測試：卸載完成通知回呼 (add_post_unload_hook)
  {
    std::cout << "[Test 11] 驗證卸載完成通知回呼 (add_post_unload_hook)..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    auto weak_lib = lib.to_weak();

    bool post_unload_called = false;
    lib.add_post_unload_hook([&post_unload_called, weak_lib]() {
      post_unload_called = true;
      // 驗證在執行 post_unload_hook 時，DLL 已經處於過期（完全卸載）狀態
      assert(weak_lib.expired());
    });

    auto token = lib.create_lifetime_token();
    lib.reset(); // 放棄宿主句柄
    assert(!post_unload_called);

    // 釋放最後權杖
    token.reset();
    assert(post_unload_called == true);
    assert(weak_lib.expired());
    std::cout << "  ✅ add_post_unload_hook 成功在 DLL 物理卸載後被精確觸發！" << std::endl;
  }

  // 12. 測試：非同步離棧延遲卸載模式 (enable_deferred_unload)
  {
    std::cout << "[Test 12] 驗證非同步離棧延遲卸載模式 (enable_deferred_unload)..." << std::endl;
    auto lib = ork::DynamicLibrary::load(plugin_path);
    assert(lib.is_loaded());
    auto weak_lib = lib.to_weak();

    lib.enable_deferred_unload(true);
    assert(lib.is_deferred_unload_enabled() == true);

    std::atomic<bool> post_unload_done{false};
    lib.add_post_unload_hook([&post_unload_done]() {
      post_unload_done.store(true);
    });

    // 模擬自解構物件
    struct SelfDestructNode
    {
      std::shared_ptr<const void> token;
      bool *destructed_flag;
      ~SelfDestructNode()
      {
        *destructed_flag = true;
        // token 在此處解構（最後一個引用歸零）
      }
    };

    bool node_destructed = false;
    {
      auto token = lib.create_lifetime_token();
      lib.reset(); // 宿主句柄先放

      SelfDestructNode node{std::move(token), &node_destructed};
    } // node 離開作用域，觸發析構

    assert(node_destructed == true);
    std::cout << "  -> 自解構物件棧幀已安全退出，等待非同步離棧卸載完成..." << std::endl;

    // 等待非同步卸載執行緒完成
    int wait_cycles = 0;
    while (!post_unload_done.load() && wait_cycles < 100)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      wait_cycles++;
    }

    assert(post_unload_done.load() == true);
    assert(weak_lib.expired());
    std::cout << "  ✅ 非同步離棧延遲卸載與後置通知成功完成，100% 杜絕呼叫棧自毀崩潰！" << std::endl;
  }

  std::cout << "============================================================" << std::endl;
  std::cout << "🎉 恭喜！DynamicLibrary 所有單元測試全部 PASS！" << std::endl;
  std::cout << "============================================================" << std::endl;
  return 0;
}
