#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ork
{

using HandleID = uint64_t;

/**
 * @brief Pure virtual communication interface for persistent storage backends in OuroKore.
 * Core only communicates with this interface, maintaining 100% domain isolation.
 */
class IStorageBackend
{
public:
  virtual ~IStorageBackend() = default;

  /**
   * @brief Save serialized blueprint data for a given HandleID.
   */
  virtual void SaveBlueprint(HandleID id, const std::vector<uint8_t> &data) = 0;

  /**
   * @brief Load blueprint data for a given HandleID.
   * @return true if data exists and loaded, false otherwise.
   */
  virtual bool LoadBlueprint(HandleID id, std::vector<uint8_t> &out_data) = 0;

  /**
   * @brief Delete blueprint data for a given HandleID.
   */
  virtual void DeleteBlueprint(HandleID id) = 0;
};

/**
 * @brief Thread-safe, memory-backed storage backend for fast unit testing & mock storage.
 */
class InMemoryStorageBackend : public IStorageBackend
{
private:
  mutable std::mutex m_mutex;
  std::unordered_map<HandleID, std::vector<uint8_t>> m_store;

public:
  InMemoryStorageBackend() = default;

  void SaveBlueprint(HandleID id, const std::vector<uint8_t> &data) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store[id] = data;
  }

  bool LoadBlueprint(HandleID id, std::vector<uint8_t> &out_data) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_store.find(id);
    if (it == m_store.end())
    {
      return false;
    }
    out_data = it->second;
    return true;
  }

  void DeleteBlueprint(HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store.erase(id);
  }

  size_t GetCount() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.size();
  }

  bool Contains(HandleID id) const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.find(id) != m_store.end();
  }
};

}  // namespace ork
