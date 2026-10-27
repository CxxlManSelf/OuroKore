#pragma once

#include "ourokore/gateway/IGatewayDriver.hpp"
#include <ourokore/base/PluginHeap.hpp>
#include <ourokore/base/FileUtils.hpp>
#include <ourokore/base/PathUtils.hpp>
#include <ourokore/base/Sha256.hpp>
#include <shared_mutex>

namespace ork::gateway {

class GatewayDriver final : public IGatewayDriver {
public:
    explicit GatewayDriver(std::filesystem::path cas_root_dir);
    ~GatewayDriver() override = default;

    base::Result<base::CidV1> PutCas(std::span<const uint8_t> data) override;
    base::Result<std::vector<uint8_t>> GetCas(const base::CidV1 &cid) override;
    bool HasCas(const base::CidV1 &cid) const noexcept override;
    base::Result<std::string> ResolvePluginPath(
        std::string_view factory_tag,
        std::string_view relative_module_path = {}) override;
    const std::filesystem::path &GetCasRoot() const noexcept override;

private:
    std::filesystem::path GetCasFilePath(const base::CidV1 &cid) const;

    std::filesystem::path m_cas_root;
    mutable std::shared_mutex m_mutex;
};

} // namespace ork::gateway
