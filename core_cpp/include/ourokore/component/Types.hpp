#pragma once

#include <cstdint>
#include "ourokore/c_api/core.h"

namespace ork
{

/**
 * @brief 物件全域唯一識別碼型別別名
 * 與底層 C API (core.h) 的 HandleID 保持完全一致。
 */
using HandleID = ::HandleID;

/// @brief 棧上 / 全域暫存持有者之特殊 Root ID（與底層 ORK_ROOT_ID 一致）
constexpr HandleID kRootID = ORK_ROOT_ID;

/// @brief 無效或未綁定物件的 HandleID 常數
constexpr HandleID kInvalidHandleID = 0;

/**
 * @brief 物件持久化與生命週期儲存狀態
 */
enum class StorageState : uint8_t
{
  UnsavedNew = 0,  ///< 新建受管物件，尚未寫入持久化儲存
  Clean = 1,       ///< 已落盤儲存，且記憶體中的實體資料與磁碟一致（未修改）
  Dirty = 2,       ///< 已在儲存體中，但記憶體中的實體資料已被修改，尚未同步落盤
  Dehydrated = 3   ///< 已安全落盤，且實體記憶體已被釋放（空殼狀態）
};

}  // namespace ork
