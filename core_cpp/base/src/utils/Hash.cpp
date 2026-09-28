#include "ourokore/base/Hash.hpp"

namespace ork::base::hash
{

// 靜態確保編譯期演算法正確性（Standard Test Vectors）
static_assert(Fnv1a32("") == 2166136261U, "FNV-1a 32-bit empty string vector failed");
static_assert(Fnv1a64("") == 14695981039346656037ULL, "FNV-1a 64-bit empty string vector failed");

// CRC32 standard test vector: "123456789" -> 0xCBF43926
static_assert(Crc32("123456789") == 0xCBF43926U, "CRC32 standard vector failed");

}  // namespace ork::base::hash
