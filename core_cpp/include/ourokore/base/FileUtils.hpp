#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ourokore/base/Result.hpp"
#include "ourokore/base/export.h"

namespace ork::base::fs
{

/**
 * @brief 原子檔案寫入模式
 */
enum class WriteMode
{
  ReplaceExisting,  ///< 若目標存在則原子覆寫 (預設)
  FailIfExists      ///< 若目標存在則失敗早退
};

/**
 * @brief 原子寫入資料至目標檔案 (Zero-Corruption Invariant)
 *
 * 演算法保證：
 * 1. 寫入同一目錄下之臨時暫存檔 (.tmp.[PID].[Random])。
 * 2. 顯式執行 OS 磁碟刷盤 (FlushFileBuffers / fsync)，確保二進位實體已物理落盤。
 * 3. 透過作業系統原生原子替換 API (MoveFileExW / rename) 原位原子取代目標檔案。
 * 4. 斷電或進程崩潰時，目標檔案要麼完整為舊版本、要麼完整為新版本，絕對零半殘損毀！
 *
 * @param target_path 目標檔案路徑 (強制 UTF-8 編碼)
 * @param data 寫入之二進位資料切片
 * @param mode 寫入覆寫模式
 * @return Result<void> 成功或錯誤狀態
 */
ORK_BASE_API Result<void> atomic_write(
    std::string_view target_path,
    std::span<const uint8_t> data,
    WriteMode mode = WriteMode::ReplaceExisting);

ORK_BASE_API Result<void> atomic_write(
    std::string_view target_path,
    std::string_view text,
    WriteMode mode = WriteMode::ReplaceExisting);

/**
 * @brief 快速讀取整個檔案為二進位 byte 陣列
 */
ORK_BASE_API Result<std::vector<uint8_t>> read_all_bytes(std::string_view path);

/**
 * @brief 快速讀取整個文字檔案為 UTF-8 字串
 */
ORK_BASE_API Result<std::string> read_all_text(std::string_view path);

/**
 * @brief 遞迴建立目錄 (若已存在則安全成功)
 */
ORK_BASE_API Result<void> ensure_directory(std::string_view dir_path);

/**
 * @brief 檢查路徑之檔案或目錄是否存在
 */
ORK_BASE_API bool exists(std::string_view path) noexcept;

/**
 * @brief 刪除指定檔案
 */
ORK_BASE_API Result<void> remove_file(std::string_view path);

/**
 * @brief 取得檔案大小 (位元組)
 */
ORK_BASE_API Result<uint64_t> get_file_size(std::string_view path);

}  // namespace ork::base::fs
