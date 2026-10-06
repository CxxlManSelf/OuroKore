#if defined(_WIN32)
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#include <cstring>
#include <string>

#include "ourokore/base/PluginHeap.hpp"

// 啟動外掛模組全域 operator new/delete 攔截
ORK_ENABLE_PLUGIN_HEAP_TRACKING()

struct PluginData
{
  int id{0};
  std::string name;
  double score{0.0};
};

static void *g_intentional_leak = nullptr;

extern "C"
{

PLUGIN_EXPORT void *PluginAllocData(int id, const char *name, double score)
{
  auto *data = new PluginData{id, name ? name : "", score};
  return data;
}

PLUGIN_EXPORT void PluginFreeData(void *ptr)
{
  auto *data = static_cast<PluginData *>(ptr);
  delete data;
}

PLUGIN_EXPORT void PluginTriggerLeak()
{
  if (!g_intentional_leak)
  {
    g_intentional_leak = new int[256];
  }
}

PLUGIN_EXPORT void PluginResolveLeak()
{
  if (g_intentional_leak)
  {
    delete[] static_cast<int *>(g_intentional_leak);
    g_intentional_leak = nullptr;
  }
}

PLUGIN_EXPORT int32_t PluginCheckHeapIsClean()
{
  return ork::PluginHeap::is_clean() ? 1 : 0;
}

PLUGIN_EXPORT uint64_t PluginGetActiveAllocations()
{
  return static_cast<uint64_t>(ork::PluginHeap::get_stats().active_allocations);
}

PLUGIN_EXPORT int32_t PluginDumpLeaks(char *out_buf, size_t buf_size)
{
  std::string report = ork::PluginHeap::dump_leaks_to_string("TrackedPluginDLL");
  if (!out_buf || buf_size == 0)
  {
    return static_cast<int32_t>(report.size() + 1);
  }
  size_t copy_len = (report.size() < buf_size - 1) ? report.size() : (buf_size - 1);
  std::memcpy(out_buf, report.data(), copy_len);
  out_buf[copy_len] = '\0';
  return static_cast<int32_t>(copy_len);
}

PLUGIN_EXPORT int32_t PluginShutdownCheck()
{
  try
  {
    ork::PluginHeap::assert_clean("TrackedPluginDLL");
    return 0; // 0 代表正常無洩漏
  }
  catch (...)
  {
    return -1; // -1 代表檢測到未釋放洩漏
  }
}

} // extern "C"
