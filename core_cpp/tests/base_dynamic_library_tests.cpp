#include <cassert>
#include <filesystem>
#include <iostream>

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

  // 尋找測試動態庫路徑
  std::filesystem::path plugin_filename = ork::DynamicLibrary::format_filename("test_plugin_dll");
  std::filesystem::path exe_dir;
  if (argc > 0 && argv[0])
  {
    exe_dir = std::filesystem::path(argv[0]).parent_path();
  }

  std::filesystem::path search_paths[] = {
      exe_dir / plugin_filename,
      exe_dir / "../lib" / plugin_filename,
      std::filesystem::current_path() / "build" / "bin" / plugin_filename,
      std::filesystem::current_path() / "build" / "lib" / plugin_filename,
      std::filesystem::current_path() / "bin" / plugin_filename,
      std::filesystem::current_path() / "lib" / plugin_filename,
      std::filesystem::path("build") / "bin" / plugin_filename,
      std::filesystem::path("build") / "lib" / plugin_filename,
      std::filesystem::path("bin") / plugin_filename,
      std::filesystem::path("lib") / plugin_filename,
      std::filesystem::path("../bin") / plugin_filename,
      std::filesystem::path("../lib") / plugin_filename,
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

  // 4. 測試：核心驗證 —— 物件生命週期反向錨定（Life-Bound Retention）
  // 驗證重點：DynamicLibrary 句柄本體銷毀後，由其建立的物件依然存活，代碼段不被卸載，
  // 直至最後一個物件被銷毀，動態庫才在底層安全卸載！
  {
    std::cout << "[Test 4] 核心驗證：物件生命週期反向錨定 (Life-Bound Retention)..." << std::endl;

    std::shared_ptr<ITestPlugin> managed_plugin;

    {
      // 在局部作用域建立 DynamicLibrary 實例
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

      std::cout << "  -> 離開局部作用域，銷毀 scoped_lib 變數本體..." << std::endl;
      // 離開此處時，scoped_lib 變數解構！但 managed_plugin 依然存活！
    }

    std::cout << "  -> 此時 scoped_lib 變數已解構，但受管物件仍持有動態庫存活權杖！" << std::endl;
    // 關鍵時刻：若 DLL 已被卸載，呼叫虛擬函式 Multiply 或 GetName 將立即引發 0xC0000005 崩潰！
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
    std::cout << "  ✅ [PASS] 所有受管物件銷毀，Deleter 成功執行且底層動態庫安全卸載無崩潰！" << std::endl;
  }

  // 5. 測試：使用 bind_lifecycle 綁定手動獲取的指標
  {
    std::cout << "[Test 5] 驗證手動指標透過 bind_lifecycle 綁定生命週期..." << std::endl;
    std::shared_ptr<ITestPlugin> plugin2;
    {
      auto lib = ork::DynamicLibrary::load(plugin_path);
      auto create_fn = lib.get_symbol<CreatePluginFn>("CreateTestPlugin");
      auto destroy_fn = lib.get_symbol<DestroyPluginFn>("DestroyTestPlugin");
      assert(create_fn && destroy_fn);

      ITestPlugin *raw = create_fn();
      plugin2 = lib.bind_lifecycle(raw, destroy_fn);
      assert(plugin2 != nullptr);
      assert(lib.use_count() == 2);
    }
    // 外部 lib 銷毀後，plugin2 依然能工作
    assert(plugin2->Multiply(3, 4) == 12);
    plugin2.reset();
    std::cout << "  ✅ bind_lifecycle 測試通過！" << std::endl;
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

  std::cout << "============================================================" << std::endl;
  std::cout << "🎉 恭喜！DynamicLibrary 所有單元測試全部 PASS！" << std::endl;
  std::cout << "============================================================" << std::endl;
  return 0;
}
