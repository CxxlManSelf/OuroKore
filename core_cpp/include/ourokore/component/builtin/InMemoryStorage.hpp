#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ourokore/component/IStorageDriver.hpp"
#include "ourokore/component/OuroStream.hpp"

namespace ork
{

/**
 * @brief 記憶體型儲存實作 (In-Memory Storage)
 *
 * 【用途說明】
 * 實作純串流 IStorageDriver 介面之執行緒安全記憶體儲存容器。
 * 主要用於單元測試、本機模擬以及快速原型開發，並提供單一指定物件的 Save / Load 串流儲存功能。
 */
class InMemoryStorage : public IStorageDriver
{
private:
  class InMemoryWriteStream : public BlueprintStream
  {
  private:
    InMemoryStorage &m_storage;
    HandleID m_id;
    bool m_committed = false;

  public:
    InMemoryWriteStream(InMemoryStorage &storage, HandleID id) :
        m_storage(storage),
        m_id(id)
    {
    }

    ~InMemoryWriteStream() override
    {
      // ⚠️ 事務原子性保證：解構時若未顯式 Commit()，表示中途拋出例外或被取消，
      // 自動將緩衝區直接拋棄（Rollback），絕不覆蓋破壞儲存區中既有的完好存檔！
    }

    void Commit() override
    {
      if (!m_committed)
      {
        m_storage.SaveRawBuffer(m_id, GetBuffer());
        m_committed = true;
      }
    }
  };

  mutable std::mutex m_mutex;
  std::unordered_map<HandleID, std::vector<uint8_t>> m_store;

public:
  InMemoryStorage() = default;

  void SaveRawBuffer(HandleID id, const std::vector<uint8_t> &data)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store[id] = data;
  }

  std::unique_ptr<OuroStream> CreateWriteStream(HandleID id) override
  {
    return std::make_unique<InMemoryWriteStream>(*this, id);
  }

  std::unique_ptr<OuroStream> OpenReadStream(HandleID id) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_store.find(id);
    if (it == m_store.end())
    {
      return nullptr;
    }
    return std::make_unique<BlueprintStream>(it->second);
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

  bool Contains(HandleID id) const override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.find(id) != m_store.end();
  }
};

}  // namespace ork
