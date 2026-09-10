#pragma once

#include <memory>

#include "OuroStream.hpp"
#include "Types.hpp"

namespace ork
{

/**
 * @brief OuroKore 核心持久化儲存驅動抽象介面 (Stream-based Storage Driver SPI)
 *
 * 【用途說明】
 * 本介面為 OuroKore 核心層與底層持久化儲存媒介之間的純串流驅動契約。
 * 核心層僅透過此介面開啟抽象 OuroStream 串流進行流式讀寫，
 * 核心內部完全不碰觸任何具體資料緩衝容器（如 std::vector<uint8_t>）。
 */
class IStorageDriver
{
public:
  virtual ~IStorageDriver() = default;

  /**
   * @brief 建立用於寫入指定 HandleID 物件藍圖的串流
   * @param id 物件全域唯一 HandleID
   * @return 抽象 OuroStream 寫入串流物件（具體實作由 Driver 決定）
   */
  virtual std::unique_ptr<OuroStream> CreateWriteStream(HandleID id) = 0;

  /**
   * @brief 開啟用於讀取指定 HandleID 物件藍圖的串流
   * @param id 物件全域唯一 HandleID
   * @return 抽象 OuroStream 讀取串流物件，若該 ID 無儲存資料則傳回 nullptr
   */
  virtual std::unique_ptr<OuroStream> OpenReadStream(HandleID id) = 0;

  /**
   * @brief 刪除指定 HandleID 的藍圖資料
   * @param id 物件全域唯一 HandleID
   */
  virtual void DeleteBlueprint(HandleID id) = 0;

  /**
   * @brief 檢查指定 HandleID 是否存在藍圖資料
   */
  virtual bool Contains(HandleID id) const = 0;
};

}  // namespace ork
