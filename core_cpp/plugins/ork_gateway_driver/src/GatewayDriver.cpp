#include "GatewayDriver.hpp"
#include <ourokore/base/PluginHeap.hpp>
#include <mutex>

ORK_ENABLE_PLUGIN_HEAP_TRACKING()

namespace ork::gateway {

std::unique_ptr<IGatewayDriver> IGatewayDriver::Create(const std::filesystem::path &cas_root_dir) {
    return std::make_unique<GatewayDriver>(cas_root_dir);
}

GatewayDriver::GatewayDriver(std::filesystem::path cas_root_dir)
    : m_cas_root(std::move(cas_root_dir)) {
    // 確保 CAS 根目錄存在
    auto ensure_res = base::fs::ensure_directory(m_cas_root.string());
    (void)ensure_res;
}

std::filesystem::path GatewayDriver::GetCasFilePath(const base::CidV1 &cid) const {
    // 採用純十六進位字串作為合法檔名 (.bin)，避免 Windows 檔名冒號 (:) 衝突
    return m_cas_root / (cid.to_hex() + ".bin");
}

base::Result<base::CidV1> GatewayDriver::PutCas(std::span<const uint8_t> data) {
    auto cid = base::Sha256::compute_cid(data);
    auto target_file = GetCasFilePath(cid);
    std::string target_path_str = target_file.string();

    {
        std::shared_lock lock(m_mutex);
        if (base::fs::exists(target_path_str)) {
            // CAS 去重公理：相同 Hash 即相同資料，無需重複落盤
            return cid;
        }
    }

    std::unique_lock lock(m_mutex);
    if (base::fs::exists(target_path_str)) {
        return cid;
    }

    // 三階段物理原子防損毀寫入
    auto write_res = base::fs::atomic_write(target_path_str, data);
    if (!write_res) {
        return base::unexpected(write_res.error());
    }

    return cid;
}

base::Result<std::vector<uint8_t>> GatewayDriver::GetCas(const base::CidV1 &cid) {
    auto target_file = GetCasFilePath(cid);
    std::string target_path_str = target_file.string();

    std::shared_lock lock(m_mutex);
    if (!base::fs::exists(target_path_str)) {
        return base::unexpected(base::Status::NotFound("CAS 儲存庫中找不到指定 CID 之內容"));
    }

    auto read_res = base::fs::read_all_bytes(target_path_str);
    if (!read_res) {
        return base::unexpected(read_res.error());
    }

    // 完整性校驗：防範磁碟損壞或外部竄改
    auto actual_cid = base::Sha256::compute_cid(*read_res);
    if (actual_cid != cid) {
        return base::unexpected(base::Status::DataLoss("CAS 資料損毀：實際內容雜湊與 CID 不符"));
    }

    return read_res;
}

bool GatewayDriver::HasCas(const base::CidV1 &cid) const noexcept {
    auto target_file = GetCasFilePath(cid);
    std::shared_lock lock(m_mutex);
    return base::fs::exists(target_file.string());
}

base::Result<std::string> GatewayDriver::ResolvePluginPath(
    std::string_view factory_tag,
    std::string_view relative_module_path) {
    // 1. 三段式標籤嚴格格式驗證
    auto tag_res = base::path::parse_factory_tag(factory_tag);
    if (!tag_res) {
        return base::unexpected(tag_res.error());
    }

    // 2. 目錄穿越攻擊防禦
    if (!base::path::is_safe_relative_path(relative_module_path)) {
        return base::unexpected(base::Status::PermissionDenied("相對模組路徑包含非法穿越或絕對路徑字元"));
    }

    // 3. 組合階層式路徑: Vendor/Plugin/Version/relative_module_path
    std::string tag_dir = tag_res->vendor + "/" + tag_res->plugin + "/" + tag_res->version;
    auto combined_res = base::path::combine_safe(tag_dir, relative_module_path);
    if (!combined_res) {
        return base::unexpected(combined_res.error());
    }

    return base::path::normalize(*combined_res);
}

const std::filesystem::path &GatewayDriver::GetCasRoot() const noexcept {
    return m_cas_root;
}

} // namespace ork::gateway
