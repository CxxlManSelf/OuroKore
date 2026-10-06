#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "ourokore/base/HeapTracker.hpp"

namespace ork
{

namespace detail
{

template <typename T, typename... Args>
inline T *tracked_construct(const char *file, int line, HeapTracker &tracker, Args &&...args)
{
  void *mem = tracker.allocate_aligned(sizeof(T), alignof(T), file, line);
  try
  {
    return ::new (mem) T(std::forward<Args>(args)...);
  }
  catch (...)
  {
    tracker.deallocate_aligned(mem, alignof(T));
    throw;
  }
}

template <typename T>
inline void tracked_destroy(T *ptr, HeapTracker &tracker) noexcept
{
  if (!ptr)
  {
    return;
  }
  ptr->~T();
  tracker.deallocate_aligned(const_cast<void *>(static_cast<const void *>(ptr)), alignof(T));
}

template <typename T>
inline T *tracked_construct_array(size_t count, const char *file, int line, HeapTracker &tracker)
{
  if (count == 0)
  {
    return nullptr;
  }
  size_t total_size = sizeof(T) * count;
  void *mem = tracker.allocate_aligned(total_size, alignof(T), file, line);
  T *arr = static_cast<T *>(mem);
  size_t constructed = 0;
  try
  {
    for (; constructed < count; ++constructed)
    {
      ::new (static_cast<void *>(arr + constructed)) T();
    }
    return arr;
  }
  catch (...)
  {
    for (size_t i = constructed; i > 0; --i)
    {
      arr[i - 1].~T();
    }
    tracker.deallocate_aligned(mem, alignof(T));
    throw;
  }
}

template <typename T>
inline void tracked_destroy_array(T *arr, size_t count, HeapTracker &tracker) noexcept
{
  if (!arr)
  {
    return;
  }
  for (size_t i = count; i > 0; --i)
  {
    arr[i - 1].~T();
  }
  tracker.deallocate_aligned(const_cast<void *>(static_cast<const void *>(arr)), alignof(T));
}

}  // namespace detail

/**
 * @brief 顯式受管 new：建立單一物件實體並記錄配置點
 */
template <typename T, typename... Args>
inline T *tracked_new(const char *file, int line, Args &&...args)
{
  return detail::tracked_construct<T>(file, line, HeapTracker::get_default_tracker(), std::forward<Args>(args)...);
}

/**
 * @brief 顯式受管 delete：解構單一物件實體並自 Tracker 釋放
 */
template <typename T>
inline void tracked_delete(T *ptr) noexcept
{
  detail::tracked_destroy<T>(ptr, HeapTracker::get_default_tracker());
}

/**
 * @brief STL 相容之受管配置器，使 std::vector / std::string 等容器可主動納入 HeapTracker 追蹤
 */
template <typename T>
class TrackedAllocator
{
public:
  using value_type = T;

  TrackedAllocator() noexcept :
      m_tracker(&HeapTracker::get_default_tracker())
  {
  }
  explicit TrackedAllocator(HeapTracker &tracker) noexcept :
      m_tracker(&tracker)
  {
  }

  template <typename U>
  TrackedAllocator(const TrackedAllocator<U> &other) noexcept :
      m_tracker(other.get_tracker())
  {
  }

  T *allocate(size_t n)
  {
    if (n > static_cast<size_t>(-1) / sizeof(T))
    {
      throw std::bad_alloc();
    }
    size_t bytes = n * sizeof(T);
    void *p = m_tracker->allocate_aligned(bytes, alignof(T), "<TrackedAllocator>", 0);
    return static_cast<T *>(p);
  }

  void deallocate(T *p, size_t) noexcept
  {
    if (p)
    {
      m_tracker->deallocate_aligned(p, alignof(T));
    }
  }

  HeapTracker *get_tracker() const noexcept
  {
    return m_tracker;
  }

  template <typename U>
  bool operator==(const TrackedAllocator<U> &other) const noexcept
  {
    return m_tracker == other.get_tracker();
  }

  template <typename U>
  bool operator!=(const TrackedAllocator<U> &other) const noexcept
  {
    return m_tracker != other.get_tracker();
  }

private:
  HeapTracker *m_tracker{nullptr};
};

}  // namespace ork

/**
 * @brief 顯式取代 new 巨集，精確捕捉程式碼位置（__FILE__, __LINE__）
 */
#define ORK_NEW(Type, ...) ::ork::tracked_new<Type>(__FILE__, __LINE__, ##__VA_ARGS__)

/**
 * @brief 顯式取代 delete 巨集
 */
#define ORK_DELETE(ptr) ::ork::tracked_delete(ptr)

/**
 * @brief 顯式取代 new[] 陣列巨集
 */
#define ORK_NEW_ARRAY(Type, count) \
  ::ork::detail::tracked_construct_array<Type>(count, __FILE__, __LINE__, ::ork::HeapTracker::get_default_tracker())

/**
 * @brief 顯式取代 delete[] 陣列巨集
 */
#define ORK_DELETE_ARRAY(ptr, count) \
  ::ork::detail::tracked_destroy_array(ptr, count, ::ork::HeapTracker::get_default_tracker())
