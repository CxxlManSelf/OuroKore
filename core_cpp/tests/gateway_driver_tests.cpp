#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ourokore/base/DynamicLibrary.hpp"
#include "ourokore/base/FileUtils.hpp"

// 定義 C ABI 函式指標型別
using fn_ork_gateway_create = int32_t (*)(const char *, void **);
using fn_ork_gateway_destroy = void (*)(void *);
using fn_ork_gateway_put_cas = int32_t (*)(void *, const uint8_t *, size_t, char *, size_t);
using fn_ork_gateway_get_cas = int32_t (*)(void *, const char *, uint8_t *, size_t, size_t *);
using fn_ork_gateway_has_cas = int32_t (*)(void *, const char *, int32_t *);
using fn_ork_gateway_resolve_plugin_path = int32_t (*)(void *, const char *, const char *, char *, size_t);

void test_gateway_driver_plugin()
{
  std::cout << "[測試 1: 動態載入 OrkGatewayDriver MODULE 外掛]" << std::endl;

#if defined(_DEBUG) || !defined(NDEBUG)
  std::string plugin_filename = "OrkGatewayDriver_d.dll";
#else
  std::string plugin_filename = "OrkGatewayDriver.dll";
#endif

  std::vector<std::filesystem::path> candidate_paths = {
      std::filesystem::path("plugins") / plugin_filename,
      std::filesystem::path("build/bin/plugins") / plugin_filename,
      std::filesystem::path("bin/plugins") / plugin_filename
  };

  std::string plugin_path;
  for (const auto &p : candidate_paths) {
    if (std::filesystem::exists(p)) {
      plugin_path = p.string();
      break;
    }
  }

  if (plugin_path.empty()) {
    plugin_path = (std::filesystem::path("plugins") / plugin_filename).string();
  }

  auto lib_res = ork::DynamicLibrary::try_load(std::string_view(plugin_path));
  if (!lib_res)
  {
    std::cerr << "動態載入外掛失敗: " << lib_res.error().message() << std::endl;
    assert(false);
  }
  auto &lib = *lib_res;
  std::cout << "  外掛載入成功: " << plugin_path << std::endl;

  // 取得 C ABI 符號
  auto fn_create = lib.get_symbol<fn_ork_gateway_create>("ork_gateway_create");
  auto fn_destroy = lib.get_symbol<fn_ork_gateway_destroy>("ork_gateway_destroy");
  auto fn_put = lib.get_symbol<fn_ork_gateway_put_cas>("ork_gateway_put_cas");
  auto fn_get = lib.get_symbol<fn_ork_gateway_get_cas>("ork_gateway_get_cas");
  auto fn_has = lib.get_symbol<fn_ork_gateway_has_cas>("ork_gateway_has_cas");
  auto fn_resolve = lib.get_symbol<fn_ork_gateway_resolve_plugin_path>("ork_gateway_resolve_plugin_path");

  assert(fn_create != nullptr);
  assert(fn_destroy != nullptr);
  assert(fn_put != nullptr);
  assert(fn_get != nullptr);
  assert(fn_has != nullptr);
  assert(fn_resolve != nullptr);

  std::string test_cas_dir = "test_gateway_cas_repo";
  (void)ork::base::fs::remove_file(test_cas_dir); // 清理

  std::cout << "[測試 2: 網關實體生命週期與 CAS 落盤]" << std::endl;
  void *gateway = nullptr;
  int32_t rc = fn_create(test_cas_dir.c_str(), &gateway);
  assert(rc == 0);
  assert(gateway != nullptr);

  // 1. CAS Put
  std::string payload = "OuroKore Weapon Specification Part Blueprint v1.0";
  char cid_hex[128] = {0};
  rc = fn_put(gateway, reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), cid_hex, sizeof(cid_hex));
  if (rc != 0) {
    std::cerr << "  fn_put failed with rc = " << rc << std::endl;
  }
  assert(rc == 0);
  assert(std::strlen(cid_hex) == 68);
  std::cout << "  CAS 寫入成功，產出 CIDv1: " << cid_hex << std::endl;

  // 2. CAS Has
  int32_t exists = 0;
  rc = fn_has(gateway, cid_hex, &exists);
  assert(rc == 0 && exists == 1);

  // 3. CAS Get (長度查詢模式)
  size_t actual_size = 0;
  rc = fn_get(gateway, cid_hex, nullptr, 0, &actual_size);
  assert(rc == 0);
  assert(actual_size == payload.size());

  // 4. CAS Get (二進位讀出)
  std::vector<uint8_t> read_buf(actual_size);
  rc = fn_get(gateway, cid_hex, read_buf.data(), read_buf.size(), &actual_size);
  assert(rc == 0);
  std::string read_str(reinterpret_cast<const char *>(read_buf.data()), actual_size);
  assert(read_str == payload);
  std::cout << "  CAS 完整性讀取比對成功！" << std::endl;

  // 5. CAS 不存在查詢
  char fake_cid[69];
  std::memset(fake_cid, '0', 68);
  fake_cid[0] = '1';
  fake_cid[1] = '2';
  fake_cid[2] = '2';
  fake_cid[3] = '0';
  fake_cid[68] = '\0';
  exists = 1;
  rc = fn_has(gateway, fake_cid, &exists);
  assert(rc == 0 && exists == 0);

  rc = fn_get(gateway, fake_cid, read_buf.data(), read_buf.size(), &actual_size);
  assert(rc == -3); // ORK_GATEWAY_ERROR_NOT_FOUND

  std::cout << "[測試 3: 三段式標籤工廠與目錄穿越防禦]" << std::endl;
  char full_path[256] = {0};
  // 合法解析
  rc = fn_resolve(gateway, "EpicGames/ChaosEngine/v1.2.0", "bin/physics.dll", full_path, sizeof(full_path));
  assert(rc == 0);
  assert(std::string(full_path) == "EpicGames/ChaosEngine/v1.2.0/bin/physics.dll");
  std::cout << "  合法標籤解析成功: " << full_path << std::endl;

  // 惡意路徑穿越攔截
  rc = fn_resolve(gateway, "EpicGames/ChaosEngine/v1.2.0", "../../Windows/System32/evil.dll", full_path, sizeof(full_path));
  assert(rc == -4); // ORK_GATEWAY_ERROR_SECURITY
  std::cout << "  目錄穿越攻擊攔截成功 (rc = -4)！" << std::endl;

  // 銷毀網關實體
  fn_destroy(gateway);
  std::cout << "  網關實體已安全銷毀！" << std::endl;

  // 清理測試檔案
  std::filesystem::remove_all(test_cas_dir);
}

int main()
{
  std::cout << "========================================" << std::endl;
  std::cout << " OrkGatewayDriver 外掛整合單元測試" << std::endl;
  std::cout << "========================================" << std::endl;

  test_gateway_driver_plugin();

  std::cout << "\n>>> OrkGatewayDriver 所有測試 100% 通過！<<<" << std::endl;
  return 0;
}
