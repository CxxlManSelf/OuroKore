#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "ourokore/base/Hash.hpp"

using namespace ork::base;
using namespace ork::base::literals::hash_literals;

// 1. 靜態編譯期常數驗證 (Compile-Time static_assert)
static_assert(Fnv1a32("") == 2166136261U);
static_assert(Fnv1a64("") == 14695981039346656037ULL);
static_assert(Crc32("") == 0x00000000U);
static_assert(Crc32("123456789") == 0xCBF43926U);  // IEEE 802.3 Standard Vector

static_assert("123456789"_crc32 == 0xCBF43926U);
static_assert(""_fnv64 == 14695981039346656037ULL);

void TestFnv1a()
{
  std::cout << "[測試 1] FNV-1a 32-bit / 64-bit 雜湊與字面量測試..." << std::endl;

  std::string text = "OuroKore_Fast_Hash_2026";
  uint32_t hash32 = Fnv1a32(text);
  uint64_t hash64 = Fnv1a64(text);

  assert(hash32 != 0);
  assert(hash64 != 0);

  // 驗證 string_view, const void*, span 介面一致性
  assert(hash64 == Fnv1a64(text.data(), text.size()));
  std::vector<uint8_t> byte_vec(text.begin(), text.end());
  assert(hash64 == Fnv1a64(std::span<const uint8_t>(byte_vec)));

  // 驗證字面量運算子
  constexpr uint64_t lit64 = "OuroKore_Fast_Hash_2026"_fnv64;
  constexpr uint32_t lit32 = "OuroKore_Fast_Hash_2026"_fnv32;
  assert(hash64 == lit64);
  assert(hash32 == lit32);

  // 驗證不同字串產生不同雜湊值
  assert(Fnv1a64("Player") != Fnv1a64("Monster"));

  std::cout << "  -> FNV-1a 編譯期常數、執行期運算與多種介面 100% 吻合！" << std::endl;
}

void TestCrc32()
{
  std::cout << "[測試 2] CRC32 標準循環冗餘校驗碼測試..." << std::endl;

  // 標準向量 "123456789" -> 0xCBF43926
  std::string std_vector = "123456789";
  uint32_t crc = Crc32(std_vector);
  assert(crc == 0xCBF43926U);

  // 驗證空資料
  assert(Crc32("") == 0x00000000U);

  // 驗證 span 介面與字面量
  std::vector<uint8_t> bytes(std_vector.begin(), std_vector.end());
  assert(Crc32(std::span<const uint8_t>(bytes)) == 0xCBF43926U);
  assert("123456789"_crc32 == 0xCBF43926U);

  // 驗證位元竄改敏感度（雪崩防禦）
  std::string altered = "123456788";
  assert(Crc32(altered) != crc);

  std::cout << "  -> CRC32 IEEE 802.3 標準測試向量完全相符，防竄改靈敏度正確！" << std::endl;
}

void TestMurmurHash3()
{
  std::cout << "[測試 3] MurmurHash3 32-bit 演算法測試..." << std::endl;

  std::string data = "The quick brown fox jumps over the lazy dog";
  uint32_t mm_default = MurmurHash3_32(data);
  uint32_t mm_seeded = MurmurHash3_32(data, 1337);

  assert(mm_default != 0);
  assert(mm_seeded != 0);
  assert(mm_default != mm_seeded);  // Seed 必須有效影響雜湊輸出

  // 驗證字面量
  constexpr uint32_t lit_mm = "The quick brown fox jumps over the lazy dog"_mm32;
  assert(mm_default == lit_mm);

  // 驗證 Tail 長度為 1, 2, 3 位元組時無越界崩潰
  assert(MurmurHash3_32("1") != 0);
  assert(MurmurHash3_32("12") != 0);
  assert(MurmurHash3_32("123") != 0);
  assert(MurmurHash3_32("1234") != 0);

  std::cout << "  -> MurmurHash3 編譯期常數計算與執行期雜湊運算通過！" << std::endl;
}

void TestHashCombine()
{
  std::cout << "[測試 4] HashCombine 組合雜湊測試..." << std::endl;

  size_t seed1 = 0;
  HashCombine(seed1, 100);
  size_t first_step = seed1;
  assert(first_step != 0);

  HashCombine(seed1, 200);
  assert(seed1 != first_step);

  // 驗證順序敏感性（(A, B) != (B, A)）
  size_t seed2 = 0;
  HashCombine(seed2, 200);
  HashCombine(seed2, 100);
  assert(seed1 != seed2);

  // 驗證可變參數重載 (Variadic Combine)
  size_t variadic_seed = 0;
  HashCombine(variadic_seed, 100, 200);
  assert(variadic_seed == seed1);

  std::cout << "  -> HashCombine 組合順序敏感度與變參重載完全符合預期！" << std::endl;
}

int main()
{
  std::cout << "=== 開始執行 OuroKore Base 雜湊模組 (Hash Utilities) 單元測試 ===" << std::endl;

  TestFnv1a();
  TestCrc32();
  TestMurmurHash3();
  TestHashCombine();

  std::cout << "\n=== 所有 Base 雜湊單元測試 100% 通過！ ===" << std::endl;
  return 0;
}
