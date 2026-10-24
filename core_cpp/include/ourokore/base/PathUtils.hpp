#pragma once

#include <string>
#include <string_view>

#include "ourokore/base/Result.hpp"
#include "ourokore/base/export.h"

namespace ork::base::path
{

/**
 * @brief 次世代工廠尋路三段式標籤 (Factory Tag)
 * 格式: "Vendor/Plugin/Version" (如 "Epic/WeaponPack/v1.2.0")
 */
struct ORK_BASE_API FactoryTag
{
  std::string vendor;
  std::string plugin;
  std::string version;

  std::string to_string() const;
  bool operator==(const FactoryTag &other) const noexcept = default;
};

/**
 * @brief 解析三段式工廠標籤 "Vendor/Plugin/Version"
 * 嚴格校驗各段字元集 (僅允許英數字、底線、減號、點)，防範注入與穿越
 */
ORK_BASE_API Result<FactoryTag> parse_factory_tag(std::string_view tag_str);

/**
 * @brief 將路徑分隔符統一為正斜線 '/' 並標準化
 * 1. 替換所有 '\\' 為 '/'
 * 2. 折疊連續斜線 "//" 為 "/"
 * 3. 移除多餘的 "/./"
 */
ORK_BASE_API std::string normalize(std::string_view path);

/**
 * @brief 檢測相對路徑是否具備潛在之路徑穿越攻擊 (Path Traversal / Directory Escape)
 * @return true 代表路徑安全（未包含跳出當前目錄之 "../"），false 代表具有惡意穿越風險
 */
ORK_BASE_API bool is_safe_relative_path(std::string_view relative_path) noexcept;

/**
 * @brief 安全拼接基礎目錄與相對路徑 (防目錄遍歷)
 * @param base_dir 基準安全目錄
 * @param relative_path 外部傳入之相對子路徑
 * @return Result<std::string> 成功返回規範化之絕對/標準路徑，若檢測到穿越攻擊則失敗早退
 */
ORK_BASE_API Result<std::string> combine_safe(
    std::string_view base_dir,
    std::string_view relative_path);

/**
 * @brief 提取檔案路徑之副檔名 (包含點號，如 ".orkt")
 */
ORK_BASE_API std::string_view get_extension(std::string_view path) noexcept;

/**
 * @brief 提取路徑之純檔名部分 (不含目錄)
 */
ORK_BASE_API std::string_view get_filename(std::string_view path) noexcept;

/**
 * @brief 提取父目錄路徑
 */
ORK_BASE_API std::string_view get_parent_path(std::string_view path) noexcept;

}  // namespace ork::base::path
