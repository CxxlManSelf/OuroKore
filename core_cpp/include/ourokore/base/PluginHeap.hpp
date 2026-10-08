#pragma once

#include <algorithm>
#include <cstring>
#include <iostream>
#include <new> // IWYU pragma: keep
#include <string_view>

#include "ourokore/base/HeapTracker.hpp"


namespace ork
{

/**
 * @brief 外掛模組 Heap 快速存取命名空間
 */
namespace PluginHeap
{

/**
 * @brief 取得當前外掛模組預設之 HeapTracker 實例
 */
inline HeapTracker &get_tracker() noexcept
{
  return HeapTracker::get_default_tracker();
}

/**
 * @brief 查詢當前模組 Heap 是否已經完全清空（零未釋放區塊）
 */
inline bool is_clean() noexcept
{
  return get_tracker().is_clean();
}

/**
 * @brief 取得當前模組 Heap 統計資訊
 */
inline HeapStats get_stats() noexcept
{
  return get_tracker().get_stats();
}

/**
 * @brief 斷言 Heap 已完全清空；若有洩漏則印出詳細報告並拋出 std::runtime_error
 */
inline void assert_clean(std::string_view context_name = "Plugin")
{
  get_tracker().assert_clean(context_name);
}

/**
 * @brief 輸出當前未釋放之洩漏報告字串
 */
inline std::string dump_leaks_to_string(std::string_view context_name = "Plugin")
{
  return get_tracker().dump_leaks_to_string(context_name);
}

}  // namespace PluginHeap

/**
 * @brief 外掛 Heap 洩漏偵測 RAII 守衛
 *
 * 在外掛模組主入口或測試案例作用域中宣告：
 * 離開作用域時自動檢查 Heap 是否完全清空，若有殘留立即向 stderr 輸出詳細洩漏清單。
 */
class PluginHeapGuard
{
public:
  /**
   * @brief 建構外掛 Heap 洩漏守衛
   *
   * 內部採用固定緩衝區進行深拷貝（Deep Copy），100% 杜絕外部傳入暫時字串（如 "Test_" + name）
   * 提早銷毀所導致的懸空字串視圖（Dangling String View）問題。
   * 同時保證零堆記憶體配置（Zero Heap Allocation），避免自身配置污染外掛的 HeapTracker 記帳。
   *
   * @param context_name 診斷情境或模組名稱（若超過緩衝區上限將安全截斷）
   * @param strict_abort 發現洩漏時是否立即強制呼叫 std::abort() 終止進程
   */
  explicit PluginHeapGuard(std::string_view context_name = "Plugin", bool strict_abort = false) :
      m_strict_abort(strict_abort)
  {
    set_context_name(context_name);
  }

  ~PluginHeapGuard()
  {
    if (!PluginHeap::is_clean())
    {
      std::string report = PluginHeap::dump_leaks_to_string(get_context_name());
      std::cerr << "\n" << report << std::endl;
      if (m_strict_abort)
      {
        std::abort();
      }
    }
  }

  /**
   * @brief 取得當前守衛持有的情境名稱視圖
   */
  [[nodiscard]] std::string_view get_context_name() const noexcept
  {
    return std::string_view(m_context_buffer, m_context_len);
  }

  PluginHeapGuard(const PluginHeapGuard &) = delete;
  PluginHeapGuard &operator=(const PluginHeapGuard &) = delete;

private:
  void set_context_name(std::string_view name) noexcept
  {
    std::size_t copy_len = (std::min)(name.size(), sizeof(m_context_buffer) - 1);
    if (copy_len > 0)
    {
      std::memcpy(m_context_buffer, name.data(), copy_len);
    }
    m_context_buffer[copy_len] = '\0';
    m_context_len = copy_len;
  }

  // 內部內聯緩衝區：兼顧無堆配置（Zero Heap Allocation）與抗懸空字串（Anti-Dangling）
  char m_context_buffer[128]{'\0'};
  std::size_t m_context_len{0};
  bool m_strict_abort{false};
};

}  // namespace ork

/**
 * @brief 外掛全域 operator new/delete 攔截啟用巨集
 *
 * 【使用方式】：
 * 在外掛 DLL/MODULE 的任一核心 .cpp 實作檔中加入 `ORK_ENABLE_PLUGIN_HEAP_TRACKING()`，
 * 即可全面替換該動態庫模組內的所有 new、delete、new[]、delete[] 以及 STL 容器的堆配置，
 * 將其全部透明導向 HeapTracker 進行統計與洩漏診斷！
 */
#define ORK_ENABLE_PLUGIN_HEAP_TRACKING()                                                            \
  void *operator new(std::size_t size)                                                               \
  {                                                                                                  \
    return ::ork::HeapTracker::get_default_tracker().allocate(size, "<plugin operator new>", 0);     \
  }                                                                                                  \
  void *operator new[](std::size_t size)                                                             \
  {                                                                                                  \
    return ::ork::HeapTracker::get_default_tracker().allocate(size, "<plugin operator new[]>", 0);   \
  }                                                                                                  \
  void operator delete(void *ptr) noexcept                                                           \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate(ptr);                                       \
  }                                                                                                  \
  void operator delete[](void *ptr) noexcept                                                         \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate(ptr);                                       \
  }                                                                                                  \
  void operator delete(void *ptr, std::size_t) noexcept                                              \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate(ptr);                                       \
  }                                                                                                  \
  void operator delete[](void *ptr, std::size_t) noexcept                                            \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate(ptr);                                       \
  }                                                                                                  \
  void *operator new(std::size_t size, std::align_val_t al)                                          \
  {                                                                                                  \
    return ::ork::HeapTracker::get_default_tracker().allocate_aligned(                               \
        size, static_cast<std::size_t>(al), "<plugin aligned new>", 0                                \
    );                                                                                               \
  }                                                                                                  \
  void *operator new[](std::size_t size, std::align_val_t al)                                        \
  {                                                                                                  \
    return ::ork::HeapTracker::get_default_tracker().allocate_aligned(                               \
        size, static_cast<std::size_t>(al), "<plugin aligned new[]>", 0                              \
    );                                                                                               \
  }                                                                                                  \
  void operator delete(void *ptr, std::align_val_t al) noexcept                                      \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate_aligned(ptr, static_cast<std::size_t>(al)); \
  }                                                                                                  \
  void operator delete[](void *ptr, std::align_val_t al) noexcept                                    \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate_aligned(ptr, static_cast<std::size_t>(al)); \
  }                                                                                                  \
  void operator delete(void *ptr, std::size_t, std::align_val_t al) noexcept                         \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate_aligned(ptr, static_cast<std::size_t>(al)); \
  }                                                                                                  \
  void operator delete[](void *ptr, std::size_t, std::align_val_t al) noexcept                       \
  {                                                                                                  \
    ::ork::HeapTracker::get_default_tracker().deallocate_aligned(ptr, static_cast<std::size_t>(al)); \
  }
