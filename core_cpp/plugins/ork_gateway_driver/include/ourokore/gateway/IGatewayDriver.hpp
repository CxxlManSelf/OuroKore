#pragma once

#include "gateway_export.h"
#include <ourokore/base/Result.hpp>
#include <ourokore/base/Sha256.hpp>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace ork::gateway {

/**
 * @brief 次世代零件網關驅動抽象介面 (IGatewayDriver)
 * 
 * 職責：
 * 1. CAS 不可變內容定址儲存與三階段物理防損毀原子寫入 (atomic_write)
 * 2. 規格藍圖去重與防竄改驗證 (Sha256 / CidV1)
 * 3. 三段式工廠提示 (Vendor/Plugin/Version) 解析與沙盒穿越防禦
 */
class ORK_GATEWAY_API IGatewayDriver {
public:
    virtual ~IGatewayDriver() = default;

    /**
     * @brief 將資料以原子防損毀方式存入 CAS，並計算返回其 CIDv1
     */
    virtual base::Result<base::CidV1> PutCas(std::span<const uint8_t> data) = 0;

    /**
     * @brief 依據 CIDv1 自 CAS 儲存庫讀取完整二進位資料
     */
    virtual base::Result<std::vector<uint8_t>> GetCas(const base::CidV1 &cid) = 0;

    /**
     * @brief 查詢指定 CID 是否已存在於 CAS 儲存庫
     */
    virtual bool HasCas(const base::CidV1 &cid) const noexcept = 0;

    /**
     * @brief 安全解析外掛模組實體檔案路徑 (具備沙盒穿越防禦)
     * @param factory_tag 模組工廠標籤 (Vendor/[SubPath/...]ModuleStem，如 "OuroKore/xxx/yyy/zzz/GoodPlugin_1.0.0")
     * @param relative_module_path 選用之自訂相對路徑。若為空，管理器會依據 ModuleStem 自行補綴平台動態庫檔名 (如 .dll / .so)
     */
    virtual base::Result<std::string> ResolvePluginPath(
        std::string_view factory_tag,
        std::string_view relative_module_path = {}) = 0;

    /**
     * @brief 取得 CAS 內容定址根目錄
     */
    virtual const std::filesystem::path &GetCasRoot() const noexcept = 0;

    /**
     * @brief 建立網關驅動實體工廠方法
     */
    static std::unique_ptr<IGatewayDriver> Create(const std::filesystem::path &cas_root_dir);
};

} // namespace ork::gateway
