#pragma once

#include <cstdint>
#include <vector>

namespace ork
{

using HandleID = uint64_t;

/**
 * @brief OuroKore 核心持久化儲存驅動抽象介面 (Storage Driver Interface)
 *
 * 【用途說明】
 * 本介面為 OuroKore 核心層與底層持久化儲存媒介之間的標準驅動契約（SPI）。
 * 核心層僅透過此介面存取已打包的二進位藍圖資料（包含純 Payload 與 Edge Roster 拓撲），
 * 藉此達成核心引擎與實體儲存格式（檔案系統、SQLite、Redis 或雲端資料庫）的 100% 領域隔離與解耦。
 */
class IStorageDriver
{
public:
  virtual ~IStorageDriver() = default;

  /**
   * @brief 儲存指定 HandleID 的藍圖二進位資料
   * @param id 物件全域唯一 HandleID
   * @param data 序列化後的藍圖二進位位元組陣列
   */
  virtual void SaveBlueprint(HandleID id, const std::vector<uint8_t> &data) = 0;

  /**
   * @brief 載入指定 HandleID 的藍圖二進位資料
   * @param id 物件全域唯一 HandleID
   * @param out_data 輸出的藍圖二進位位元組陣列
   * @return true 若資料存在且成功讀取，否則傳回 false
   */
  virtual bool LoadBlueprint(HandleID id, std::vector<uint8_t> &out_data) = 0;

  /**
   * @brief 刪除指定 HandleID 的藍圖資料（如物件被徹底銷毀時）
   * @param id 物件全域唯一 HandleID
   */
  virtual void DeleteBlueprint(HandleID id) = 0;
};

}  // namespace ork
