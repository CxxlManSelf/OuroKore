#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "IStorageDriver.hpp"
#include "OuroCore.hpp"

namespace ork
{

/**
 * @brief 記憶體型儲存實作 (In-Memory Storage)
 *
 * 【用途說明】
 * 實作 IStorageDriver 介面之執行緒安全記憶體儲存容器。
 * 主要用於單元測試、本機模擬以及快速原型開發，並提供單一指定物件的 Save / Load 永續儲存與狀態管理功能。
 */
class InMemoryStorage : public IStorageDriver
{
private:
  mutable std::mutex m_mutex;
  std::unordered_map<HandleID, std::vector<uint8_t>> m_store;

public:
  InMemoryStorage() = default;

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

  // --- 單一物件 Save / Load 永續儲存功能 ---

  /**
   * @brief 儲存單一指定物件（未變更資料時跳過，具備 OuroReadLock）
   * @note 僅儲存指定物件本身的 Payload 與 Edge Roster，不遞迴儲存子物件。
   * @return true 若儲存成功或因 Clean/Dehydrated 安全跳過；若 ptr 為空傳回 false。
   */
  template <typename T>
  bool Save(const OuroPtr<T> &ptr)
  {
    if (!ptr) return false;

    // 1. 若資料未曾變更 (Clean 或 Dehydrated)，不實際做儲存動作
    StorageState state = ptr->GetStorageState();
    if (state == StorageState::Clean || state == StorageState::Dehydrated)
    {
      return true;
    }

    // 2. 唯讀鎖定並打包單一物件藍圖（Payload + Edge Roster）
    std::vector<uint8_t> data;
    {
      OuroReadLock lock(*ptr);
      data = PackBlueprint(*ptr);
    }

    // 3. 寫入儲存區並更新狀態為 Clean
    SaveBlueprint(ptr.GetTargetID(), data);
    ptr->SetStorageState(StorageState::Clean);
    return true;
  }

  /**
   * @brief 載入單一指定物件（永續儲存資料不存在時跳過，具備 OuroWriteLock）
   * @note 僅還原指定物件本身的 Payload 與 Edge Roster 拓撲連線，不遞迴載入子物件。
   * @return true 若成功載入；若儲存區無該物件資料或 ptr 為空傳回 false。
   */
  template <typename T>
  bool Load(const OuroPtr<T> &ptr)
  {
    if (!ptr) return false;

    HandleID id = ptr.GetTargetID();
    std::vector<uint8_t> data;

    // 1. 若永續儲存資料不存在，跳過載入動作
    if (!LoadBlueprint(id, data))
    {
      return false;
    }

    // 2. 獨佔寫鎖定並解包還原（UnpackBlueprint 自動處理 Edge Roster 連線對帳）
    {
      OuroWriteLock lock(*ptr);
      UnpackBlueprint(*ptr, data);
    }

    ptr->SetStorageState(StorageState::Clean);
    return true;
  }
};

}  // namespace ork
