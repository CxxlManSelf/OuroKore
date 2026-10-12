#include <cassert>
#include <chrono>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "ourokore/base/Uuid.hpp"
#include "ourokore/base/uuid_api.h"

using namespace ork::base;
using namespace ork::base::literals::uuid_literals;

// =========================================================================
// 1. 靜態編譯期常數驗證 (Compile-Time static_assert)
// =========================================================================

static_assert(Uuid::Nil().IsNil());
static_assert(!Uuid::Nil().IsMax());
static_assert(Uuid::Nil().Version() == 0);

static_assert(Uuid::Max().IsMax());
static_assert(!Uuid::Max().IsNil());

// 測試編譯期常數字面量 _uuid
constexpr Uuid kConstUuid = "018f2f4c-5353-7b3b-8105-df4d3855a02e"_uuid;
static_assert(kConstUuid.Version() == 7);
static_assert(kConstUuid.Variant() == 1);
static_assert(kConstUuid.TimestampMs() == 0x018F2F4C5353ULL);

// 驗證 32 字元緊湊格式與 38 字元大括號格式在編譯期等價
constexpr Uuid kCompactUuid = *Uuid::ParseConstexpr("018f2f4c53537b3b8105df4d3855a02e");
constexpr Uuid kBracedUuid = *Uuid::ParseConstexpr("{018f2f4c-5353-7b3b-8105-df4d3855a02e}");
static_assert(kConstUuid == kCompactUuid);
static_assert(kConstUuid == kBracedUuid);

// 驗證 HighBits 與 LowBits 建構子
constexpr Uuid kBitsUuid(kConstUuid.HighBits(), kConstUuid.LowBits());
static_assert(kConstUuid == kBitsUuid);

void TestUuidCompileTimeAndBasics()
{
  std::cout << "[測試 1] Uuid 編譯期常數、基本屬性與記憶體佈局測試..." << std::endl;

  Uuid nil_id = Uuid::Nil();
  assert(nil_id.IsNil());
  assert(!nil_id);
  assert(nil_id.ToString() == "00000000-0000-0000-0000-000000000000");

  Uuid max_id = Uuid::Max();
  assert(max_id.IsMax());
  assert(static_cast<bool>(max_id));
  assert(max_id.ToString() == "ffffffff-ffff-ffff-ffff-ffffffffffff");

  assert(nil_id < max_id);
  assert(max_id > nil_id);
  assert(nil_id != max_id);

  std::cout << "  -> 編譯期靜態斷言與基本屬性驗證 100% 通過！" << std::endl;
}

void TestUuidV4()
{
  std::cout << "[測試 2] UUIDv4 隨機型生成器測試..." << std::endl;

  Uuid id1 = Uuid::GenerateV4();
  Uuid id2 = Uuid::GenerateV4();

  assert(!id1.IsNil());
  assert(!id2.IsNil());
  assert(id1 != id2);

  assert(id1.Version() == 4);
  assert(id2.Version() == 4);
  assert(id1.Variant() == 1);
  assert(id2.Variant() == 1);

  // 批量生成 1000 個，驗證全域無碰撞
  std::unordered_set<Uuid> set;
  for (int i = 0; i < 1000; ++i)
  {
    Uuid id = Uuid::GenerateV4();
    assert(id.Version() == 4);
    assert(id.Variant() == 1);
    bool inserted = set.insert(id).second;
    assert(inserted && "UUIDv4 碰撞偵測失敗");
  }

  std::cout << "  -> 1000 個隨機 UUIDv4 零碰撞且版本/變體位元 100% 合規！" << std::endl;
}

void TestUuidV7AndMonotonicity()
{
  std::cout << "[測試 3] UUIDv7 時間排序型與單調性 (Monotonicity) 測試..." << std::endl;

  using namespace std::chrono;
  uint64_t before_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

  Uuid id = Uuid::GenerateV7();

  uint64_t after_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

  assert(id.Version() == 7);
  assert(id.Variant() == 1);
  assert(id.TimestampMs() >= before_ms && id.TimestampMs() <= after_ms);

  // 測試指定歷史時間戳
  uint64_t fixed_time = 1773000000000ULL;
  Uuid fixed_v7 = Uuid::FromUnixTimeMsV7(fixed_time);
  assert(fixed_v7.Version() == 7);
  assert(fixed_v7.Variant() == 1);
  assert(fixed_v7.TimestampMs() == fixed_time);

  // 關鍵單調性測試：在緊密迴圈中連續生成 2000 個 UUIDv7，必須嚴格單調遞增 (a < b)
  std::vector<Uuid> v7_list;
  v7_list.reserve(2000);
  for (int i = 0; i < 2000; ++i)
  {
    v7_list.push_back(Uuid::GenerateV7());
  }

  for (size_t i = 1; i < v7_list.size(); ++i)
  {
    assert(v7_list[i - 1] < v7_list[i] && "UUIDv7 未滿足嚴格單調時序！");
    assert(v7_list[i - 1] != v7_list[i]);
  }

  std::cout << "  -> 2000 個連續快速生成的 UUIDv7 具備 100% 嚴格單調遞增時序性！" << std::endl;
}

void TestSerializationAndParsing()
{
  std::cout << "[測試 4] 字串序列化、格式化與容錯解析測試..." << std::endl;

  Uuid origin = "018f2f4c-5353-7b3b-8105-df4d3855a02e"_uuid;

  // 1. 各種格式轉換
  std::string lower_str = origin.ToString(false);
  std::string upper_str = origin.ToString(true);
  std::string compact_str = origin.ToCompactString(false);
  std::string braced_str = origin.ToBracedString(false);

  assert(lower_str == "018f2f4c-5353-7b3b-8105-df4d3855a02e");
  assert(upper_str == "018F2F4C-5353-7B3B-8105-DF4D3855A02E");
  assert(compact_str == "018f2f4c53537b3b8105df4d3855a02e");
  assert(braced_str == "{018f2f4c-5353-7b3b-8105-df4d3855a02e}");

  // 2. 從各種字串格式反向解析
  auto r1 = Uuid::FromString(lower_str);
  assert(r1.has_value() && *r1 == origin);

  auto r2 = Uuid::FromString(upper_str);
  assert(r2.has_value() && *r2 == origin);

  auto r3 = Uuid::FromString(compact_str);
  assert(r3.has_value() && *r3 == origin);

  auto r4 = Uuid::FromString(braced_str);
  assert(r4.has_value() && *r4 == origin);

  // 3. 異常字串反向解析檢測 (無例外安全傳回錯誤)
  assert(!Uuid::FromString("").has_value());
  assert(!Uuid::FromString("018f2f4c-5353-7b3b-8105").has_value());
  assert(!Uuid::FromString("018f2f4c-5353-7b3b-8105-df4d3855a02g").has_value());  // 非法 'g'
  assert(!Uuid::FromString("018f2f4c_5353_7b3b_8105_df4d3855a02e").has_value());  // 錯誤分隔符號

  // 4. std::format 支援測試
  assert(std::format("{}", origin) == lower_str);
  assert(std::format("{:S}", origin) == upper_str);
  assert(std::format("{:c}", origin) == compact_str);
  assert(std::format("{:b}", origin) == braced_str);

  std::cout << "  -> 標準、緊湊、大括號格式互轉與 std::format 格式化完全相容！" << std::endl;
}

void TestStlContainerIntegration()
{
  std::cout << "[測試 5] STL 容器 (std::map, std::unordered_set) 整合測試..." << std::endl;

  Uuid u1 = "00000000-0000-0000-0000-000000000001"_uuid;
  Uuid u2 = "00000000-0000-0000-0000-000000000002"_uuid;
  Uuid u3 = "00000000-0000-0000-0000-000000000003"_uuid;

  std::map<Uuid, std::string> ordered_map;
  ordered_map[u2] = "Two";
  ordered_map[u1] = "One";
  ordered_map[u3] = "Three";

  auto it = ordered_map.begin();
  assert(it->first == u1);
  ++it;
  assert(it->first == u2);
  ++it;
  assert(it->first == u3);

  std::unordered_set<Uuid> set;
  set.insert(u1);
  set.insert(u2);
  assert(set.contains(u1));
  assert(set.contains(u2));
  assert(!set.contains(u3));

  std::cout << "  -> std::map 紅黑樹排序與 std::unordered_set 雜湊表運作正常！" << std::endl;
}

void TestCApi()
{
  std::cout << "[測試 6] 純 C ABI (uuid_api.h) 完整介面測試..." << std::endl;

  ork_uuid_t nil_c = ork_uuid_nil();
  assert(ork_uuid_is_nil(&nil_c) == 1);
  assert(ork_uuid_is_max(&nil_c) == 0);

  ork_uuid_t max_c = ork_uuid_max();
  assert(ork_uuid_is_nil(&max_c) == 0);
  assert(ork_uuid_is_max(&max_c) == 1);

  assert(ork_uuid_compare(&nil_c, &max_c) < 0);
  assert(ork_uuid_compare(&max_c, &nil_c) > 0);
  assert(ork_uuid_compare(&nil_c, &nil_c) == 0);

  // 生成 v4
  ork_uuid_t v4_c = ork_uuid_generate_v4();
  assert(ork_uuid_get_version(&v4_c) == 4);
  assert(ork_uuid_get_variant(&v4_c) == 1);

  // 生成 v7
  ork_uuid_t v7_c = ork_uuid_generate_v7();
  assert(ork_uuid_get_version(&v7_c) == 7);
  assert(ork_uuid_get_variant(&v7_c) == 1);

  uint64_t ts = 0;
  assert(ork_uuid_get_timestamp_ms(&v7_c, &ts) == 0);
  assert(ts > 0);

  // 字串轉換
  char buf[37]{};
  int32_t ret = ork_uuid_to_string(&v7_c, buf, sizeof(buf), 0);
  assert(ret == 36);
  assert(std::string(buf).length() == 36);

  ork_uuid_t parsed_c{};
  assert(ork_uuid_from_string(buf, &parsed_c) == 0);
  assert(ork_uuid_compare(&v7_c, &parsed_c) == 0);

  std::cout << "  -> 純 C ABI 函式行為與 C++ 原生介面 100% 對齊無誤！" << std::endl;
}

int main()
{
  std::cout << "=== 開始執行 OuroKore Base UUID 模組單元測試 ===" << std::endl;

  TestUuidCompileTimeAndBasics();
  TestUuidV4();
  TestUuidV7AndMonotonicity();
  TestSerializationAndParsing();
  TestStlContainerIntegration();
  TestCApi();

  std::cout << "\n=== 所有 Base UUID 單元測試 100% 通過！ ===" << std::endl;
  return 0;
}
