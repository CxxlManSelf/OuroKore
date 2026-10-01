#if defined(_WIN32)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#include <atomic>
#include <iostream>

static std::atomic<int> g_instance_count{0};

class ITestPlugin
{
public:
  virtual ~ITestPlugin() = default;
  virtual int Multiply(int a, int b) = 0;
  virtual const char *GetName() const = 0;
};

class TestPluginImpl : public ITestPlugin
{
public:
  TestPluginImpl()
  {
    g_instance_count.fetch_add(1, std::memory_order_relaxed);
  }

  ~TestPluginImpl() override
  {
    g_instance_count.fetch_sub(1, std::memory_order_relaxed);
  }

  int Multiply(int a, int b) override
  {
    return a * b;
  }

  const char *GetName() const override
  {
    return "TestPluginInstance";
  }
};

extern "C"
{
  PLUGIN_EXPORT int AddNumbers(int a, int b)
  {
    return a + b;
  }

  PLUGIN_EXPORT ITestPlugin *CreateTestPlugin()
  {
    return new TestPluginImpl();
  }

  PLUGIN_EXPORT void DestroyTestPlugin(ITestPlugin *plugin)
  {
    delete plugin;
  }

  PLUGIN_EXPORT int GetActiveInstanceCount()
  {
    return g_instance_count.load(std::memory_order_relaxed);
  }

  static std::atomic<int> g_init_count{0};
  static std::atomic<int> g_shutdown_count{0};

  PLUGIN_EXPORT int PluginInit()
  {
    g_init_count.fetch_add(1, std::memory_order_relaxed);
    return 0; // 0 代表成功
  }

  PLUGIN_EXPORT void PluginShutdown()
  {
    g_shutdown_count.fetch_add(1, std::memory_order_relaxed);
  }

  PLUGIN_EXPORT int GetInitCallCount()
  {
    return g_init_count.load(std::memory_order_relaxed);
  }

  PLUGIN_EXPORT int GetShutdownCallCount()
  {
    return g_shutdown_count.load(std::memory_order_relaxed);
  }
}
