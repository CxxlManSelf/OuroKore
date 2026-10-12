# -*- coding: utf-8 -*-
"""
更新應用端文件、技術文件與 Skill 文件以支援 UUID 工具模組
"""
from pathlib import Path

def update_manual_08():
    path = Path("../specs/manual/08_base_utilities.md").resolve()
    content = path.read_text(encoding="utf-8")
    
    uuid_section = """
---

## 🆔 7. 現代 128 位元 UUID/GUID 工具 (Uuid.hpp & uuid_api.h)

* **標頭檔**：`<ourokore/base/Uuid.hpp>`、`<ourokore/base/uuid_api.h>`
* **命名空間**：`ork::base`（或使用者自訂字面量 `ork::base::literals::uuid_literals`）

OuroKore 基礎層提供嚴格符合 **RFC 4122** 與最新 **RFC 9562** 規範的現代高效能 128 位元 UUID/GUID 工具：

### 7.1 核心特性
1. **UUIDv4（隨機型）**：底層採用平台原生密碼學安全隨機數（Windows `BCryptGenRandom`、Linux `getrandom`），提供高品質低碰撞的隨機識別碼。
2. **UUIDv7（時間排序型，RFC 9562）**：前 48 位元為 Unix 紀元毫秒時間戳記（可表達至西元 10889 年），結合 12 位元單調計數器（Monotonic Counter）與 62 位元隨機熵。在緊密迴圈與多執行緒高並發下保證**嚴格單調遞增時序**，具備極佳的資料庫叢集索引（B-Tree）親和性與時間序列檢索效能。
3. **C++23 編譯期常數支援（constexpr）**：支援編譯期字串解析與常數字面量 `_uuid`，在編譯期即可驗證 UUID 格式合法性。
4. **無例外值語意解析（Result<Uuid>）**：字串解析介面整合 `ork::base::Result`（`std::expected`），格式錯誤時安全傳播，不拋出任何 C++ 例外。
5. **格式彈性相容**：同時相容標準 36 字元橫線格式（`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`）、32 字元無橫線緊湊格式、以及 38 字元大括號格式（`{...}`），解析時自動忽略大小寫。
6. **三路比較與 STL 容器整合**：原生支援 `<=>` 運算子（UUIDv7 字典序直接對齊時序）、`std::hash<ork::base::Uuid>` 特化，以及 C++20/C++23 `std::formatter<ork::base::Uuid>`（支援 `{:S}` 大寫、`{:c}` 緊湊、`{:b}` 大括號等格式標記）。
7. **純 C ABI 底層對齊**：提供 `ork_uuid_t` 與 `uuid_api.h`，100% 友善支援跨語言 FFI（C#、Rust、Python、Go）。

### 7.2 程式碼範例
```cpp
#include <ourokore/base/Uuid.hpp>
#include <iostream>

using namespace ork::base;
using namespace ork::base::literals::uuid_literals;

// 1. 編譯期常數字面量
constexpr Uuid kSystemId = "018f2f4c-5353-7b3b-8105-df4d3855a02e"_uuid;
static_assert(kSystemId.Version() == 7);
static_assert(kSystemId.Variant() == 1);

void Example()
{
    // 2. 隨機生成 UUIDv4
    Uuid v4 = Uuid::GenerateV4();

    // 3. 產生時間排序單調 UUIDv7
    Uuid v7 = Uuid::GenerateV7();
    uint64_t ms = v7.TimestampMs(); // 取得 48 位元毫秒時間戳

    // 4. 字串格式化與 std::format 支援
    std::cout << "標準格式: " << v7.ToString() << "\\n";
    std::cout << "大寫格式: " << std::format("{:S}", v7) << "\\n";
    std::cout << "緊湊格式: " << v7.ToCompactString() << "\\n";
    std::cout << "大括號格式: " << v7.ToBracedString() << "\\n";

    // 5. 無例外值語意解析
    auto parse_res = Uuid::FromString("018f2f4c-5353-7b3b-8105-df4d3855a02e");
    if (parse_res) {
        Uuid parsed = *parse_res;
        assert(parsed == kSystemId);
    }
}
```
"""
    if "## 🆔 7. 現代 128 位元 UUID/GUID 工具" not in content:
        content = content.rstrip() + "\n" + uuid_section
        path.write_text(content, encoding="utf-8")
        print("Updated 08_base_utilities.md")

def update_manual_10():
    path = Path("../specs/manual/10_api_reference.md").resolve()
    content = path.read_text(encoding="utf-8")
    
    target_needle = "## 🌐 13. 底層純 C ABI（Cross-Language FFI）分類索引"
    replacement = """## 🆔 13. 現代 128 位元 UUID/GUID 工具：`ork::base::Uuid` / `uuid_api.h`
* **標頭檔**：
  * `<ourokore/base/Uuid.hpp>`
  * `<ourokore/base/uuid_api.h>`
* **命名空間**：`ork::base`（字面量在 `ork::base::literals::uuid_literals`）
* **靜態常數與工廠方法**：
  * `static constexpr Uuid Nil() noexcept`：全 0 Nil UUID。
  * `static constexpr Uuid Max() noexcept`：全 0xFF Max UUID。
  * `static Uuid GenerateV4() noexcept`：產生密碼學安全隨機 UUIDv4。
  * `static Uuid GenerateV7() noexcept`：產生時間排序單調 UUIDv7（Unix 毫秒時間戳 + 單調計數器 + 隨機熵）。
  * `static Uuid FromUnixTimeMsV7(uint64_t timestamp_ms) noexcept`：指定時間戳產生 UUIDv7。
* **狀態查詢與欄位解構**：
  * `bool IsNil() const noexcept` / `bool IsMax() const noexcept` / `explicit operator bool() const noexcept`
  * `uint8_t Version() const noexcept`：取得版本號（如 4 或 7）。
  * `uint8_t Variant() const noexcept`：取得變體號（RFC 4122/9562 為 1）。
  * `uint64_t TimestampMs() const noexcept`：提取 UUIDv7 前 48 位元毫秒時間戳。
  * `uint64_t HighBits() const noexcept` / `uint64_t LowBits() const noexcept`：高低 64 位元整數。
  * `const std::array<uint8_t, 16>& Bytes() const noexcept` / `const uint8_t* Data() const noexcept`
* **序列化與格式化**：
  * `std::string ToString(bool uppercase = false) const`：標準 36 字元橫線格式。
  * `std::string ToCompactString(bool uppercase = false) const`：32 字元無橫線緊湊格式。
  * `std::string ToBracedString(bool uppercase = false) const`：38 字元大括號格式。
  * `void ToChars(char* out_36, bool uppercase = false) const noexcept`：零配置輸出至緩衝區。
* **反序列化與編譯期字面量**：
  * `static Result<Uuid> FromString(std::string_view str) noexcept`：無例外安全解析。
  * `static std::optional<Uuid> TryParse(std::string_view str) noexcept`
  * `static constexpr std::optional<Uuid> ParseConstexpr(std::string_view str) noexcept`
  * `constexpr Uuid operator""_uuid(const char* str, size_t len)`：編譯期常數字面量。
* **STL 整合特化**：
  * `std::hash<ork::base::Uuid>`
  * `std::formatter<ork::base::Uuid>`（支援格式標記：`{:S}` 大寫、`{:c}` / `{:C}` 緊湊、`{:b}` / `{:B}` 大括號）
* **純 C ABI 介面（修飾 `ORK_CALL`）**：
  * `ork_uuid_nil()` / `ork_uuid_max()`
  * `ork_uuid_generate_v4()` / `ork_uuid_generate_v7()` / `ork_uuid_generate_v7_with_time(ts)`
  * `ork_uuid_is_nil(u)` / `ork_uuid_is_max(u)` / `ork_uuid_get_version(u)` / `ork_uuid_get_variant(u)`
  * `ork_uuid_get_timestamp_ms(u, out_ms)`
  * `ork_uuid_to_string(u, buf, size, upper)` / `ork_uuid_from_string(str, out_u)` / `ork_uuid_compare(a, b)`

---

## 🌐 14. 底層純 C ABI（Cross-Language FFI）分類索引"""
    
    if target_needle in content and "## 🆔 13. 現代 128 位元 UUID/GUID 工具" not in content:
        content = content.replace(target_needle, replacement)
        # 並在分類索引最後加上類別 E
        category_e = """* **類別 D：外掛 Heap 記憶體檢查**（`ork_heap_allocate`、`ork_heap_deallocate`、`ork_heap_is_clean`、`ork_heap_dump_leaks`、`ork_heap_assert_clean`）
* **類別 E：UUID / GUID 工具**（`ork_uuid_nil`、`ork_uuid_max`、`ork_uuid_generate_v4`、`ork_uuid_generate_v7`、`ork_uuid_generate_v7_with_time`、`ork_uuid_is_nil`、`ork_uuid_is_max`、`ork_uuid_get_version`、`ork_uuid_get_variant`、`ork_uuid_get_timestamp_ms`、`ork_uuid_to_string`、`ork_uuid_from_string`、`ork_uuid_compare`）"""
        content = content.replace("* **類別 D：外掛 Heap 記憶體檢查**（`ork_heap_allocate`、`ork_heap_deallocate`、`ork_heap_is_clean`、`ork_heap_dump_leaks`、`ork_heap_assert_clean`）", category_e)
        path.write_text(content, encoding="utf-8")
        print("Updated 10_api_reference.md")

def update_technical_03():
    path = Path("../specs/technical/03_c_abi_and_memory.md").resolve()
    content = path.read_text(encoding="utf-8")
    
    category_e = """
---

### 🆔 類別 E：Base 模組 UUID / GUID 介面 (`uuid_api.h`)
> 提供符合 RFC 4122 與 RFC 9562 規範之 128 位元 UUID 純 C ABI 介面，支援跨語言 FFI（C# P/Invoke、Rust extern "C"、Python ctypes/cffi），保證零例外逃逸。

48. `ork_uuid_nil`
    - **符號規格**：`Function ork_uuid_nil() -> ork_uuid_t`
    - **說明**：產生全 0 的 Nil UUID。
49. `ork_uuid_max`
    - **符號規格**：`Function ork_uuid_max() -> ork_uuid_t`
    - **說明**：產生全 0xFF 的 Max UUID。
50. `ork_uuid_generate_v4`
    - **符號規格**：`Function ork_uuid_generate_v4() -> ork_uuid_t`
    - **說明**：產生密碼學安全隨機型 UUIDv4。
51. `ork_uuid_generate_v7`
    - **符號規格**：`Function ork_uuid_generate_v7() -> ork_uuid_t`
    - **說明**：產生時間排序型 UUIDv7（Unix 毫秒時間戳記 + 嚴格單調計數器 + 隨機熵）。
52. `ork_uuid_generate_v7_with_time`
    - **符號規格**：`Function ork_uuid_generate_v7_with_time(timestamp_ms: UInt64) -> ork_uuid_t`
    - **說明**：指定 Unix 紀元毫秒時間戳記產生 UUIDv7。
53. `ork_uuid_is_nil`
    - **符號規格**：`Function ork_uuid_is_nil(uuid: Pointer<ork_uuid_t>) -> Int32`
    - **說明**：判定是否為 Nil UUID（1 代表是，0 代表否，-1 代表無效指標）。
54. `ork_uuid_is_max`
    - **符號規格**：`Function ork_uuid_is_max(uuid: Pointer<ork_uuid_t>) -> Int32`
    - **說明**：判定是否為 Max UUID（1 代表是，0 代表否，-1 代表無效指標）。
55. `ork_uuid_get_version`
    - **符號規格**：`Function ork_uuid_get_version(uuid: Pointer<ork_uuid_t>) -> Int32`
    - **說明**：取得 UUID 版本號（如 4 或 7，指標無效傳回 -1）。
56. `ork_uuid_get_variant`
    - **符號規格**：`Function ork_uuid_get_variant(uuid: Pointer<ork_uuid_t>) -> Int32`
    - **說明**：取得 UUID 變體代碼（RFC 4122/9562 為 1，指標無效傳回 -1）。
57. `ork_uuid_get_timestamp_ms`
    - **符號規格**：`Function ork_uuid_get_timestamp_ms(uuid: Pointer<ork_uuid_t>, out_ms: MutablePointer<UInt64>) -> Int32`
    - **說明**：提取 UUIDv7 之 Unix 毫秒時間戳記（成功傳回 0，非 v7 或指標無效傳回負數錯誤碼）。
58. `ork_uuid_to_string`
    - **符號規格**：`Function ork_uuid_to_string(uuid: Pointer<ork_uuid_t>, out_buf: MutablePointer<Char>, buf_size: UInt64, uppercase: Int32) -> Int32`
    - **說明**：將 UUID 轉為標準 36 字元橫線字串（成功傳回 36，緩衝區不足或失敗傳回負數錯誤碼）。
59. `ork_uuid_from_string`
    - **符號規格**：`Function ork_uuid_from_string(str: CString, out_uuid: MutablePointer<ork_uuid_t>) -> Int32`
    - **說明**：從字串解析 UUID（支援 36 字元標準、32 字元緊湊、38 字元大括號格式，成功傳回 0，格式錯誤傳回負數）。
60. `ork_uuid_compare`
    - **符號規格**：`Function ork_uuid_compare(a: Pointer<ork_uuid_t>, b: Pointer<ork_uuid_t>) -> Int32`
    - **說明**：比較兩個 UUID 的 16 位元組字典序（傳回 <0, 0, >0）。
"""
    if "### 🆔 類別 E：Base 模組 UUID / GUID 介面" not in content:
        content = content.rstrip() + "\n" + category_e
        path.write_text(content, encoding="utf-8")
        print("Updated 03_c_abi_and_memory.md")

def update_skill_app():
    path = Path("../skills/ourokore-app/SKILL.md").resolve()
    content = path.read_text(encoding="utf-8")
    
    app_uuid_section = """
---

## 🆔 9. 現代 128 位元 UUID/GUID 工具最佳實踐 (Uuid.hpp & uuid_api.h)

在應用端、領域實體設計或分散式通信中，若需要全局唯一識別碼、落盤主鍵或資料庫索引友好鍵值，直接使用 Base 工具庫提供之 `ork::base::Uuid`：

1. **UUIDv4（隨機識別碼） vs UUIDv7（時間排序單調識別碼）**：
   - 一般隨機唯一識別：`auto id = ork::base::Uuid::GenerateV4();`
   - 資料庫叢集索引、日誌追蹤或有時序性實體：**強烈推薦使用 `ork::base::Uuid::GenerateV7()`**。
     前 48 位元為 Unix 毫秒時間戳，內建 12 位元單調計數器，**保證緊密迴圈下生成時嚴格單調遞增（`<`）**，大幅減輕 B-Tree 索引頁分裂開銷！
2. **編譯期常數字面量**：
   - 引用標頭檔 `<ourokore/base/Uuid.hpp>` 與 `using namespace ork::base::literals::uuid_literals;`
   - `constexpr auto kRootId = "018f2f4c-5353-7b3b-8105-df4d3855a02e"_uuid;`
   - 在編譯期完成解析與驗證，零執行期開銷。
3. **無例外字串解析與 std::format 輸出**：
   - `auto res = ork::base::Uuid::FromString(str);` 回傳 `Result<Uuid>`，無例外安全。
   - 格式化輸出支援 `std::format("{}", id)`（標準）、`std::format("{:S}", id)`（大寫）、`std::format("{:c}", id)`（32 字元緊湊）。
"""
    if "## 🆔 9. 現代 128 位元 UUID/GUID 工具最佳實踐" not in content:
        content = content.rstrip() + "\n" + app_uuid_section
        path.write_text(content, encoding="utf-8")
        print("Updated skills/ourokore-app/SKILL.md")

if __name__ == "__main__":
    update_manual_08()
    update_manual_10()
    update_technical_03()
    update_skill_app()
    print("All doc updates finished successfully!")
