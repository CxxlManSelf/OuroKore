#include "ourokore/base/HeapTracker.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace ork
{

namespace
{

void *raw_system_alloc(size_t size, size_t alignment)
{
  if (size == 0)
  {
    size = 1;
  }
  if (alignment > alignof(std::max_align_t))
  {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void *ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0)
    {
      return nullptr;
    }
    return ptr;
#endif
  }
  return std::malloc(size);
}

void raw_system_free(void *ptr, size_t alignment)
{
  if (!ptr)
  {
    return;
  }
  if (alignment > alignof(std::max_align_t))
  {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
  }
  else
  {
    std::free(ptr);
  }
}

} // namespace

#if (ORK_HEAP_TRACKING_MODE == ORK_HEAP_TRACK_DISABLED)

// ----------------------------------------------------------------------------
// 模式 0: 完全關閉直通 (零開銷)
// ----------------------------------------------------------------------------
class HeapTrackerImpl
{
public:
  void *allocate(size_t size, const char *, int)
  {
    void *p = raw_system_alloc(size, 0);
    if (!p) throw std::bad_alloc();
    return p;
  }

  void *allocate_aligned(size_t size, size_t alignment, const char *, int)
  {
    void *p = raw_system_alloc(size, alignment);
    if (!p) throw std::bad_alloc();
    return p;
  }

  void deallocate(void *ptr) noexcept
  {
    raw_system_free(ptr, 0);
  }

  void deallocate_aligned(void *ptr, size_t alignment) noexcept
  {
    raw_system_free(ptr, alignment);
  }

  bool is_clean() const noexcept { return true; }

  HeapStats get_stats() const noexcept { return HeapStats{}; }

  std::vector<AllocationRecord> get_leaks() const { return {}; }

  std::string dump_leaks_to_string(std::string_view) const
  {
    return "[OuroKore HeapTracker] 追蹤模式為 DISABLED (Mode 0)，零開銷直通中。\n";
  }

  void assert_clean(std::string_view) const {}

  void reset() noexcept {}
};

#elif (ORK_HEAP_TRACKING_MODE == ORK_HEAP_TRACK_ATOMIC)

// ----------------------------------------------------------------------------
// 模式 1: 輕量原子無鎖計數 (極致效能，驗收 Heap 清空)
// ----------------------------------------------------------------------------
class HeapTrackerImpl
{
public:
  struct alignas(std::max_align_t) PrefixHeader
  {
    size_t size;
    size_t alignment;
    size_t offset_to_base;
    uint32_t magic;
  };

  static constexpr uint32_t HEADER_MAGIC = 0x0712A701;

  void *allocate(size_t size, const char *, int)
  {
    return allocate_aligned(size, 0, nullptr, 0);
  }

  void *allocate_aligned(size_t size, size_t alignment, const char *, int)
  {
    if (alignment < alignof(std::max_align_t))
    {
      alignment = alignof(std::max_align_t);
    }

    size_t total_size = sizeof(PrefixHeader) + alignment + size;
    void *raw = raw_system_alloc(total_size, alignment);
    if (!raw)
    {
      throw std::bad_alloc();
    }

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw);
    uintptr_t user_addr = (raw_addr + sizeof(PrefixHeader) + alignment - 1) & ~(alignment - 1);
    PrefixHeader *header = reinterpret_cast<PrefixHeader *>(user_addr - sizeof(PrefixHeader));

    header->size = size;
    header->alignment = alignment;
    header->offset_to_base = static_cast<size_t>(user_addr - raw_addr);
    header->magic = HEADER_MAGIC;

    m_active_allocations.fetch_add(1, std::memory_order_relaxed);
    m_active_bytes.fetch_add(size, std::memory_order_relaxed);
    m_total_allocations.fetch_add(1, std::memory_order_relaxed);

    size_t current_active = m_active_bytes.load(std::memory_order_relaxed);
    size_t prev_peak = m_peak_bytes.load(std::memory_order_relaxed);
    while (current_active > prev_peak &&
           !m_peak_bytes.compare_exchange_weak(prev_peak, current_active, std::memory_order_relaxed))
    {
    }

    return reinterpret_cast<void *>(user_addr);
  }

  void deallocate(void *ptr) noexcept
  {
    deallocate_aligned(ptr, 0);
  }

  void deallocate_aligned(void *ptr, size_t) noexcept
  {
    if (!ptr)
    {
      return;
    }

    uintptr_t user_addr = reinterpret_cast<uintptr_t>(ptr);
    PrefixHeader *header = reinterpret_cast<PrefixHeader *>(user_addr - sizeof(PrefixHeader));

    if (header->magic == HEADER_MAGIC)
    {
      size_t size = header->size;
      size_t alignment = header->alignment;
      void *raw = reinterpret_cast<void *>(user_addr - header->offset_to_base);

      m_active_allocations.fetch_sub(1, std::memory_order_relaxed);
      m_active_bytes.fetch_sub(size, std::memory_order_relaxed);
      m_total_deallocations.fetch_add(1, std::memory_order_relaxed);

      header->magic = 0;
      raw_system_free(raw, alignment);
    }
    else
    {
      raw_system_free(ptr, 0);
    }
  }

  bool is_clean() const noexcept
  {
    return m_active_allocations.load(std::memory_order_seq_cst) == 0 &&
           m_active_bytes.load(std::memory_order_seq_cst) == 0;
  }

  HeapStats get_stats() const noexcept
  {
    HeapStats s;
    s.active_allocations = m_active_allocations.load(std::memory_order_relaxed);
    s.active_bytes = m_active_bytes.load(std::memory_order_relaxed);
    s.total_allocations = m_total_allocations.load(std::memory_order_relaxed);
    s.total_deallocations = m_total_deallocations.load(std::memory_order_relaxed);
    s.peak_bytes = m_peak_bytes.load(std::memory_order_relaxed);
    return s;
  }

  std::vector<AllocationRecord> get_leaks() const { return {}; }

  std::string dump_leaks_to_string(std::string_view context_name) const
  {
    auto stats = get_stats();
    std::ostringstream oss;
    oss << "================================================================================\n";
    if (context_name.empty())
    {
      oss << "[OuroKore HeapTracker] Heap 記憶體狀態報告 (模式: ATOMIC 輕量無鎖)\n";
    }
    else
    {
      oss << "[OuroKore HeapTracker] 模組/情境 [" << context_name << "] Heap 記憶體狀態報告 (模式: ATOMIC)\n";
    }
    oss << "--------------------------------------------------------------------------------\n";
    oss << "- 當前存活配置區塊數: " << stats.active_allocations << " 塊\n";
    oss << "- 當前存活記憶體佔用: " << stats.active_bytes << " bytes\n";
    oss << "- 歷史累計配置次數:   " << stats.total_allocations << "\n";
    oss << "- 歷史累計釋放次數:   " << stats.total_deallocations << "\n";
    oss << "- 歷史峰值記憶體佔用: " << stats.peak_bytes << " bytes\n";

    if (stats.active_allocations == 0)
    {
      oss << "--------------------------------------------------------------------------------\n";
      oss << "🎉 恭喜！Heap 已完全清空，零記憶體洩漏！(Zero Memory Leak Clean)\n";
      oss << "================================================================================\n";
    }
    else
    {
      oss << "--------------------------------------------------------------------------------\n";
      oss << "🚨 偵測到未釋放之記憶體洩漏 (共 " << stats.active_allocations
          << " 區塊未歸還，合計 " << stats.active_bytes << " bytes)！\n";
      oss << "💡 提示：若需精確定位檔案與行號，請以 DETAILED (模式 2) 重新編譯。\n";
      oss << "================================================================================\n";
    }
    return oss.str();
  }

  void assert_clean(std::string_view context_name) const
  {
    if (!is_clean())
    {
      std::string report = dump_leaks_to_string(context_name);
      std::cerr << "\n" << report << std::endl;
      throw std::runtime_error("OuroKore HeapTracker Assertion Failed: Heap is not clean!\n" + report);
    }
  }

  void reset() noexcept
  {
    m_active_allocations.store(0, std::memory_order_relaxed);
    m_active_bytes.store(0, std::memory_order_relaxed);
    m_total_allocations.store(0, std::memory_order_relaxed);
    m_total_deallocations.store(0, std::memory_order_relaxed);
    m_peak_bytes.store(0, std::memory_order_relaxed);
  }

private:
  std::atomic<size_t> m_active_allocations{0};
  std::atomic<size_t> m_active_bytes{0};
  std::atomic<size_t> m_total_allocations{0};
  std::atomic<size_t> m_total_deallocations{0};
  std::atomic<size_t> m_peak_bytes{0};
};

#else // ORK_HEAP_TRACK_DETAILED

// ----------------------------------------------------------------------------
// 模式 2: 詳細定位診斷 (含互斥鎖、檔名行號、序號、洩漏清單)
// ----------------------------------------------------------------------------
namespace
{
thread_local bool t_in_tracker = false;

struct TrackerScopeGuard
{
  TrackerScopeGuard() { t_in_tracker = true; }
  ~TrackerScopeGuard() { t_in_tracker = false; }
};

template <typename T>
class RawSystemAllocator
{
public:
  using value_type = T;
  RawSystemAllocator() noexcept = default;
  template <typename U>
  RawSystemAllocator(const RawSystemAllocator<U> &) noexcept {}

  T *allocate(size_t n)
  {
    if (n > static_cast<size_t>(-1) / sizeof(T)) throw std::bad_alloc();
    void *p = std::malloc(n * sizeof(T));
    if (!p) throw std::bad_alloc();
    return static_cast<T *>(p);
  }

  void deallocate(T *p, size_t) noexcept { std::free(p); }
  bool operator==(const RawSystemAllocator &) const noexcept { return true; }
  bool operator!=(const RawSystemAllocator &) const noexcept { return false; }
};

} // namespace

class HeapTrackerImpl
{
public:
  using AllocMap = std::unordered_map<
      void *,
      AllocationRecord,
      std::hash<void *>,
      std::equal_to<void *>,
      RawSystemAllocator<std::pair<void *const, AllocationRecord>>>;

  void *allocate(size_t size, const char *file, int line)
  {
    return allocate_aligned(size, 0, file, line);
  }

  void *allocate_aligned(size_t size, size_t alignment, const char *file, int line)
  {
    if (t_in_tracker)
    {
      return raw_system_alloc(size, alignment);
    }

    void *ptr = raw_system_alloc(size, alignment);
    if (!ptr)
    {
      throw std::bad_alloc();
    }

    {
      TrackerScopeGuard guard;
      std::lock_guard<std::mutex> lock(m_mutex);

      AllocationRecord record;
      record.address = ptr;
      record.size = size;
      record.alignment = alignment;
      record.file = file ? file : "<unspecified>";
      record.line = line;
      record.sequence_id = m_next_sequence++;

      m_allocations.insert_or_assign(ptr, record);

      m_stats.active_allocations++;
      m_stats.active_bytes += size;
      m_stats.total_allocations++;
      if (m_stats.active_bytes > m_stats.peak_bytes)
      {
        m_stats.peak_bytes = m_stats.active_bytes;
      }
    }

    return ptr;
  }

  void deallocate(void *ptr) noexcept
  {
    deallocate_aligned(ptr, 0);
  }

  void deallocate_aligned(void *ptr, size_t alignment) noexcept
  {
    if (!ptr)
    {
      return;
    }

    if (t_in_tracker)
    {
      raw_system_free(ptr, alignment);
      return;
    }

    size_t recorded_alignment = alignment;
    {
      TrackerScopeGuard guard;
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = m_allocations.find(ptr);
      if (it != m_allocations.end())
      {
        m_stats.active_allocations--;
        m_stats.active_bytes -= it->second.size;
        m_stats.total_deallocations++;
        if (it->second.alignment > 0)
        {
          recorded_alignment = it->second.alignment;
        }
        m_allocations.erase(it);
      }
    }

    raw_system_free(ptr, recorded_alignment);
  }

  bool is_clean() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats.active_allocations == 0 && m_stats.active_bytes == 0 && m_allocations.empty();
  }

  HeapStats get_stats() const noexcept
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
  }

  std::vector<AllocationRecord> get_leaks() const
  {
    TrackerScopeGuard guard;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AllocationRecord> leaks;
    leaks.reserve(m_allocations.size());
    for (const auto &pair : m_allocations)
    {
      leaks.push_back(pair.second);
    }
    std::sort(leaks.begin(), leaks.end(), [](const AllocationRecord &a, const AllocationRecord &b) {
      return a.sequence_id < b.sequence_id;
    });
    return leaks;
  }

  std::string dump_leaks_to_string(std::string_view context_name) const
  {
    auto leaks = get_leaks();
    auto stats = get_stats();

    std::ostringstream oss;
    oss << "================================================================================\n";
    if (context_name.empty())
    {
      oss << "[OuroKore HeapTracker] Heap 記憶體狀態報告 (模式: DETAILED 詳細診斷)\n";
    }
    else
    {
      oss << "[OuroKore HeapTracker] 模組/情境 [" << context_name << "] Heap 記憶體狀態報告 (模式: DETAILED)\n";
    }
    oss << "--------------------------------------------------------------------------------\n";
    oss << "- 當前存活配置區塊數: " << stats.active_allocations << " 塊\n";
    oss << "- 當前存活記憶體佔用: " << stats.active_bytes << " bytes\n";
    oss << "- 歷史累計配置次數:   " << stats.total_allocations << "\n";
    oss << "- 歷史累計釋放次數:   " << stats.total_deallocations << "\n";
    oss << "- 歷史峰值記憶體佔用: " << stats.peak_bytes << " bytes\n";

    if (leaks.empty())
    {
      oss << "--------------------------------------------------------------------------------\n";
      oss << "🎉 恭喜！Heap 已完全清空，零記憶體洩漏！(Zero Memory Leak Clean)\n";
      oss << "================================================================================\n";
    }
    else
    {
      oss << "--------------------------------------------------------------------------------\n";
      oss << "🚨 偵測到未釋放之記憶體洩漏 (Memory Leaks Detected, 共 " << leaks.size() << " 筆):\n";
      for (size_t i = 0; i < leaks.size(); ++i)
      {
        const auto &rec = leaks[i];
        oss << "  #" << (i + 1)
            << " [序號: " << rec.sequence_id << "]"
            << " 位址: 0x" << std::hex << reinterpret_cast<uintptr_t>(rec.address) << std::dec
            << ", 大小: " << rec.size << " bytes"
            << ", 來源: " << (rec.file ? rec.file : "<unknown>") << ":" << rec.line << "\n";
      }
      oss << "================================================================================\n";
    }

    return oss.str();
  }

  void assert_clean(std::string_view context_name) const
  {
    if (!is_clean())
    {
      std::string report = dump_leaks_to_string(context_name);
      std::cerr << "\n" << report << std::endl;
      throw std::runtime_error("OuroKore HeapTracker Assertion Failed: Heap is not clean!\n" + report);
    }
  }

  void reset() noexcept
  {
    TrackerScopeGuard guard;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocations.clear();
    m_stats = HeapStats{};
    m_next_sequence = 1;
  }

private:
  mutable std::mutex m_mutex;
  AllocMap m_allocations;
  HeapStats m_stats;
  uint64_t m_next_sequence{1};
};

#endif

HeapTracker::HeapTracker() : m_impl(std::make_unique<HeapTrackerImpl>()) {}

HeapTracker::~HeapTracker() = default;

HeapTracker &HeapTracker::get_default_tracker() noexcept
{
  static HeapTracker s_tracker;
  return s_tracker;
}

void *HeapTracker::allocate(size_t size, const char *file, int line)
{
  return m_impl->allocate(size, file, line);
}

void *HeapTracker::allocate_aligned(size_t size, size_t alignment, const char *file, int line)
{
  return m_impl->allocate_aligned(size, alignment, file, line);
}

void HeapTracker::deallocate(void *ptr) noexcept
{
  m_impl->deallocate(ptr);
}

void HeapTracker::deallocate_aligned(void *ptr, size_t alignment) noexcept
{
  m_impl->deallocate_aligned(ptr, alignment);
}

bool HeapTracker::is_clean() const noexcept
{
  return m_impl->is_clean();
}

HeapStats HeapTracker::get_stats() const noexcept
{
  return m_impl->get_stats();
}

std::vector<AllocationRecord> HeapTracker::get_leaks() const
{
  return m_impl->get_leaks();
}

std::string HeapTracker::dump_leaks_to_string(std::string_view context_name) const
{
  return m_impl->dump_leaks_to_string(context_name);
}

void HeapTracker::assert_clean(std::string_view context_name) const
{
  m_impl->assert_clean(context_name);
}

void HeapTracker::reset() noexcept
{
  m_impl->reset();
}

} // namespace ork

#include "ourokore/base/heap_api.h"

extern "C"
{

void *ORK_CALL ork_heap_allocate(size_t size, const char *file, int32_t line)
{
  try
  {
    return ork::HeapTracker::get_default_tracker().allocate(size, file, line);
  }
  catch (...)
  {
    return nullptr;
  }
}

void *ORK_CALL ork_heap_allocate_aligned(size_t size, size_t alignment, const char *file, int32_t line)
{
  try
  {
    return ork::HeapTracker::get_default_tracker().allocate_aligned(size, alignment, file, line);
  }
  catch (...)
  {
    return nullptr;
  }
}

void ORK_CALL ork_heap_deallocate(void *ptr)
{
  try
  {
    ork::HeapTracker::get_default_tracker().deallocate(ptr);
  }
  catch (...)
  {
  }
}

void ORK_CALL ork_heap_deallocate_aligned(void *ptr, size_t alignment)
{
  try
  {
    ork::HeapTracker::get_default_tracker().deallocate_aligned(ptr, alignment);
  }
  catch (...)
  {
  }
}

int32_t ORK_CALL ork_heap_is_clean(void)
{
  try
  {
    return ork::HeapTracker::get_default_tracker().is_clean() ? 1 : 0;
  }
  catch (...)
  {
    return 0;
  }
}

uint64_t ORK_CALL ork_heap_get_active_allocations(void)
{
  try
  {
    return static_cast<uint64_t>(ork::HeapTracker::get_default_tracker().get_stats().active_allocations);
  }
  catch (...)
  {
    return 0;
  }
}

uint64_t ORK_CALL ork_heap_get_active_bytes(void)
{
  try
  {
    return static_cast<uint64_t>(ork::HeapTracker::get_default_tracker().get_stats().active_bytes);
  }
  catch (...)
  {
    return 0;
  }
}

int32_t ORK_CALL ork_heap_dump_leaks(char *out_buf, size_t buf_size)
{
  try
  {
    std::string report = ork::HeapTracker::get_default_tracker().dump_leaks_to_string();
    if (!out_buf || buf_size == 0)
    {
      return static_cast<int32_t>(report.size() + 1);
    }
    size_t copy_len = (report.size() < buf_size - 1) ? report.size() : (buf_size - 1);
    std::memcpy(out_buf, report.data(), copy_len);
    out_buf[copy_len] = '\0';
    return static_cast<int32_t>(copy_len);
  }
  catch (...)
  {
    return -1;
  }
}

int32_t ORK_CALL ork_heap_assert_clean(const char *context_name)
{
  try
  {
    ork::HeapTracker::get_default_tracker().assert_clean(context_name ? context_name : "");
    return 0;
  }
  catch (...)
  {
    return -1;
  }
}

void ORK_CALL ork_heap_reset(void)
{
  try
  {
    ork::HeapTracker::get_default_tracker().reset();
  }
  catch (...)
  {
  }
}

} // extern "C"
