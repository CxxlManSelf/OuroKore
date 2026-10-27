#include "ourokore/gateway/gateway_api.h"
#include "GatewayDriver.hpp"
#include <cstring>
#include <string>

using namespace ork::gateway;

extern "C" {

ORK_GATEWAY_API int32_t ork_gateway_create(const char *cas_root_dir, ork_gateway_handle_t *out_handle) {
    if (!cas_root_dir || !out_handle) {
        return ORK_GATEWAY_ERROR_INVALID_ARG;
    }

    try {
        auto driver = new GatewayDriver(std::filesystem::path(std::string(cas_root_dir)));
        *out_handle = static_cast<ork_gateway_handle_t>(driver);
        return ORK_GATEWAY_OK;
    } catch (...) {
        return ORK_GATEWAY_ERROR_INTERNAL;
    }
}

ORK_GATEWAY_API void ork_gateway_destroy(ork_gateway_handle_t handle) {
    if (!handle) {
        return;
    }

    try {
        auto driver = static_cast<GatewayDriver *>(handle);
        delete driver;
    } catch (...) {
        // 例外完全阻絕於 DLL 內部
    }
}

ORK_GATEWAY_API int32_t ork_gateway_put_cas(
    ork_gateway_handle_t handle,
    const uint8_t *data,
    size_t size,
    char *out_cid_hex,
    size_t max_len) {
    if (!handle || !data || !out_cid_hex || max_len < 69) {
        return ORK_GATEWAY_ERROR_INVALID_ARG;
    }

    try {
        auto driver = static_cast<GatewayDriver *>(handle);
        auto res = driver->PutCas(std::span<const uint8_t>(data, size));
        if (!res) {
            // 寫入失敗
            return ORK_GATEWAY_ERROR_IO;
        }

        std::string hex_str = res->to_hex();
        if (hex_str.size() + 1 > max_len) {
            return ORK_GATEWAY_ERROR_BUFFER_TOO_SMALL;
        }

        std::memcpy(out_cid_hex, hex_str.c_str(), hex_str.size() + 1);
        return ORK_GATEWAY_OK;
    } catch (...) {
        return ORK_GATEWAY_ERROR_INTERNAL;
    }
}

ORK_GATEWAY_API int32_t ork_gateway_get_cas(
    ork_gateway_handle_t handle,
    const char *cid_hex,
    uint8_t *out_buffer,
    size_t buffer_size,
    size_t *out_actual_size) {
    if (!handle || !cid_hex || !out_actual_size) {
        return ORK_GATEWAY_ERROR_INVALID_ARG;
    }

    try {
        auto driver = static_cast<GatewayDriver *>(handle);
        auto cid_res = ork::base::CidV1::from_string(cid_hex);
        if (!cid_res) {
            return ORK_GATEWAY_ERROR_INVALID_ARG;
        }

        auto res = driver->GetCas(*cid_res);
        if (!res) {
            if (res.error().status_code() == ork::base::StatusCode::NotFound) {
                return ORK_GATEWAY_ERROR_NOT_FOUND;
            }
            return ORK_GATEWAY_ERROR_IO;
        }

        *out_actual_size = res->size();

        if (!out_buffer) {
            // 查詢長度模式
            return ORK_GATEWAY_OK;
        }

        if (buffer_size < res->size()) {
            return ORK_GATEWAY_ERROR_BUFFER_TOO_SMALL;
        }

        std::memcpy(out_buffer, res->data(), res->size());
        return ORK_GATEWAY_OK;
    } catch (...) {
        return ORK_GATEWAY_ERROR_INTERNAL;
    }
}

ORK_GATEWAY_API int32_t ork_gateway_has_cas(
    ork_gateway_handle_t handle,
    const char *cid_hex,
    int32_t *out_exists) {
    if (!handle || !cid_hex || !out_exists) {
        return ORK_GATEWAY_ERROR_INVALID_ARG;
    }

    try {
        auto driver = static_cast<GatewayDriver *>(handle);
        auto cid_res = ork::base::CidV1::from_string(cid_hex);
        if (!cid_res) {
            *out_exists = 0;
            return ORK_GATEWAY_ERROR_INVALID_ARG;
        }

        *out_exists = driver->HasCas(*cid_res) ? 1 : 0;
        return ORK_GATEWAY_OK;
    } catch (...) {
        return ORK_GATEWAY_ERROR_INTERNAL;
    }
}

ORK_GATEWAY_API int32_t ork_gateway_resolve_plugin_path(
    ork_gateway_handle_t handle,
    const char *factory_tag,
    const char *relative_module_path,
    char *out_full_path,
    size_t max_len) {
    if (!handle || !factory_tag || !out_full_path || max_len == 0) {
        return ORK_GATEWAY_ERROR_INVALID_ARG;
    }

    try {
        auto driver = static_cast<GatewayDriver *>(handle);
        std::string_view rel_path = relative_module_path ? relative_module_path : "";
        auto res = driver->ResolvePluginPath(factory_tag, rel_path);
        if (!res) {
            if (res.error().status_code() == ork::base::StatusCode::PermissionDenied) {
                return ORK_GATEWAY_ERROR_SECURITY;
            }
            return ORK_GATEWAY_ERROR_INVALID_ARG;
        }

        if (res->size() + 1 > max_len) {
            return ORK_GATEWAY_ERROR_BUFFER_TOO_SMALL;
        }

        std::memcpy(out_full_path, res->c_str(), res->size() + 1);
        return ORK_GATEWAY_OK;
    } catch (...) {
        return ORK_GATEWAY_ERROR_INTERNAL;
    }
}

} // extern "C"
