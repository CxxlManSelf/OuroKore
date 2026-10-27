#include <cassert>
#include <iostream>
#include <vector>

#include "ourokore/base/Endian.hpp"
#include "ourokore/base/FileUtils.hpp"
#include "ourokore/base/PathUtils.hpp"
#include "ourokore/base/Sha256.hpp"

using namespace ork::base;

void test_endian()
{
  std::cout << "[測試 1: 二進位字節序工具 (Endian Invariants)]" << std::endl;

  // 1. 基本屬性
  assert(endian::is_little_endian() || endian::is_big_endian());
  std::cout << "  主機位元組序: " << (endian::is_little_endian() ? "Little-Endian" : "Big-Endian") << std::endl;

  // 2. 整數與浮點數讀寫
  uint8_t buffer[64] = {0};
  endian::write_uint16_le(buffer, 0x1234);
  assert(buffer[0] == 0x34 && buffer[1] == 0x12);
  assert(endian::read_uint16_le(buffer) == 0x1234);

  endian::write_uint32_le(buffer, 0x12345678);
  assert(buffer[0] == 0x78 && buffer[1] == 0x56 && buffer[2] == 0x34 && buffer[3] == 0x12);
  assert(endian::read_uint32_le(buffer) == 0x12345678);

  endian::write_uint64_le(buffer, 0x0102030405060708ULL);
  assert(buffer[0] == 0x08 && buffer[7] == 0x01);
  assert(endian::read_uint64_le(buffer) == 0x0102030405060708ULL);

  endian::write_float32_le(buffer, 123.456f);
  assert(endian::read_float32_le(buffer) == 123.456f);

  endian::write_float64_le(buffer, 987654.321);
  assert(endian::read_float64_le(buffer) == 987654.321);

  // 3. 邊界安全 Span 讀取
  std::vector<uint8_t> span_buf = {0x78, 0x56, 0x34, 0x12};
  auto read_res = endian::read_le_checked<uint32_t>(span_buf, 0);
  assert(read_res.has_value());
  assert(*read_res == 0x12345678);

  auto oob_res = endian::read_le_checked<uint64_t>(span_buf, 0);
  assert(!oob_res.has_value()); // 緩衝區僅 4 bytes，讀取 uint64 越界報錯
  std::cout << "  越界安全防禦測試通過: " << oob_res.error().message() << std::endl;

  std::cout << "  Endian 測試全數通過！\n" << std::endl;
}

void test_sha256_and_cid()
{
  std::cout << "[測試 2: 純 CPU SHA-256 與 CIDv1 Multihash]" << std::endl;

  // 1. RFC 6234 空字串測試向量
  std::string empty_hex = crypto::Sha256::compute_hex("");
  assert(empty_hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // 2. RFC 6234 "abc" 測試向量
  std::string abc_hex = crypto::Sha256::compute_hex("abc");
  assert(abc_hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  // 3. 增量計算測試
  crypto::Sha256 ctx;
  ctx.update("a");
  ctx.update("b");
  ctx.update("c");
  auto incremental_digest = ctx.finalize();
  assert(crypto::Sha256::to_hex(incremental_digest) == abc_hex);

  // 4. CIDv1 Multihash 容器測試
  auto cid = crypto::CidV1::from_data("abc");
  assert(cid.raw_bytes[0] == 0x12); // sha2-256
  assert(cid.raw_bytes[1] == 0x20); // 32 bytes length
  assert(cid.bytes().size() == 34);

  std::string cid_str = cid.to_string();
  assert(cid_str.starts_with("cid:sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  // 5. CIDv1 反序列化解析
  auto parsed_cid = crypto::CidV1::from_bytes(cid.bytes());
  assert(parsed_cid.has_value());
  assert(*parsed_cid == cid);

  uint8_t invalid_multihash[34] = {0x00, 0x20};
  auto bad_cid = crypto::CidV1::from_bytes(invalid_multihash);
  assert(!bad_cid.has_value());

  std::cout << "  CID 格式字串: " << cid_str << std::endl;
  std::cout << "  SHA-256 & CIDv1 測試全數通過！\n" << std::endl;
}

void test_file_utils()
{
  std::cout << "[測試 3: 原子防損毀檔案操作 (FileUtils)]" << std::endl;

  std::string test_dir = "test_atomic_scratch";
  auto dir_res = fs::ensure_directory(test_dir);
  assert(dir_res.has_value());

  std::string file_path = test_dir + "/blueprint.orkt";

  // 1. 初次原子寫入
  std::string content1 = "OuroKore Blueprint Snapshot v1";
  auto write_res = fs::atomic_write(file_path, content1);
  assert(write_res.has_value());
  assert(fs::exists(file_path));

  // 2. 讀取驗證
  auto read_res = fs::read_all_text(file_path);
  assert(read_res.has_value());
  assert(*read_res == content1);

  // 3. 檔案大小驗證
  auto size_res = fs::get_file_size(file_path);
  assert(size_res.has_value());
  assert(*size_res == content1.size());

  // 4. 原子覆寫 (ReplaceExisting)
  std::string content2 = "OuroKore Blueprint Snapshot v2 - Updated Content";
  auto rewrite_res = fs::atomic_write(file_path, content2, fs::WriteMode::ReplaceExisting);
  assert(rewrite_res.has_value());

  auto read2_res = fs::read_all_text(file_path);
  assert(read2_res.has_value());
  assert(*read2_res == content2);

  // 5. 不可覆寫模式 (FailIfExists)
  auto fail_res = fs::atomic_write(file_path, "Fail Content", fs::WriteMode::FailIfExists);
  assert(!fail_res.has_value()); // 目標存在，必須報錯阻止覆寫
  std::cout << "  不可覆寫防線攔截成功: " << fail_res.error().message() << std::endl;

  // 6. 清理檔案
  auto rm_res = fs::remove_file(file_path);
  assert(rm_res.has_value());
  assert(!fs::exists(file_path));

  std::cout << "  FileUtils 測試全數通過！\n" << std::endl;
}

void test_path_utils()
{
  std::cout << "[測試 4: 路徑安全與階層式工廠標籤解析 (PathUtils)]" << std::endl;

  // 1. 多段式階層標籤解析 (OuroKore/xxx/yyy/zzz/GoodPlugin_1.0.0)
  auto tag_res1 = path::parse_factory_tag("OuroKore/xxx/yyy/zzz/GoodPlugin_1.0.0");
  assert(tag_res1.has_value());
  assert(tag_res1->vendor == "OuroKore");
  assert(tag_res1->subpath == "xxx/yyy/zzz");
  assert(tag_res1->module_stem == "GoodPlugin_1.0.0");
  assert(tag_res1->get_directory() == "OuroKore/xxx/yyy/zzz");
  assert(tag_res1->to_string() == "OuroKore/xxx/yyy/zzz/GoodPlugin_1.0.0");

  // 兩段式標籤 (無中間路徑: Vendor/ModuleStem)
  auto tag_res2 = path::parse_factory_tag("Epic/GoodPlugin_1.0.0");
  assert(tag_res2.has_value());
  assert(tag_res2->vendor == "Epic");
  assert(tag_res2->subpath.empty());
  assert(tag_res2->module_stem == "GoodPlugin_1.0.0");
  assert(tag_res2->get_directory() == "Epic");
  assert(tag_res2->to_string() == "Epic/GoodPlugin_1.0.0");

  // 三段式標籤 (相容 Vendor/SubPath/ModuleStem)
  auto tag_res3 = path::parse_factory_tag("Epic/WeaponPack/v1.2.0");
  assert(tag_res3.has_value());
  assert(tag_res3->vendor == "Epic");
  assert(tag_res3->subpath == "WeaponPack");
  assert(tag_res3->module_stem == "v1.2.0");
  assert(tag_res3->get_directory() == "Epic/WeaponPack");
  assert(tag_res3->to_string() == "Epic/WeaponPack/v1.2.0");

  // 驗證自適應平台動態庫檔名工具
#if defined(__MINGW32__) || defined(__MINGW64__)
  assert(path::get_platform_module_filename("GoodPlugin_1.0.0") == "libGoodPlugin_1.0.0.dll");
#elif defined(_WIN32)
  assert(path::get_platform_module_filename("GoodPlugin_1.0.0") == "GoodPlugin_1.0.0.dll");
#elif defined(__APPLE__)
  assert(path::get_platform_module_filename("GoodPlugin_1.0.0") == "libGoodPlugin_1.0.0.dylib");
#else
  assert(path::get_platform_module_filename("GoodPlugin_1.0.0") == "libGoodPlugin_1.0.0.so");
#endif

  // 非法標籤測試
  assert(!path::parse_factory_tag("Epic").has_value()); // 僅一段 (缺少主體名稱)
  assert(!path::parse_factory_tag("Epic/Weapon Pack/v1").has_value()); // 包含空白
  assert(!path::parse_factory_tag("Epic/../v1").has_value()); // 包含遍歷符
  assert(!path::parse_factory_tag("").has_value()); // 空字串

  // 2. 路徑正規化
  std::string norm1 = path::normalize("a//b/./c/../d");
  assert(norm1 == "a/b/d");

  std::string norm2 = path::normalize("C:\\MySrc\\OuroKore\\build\\..\\specs");
  assert(norm2 == "C:/MySrc/OuroKore/specs");

  // 3. 路徑穿越安全性檢測
  assert(path::is_safe_relative_path("assets/blueprints/actor.orkt"));
  assert(path::is_safe_relative_path("a/b/../c"));
  assert(!path::is_safe_relative_path("../escape_sandbox")); // 直接跳出
  assert(!path::is_safe_relative_path("a/../../escape_sandbox")); // 深度小於 0
  assert(!path::is_safe_relative_path("/root/abs_path")); // 絕對路徑

  // 4. 安全路徑拼接
  auto safe_res = path::combine_safe("c:/MySandbox", "plugins/module.dll");
  assert(safe_res.has_value());
  assert(*safe_res == "c:/MySandbox/plugins/module.dll");

  auto hack_res = path::combine_safe("c:/MySandbox", "../windows/system32/cmd.exe");
  assert(!hack_res.has_value()); // 攔截目錄穿越攻擊
  std::cout << "  路徑穿越攻擊攔截成功: " << hack_res.error().message() << std::endl;

  // 5. 檔名與副檔名提取
  assert(path::get_filename("c:/foo/bar/archive.orkt") == "archive.orkt");
  assert(path::get_extension("c:/foo/bar/archive.orkt") == ".orkt");
  assert(path::get_parent_path("c:/foo/bar/archive.orkt") == "c:/foo/bar");

  std::cout << "  PathUtils 測試全數通過！\n" << std::endl;
}

int main()
{
  std::cout << "==================================================" << std::endl;
  std::cout << "   OrkGatewayDriver 專用純通用工具套件驗證測試   " << std::endl;
  std::cout << "==================================================" << std::endl;

  try
  {
    test_endian();
    test_sha256_and_cid();
    test_file_utils();
    test_path_utils();

    std::cout << "==================================================" << std::endl;
    std::cout << "  🎉 所有 4 項純通用工具套件測試 100% 順利通過！  " << std::endl;
    std::cout << "==================================================" << std::endl;
    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "❌ 測試異常失敗: " << e.what() << std::endl;
    return 1;
  }
}
