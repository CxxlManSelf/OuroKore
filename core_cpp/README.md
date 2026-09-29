# OuroKore Core C++

**OuroKore** 是一個針對超大規模物件圖（Large-Scale Object Graph）託管、弱引用代數系統、透明自動脫水換頁（Dehydration/Rehydration）與三層權限邊界隔離所設計的高效能 C++ 系統核心框架。

---

## ⚡ 語言標準與編譯要求 (C++20 Standard Invariant)

> ⚠️ **重要環境規範**：
> **OuroKore 核心程式庫全體（包含 `ourokore_base` 與 `ourokore_core`）均嚴格採用 ISO C++20 標準建立。**
> 
> 為確保公開標頭檔中的現代 C++ 模板（例如 `OwningHandle<T>`、`OuroPtr<T>`）、STL 資料結構記憶體佈局、多執行緒同步原語與最佳化編譯相容性：
> **程式庫的使用端（無論是 Host 主程式或外掛 Component/Plugin）強烈建議一律採用 C++20 或更高標準（C++20+）進行開發與建置。**

### CMake 使用端配置建議

在您的應用程式或插件 CMake 專案中，請確保啟用 C++20。針對動態外掛（Plugin），**強制使用 `MODULE` 庫類型**：

#### 1. 宿主主程式 (Host Executable)
```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKoreApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MyOuroKoreApp main.cpp)
target_include_directories(MyOuroKoreApp PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyOuroKoreApp PRIVATE ourokore_core ourokore_base)
```

#### 2. 動態擴充外掛 (Plugin / Component)
> ⚠️ **鐵律：外掛在庫類型上必須使用 `MODULE`，嚴禁宣告為 `SHARED`**。`MODULE` 保證外掛庫無法被靜態鏈結，僅供執行期透過 `DynamicLibrary` 動態載入，維持純淨熱卸載隔離。

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOuroKorePlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 宣告為 MODULE 插件庫（非 SHARED）
add_library(MyPlugin MODULE MyPlugin.cpp)
target_include_directories(MyPlugin PRIVATE ${OUROKORE_INCLUDE_DIR})
target_link_libraries(MyPlugin PRIVATE ourokore_core ourokore_base)
set_target_properties(MyPlugin PROPERTIES 
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
```

### 全域文字訊息標準 (UTF-8 Standard Invariant)

> ⚠️ **全域唯一字串編碼標準**：
> OuroKore 系統內部涉及到任何文字訊息（屬性鍵名、插槽名稱、錯誤描述、二進位字串序列化與跨語言 FFI 傳遞），**一律強制採用 UTF-8 為唯一標準**。
> Windows 下涉及檔案路徑與作業系統呼叫，底層自動轉換為 `std::wstring` 呼叫 Unicode `W` 版 API，對外介面與持久化儲存 100% 保持 UTF-8，嚴禁混用 ANSI 本地編碼。

---

## 🚀 應用端受管物件開發規範 (Managed Object Definition)

> ⚠️ **全面嚴格強制宣告鐵律 (Strict Subclass Invariant)**：
> 凡是交由 OuroKore 託管的物件（需透過 `ork::CreateObject<T>()` 建立者），**一律強制繼承自 `ork::Subclass<T, Base = ork::OuroObject>`**。
> **嚴格禁止直接裸繼承 `OuroObject`**（如 `class Foo : public OuroObject`）；若直接繼承，`CreateObject<Foo>()` 將於編譯期觸發 `static_assert` 攔截阻斷。

### 1. 基礎宣告與多層繼承範例（零巨集、單一真實來源）
```cpp
#include <ourokore/component/OuroCore.hpp>

// 1. 基底受管物件（預設 Base 為 ork::OuroObject）
class Creature : public ork::Subclass<Creature, ork::OuroObject>
{
public:
  int m_hp{100};

  Creature() = default;
  explicit Creature(int hp) : m_hp(hp) {}

  void SerializePayload(ork::OuroStream &stream) const override {
    stream.WriteProperty("hp", m_hp);
  }
  void DeserializePayload(ork::OuroStream &stream) override {
    stream.ReadProperty("hp", m_hp);
  }
};

// 2. 衍生子類別（將 Base 指定為 Creature，支援建構子完美轉發）
class Monster : public ork::Subclass<Monster, Creature>
{
public:
  int m_rage{50};

  Monster() = default;
  Monster(int hp, int rage) : Subclass(hp), m_rage(rage) {}
};

// 3. 衍生孫類別（曾孫類別多層繼承無縫串接）
class BossMonster : public ork::Subclass<BossMonster, Monster>
{
public:
  std::string m_special{"Meteor"};

  BossMonster() = default;
  BossMonster(int hp, int rage, std::string special)
      : Subclass(hp, rage), m_special(std::move(special)) {}
};
```

### 2. 多型型別檢查與安全轉型
```cpp
auto boss = ork::CreateObject<BossMonster>(5000, 200, "Supernova");

// 1. 多型繼承判定（零 I/O、純 ControlBlock 墓碑命中）
assert(boss.Is<BossMonster>());
assert(boss.Is<Monster>());
assert(boss.Is<Creature>());
assert(boss.Is<ork::OuroObject>());

// 2. 向上轉型（Upcasting 到祖父 OuroPtr）
ork::OuroPtr<Creature> creature_ptr = boss.As<Creature>();

// 3. 向下轉型（Downcasting 回 BossMonster）
ork::OuroPtr<BossMonster> restored = creature_ptr.As<BossMonster>();
assert(restored);
```

---

## 🧭 核心架構特色

1. **控制區塊與 Handle 代數系統 (ControlBlock & Handle System)**：
   - 透過全域唯一 64-bit `HandleID` 與控制區塊管理物件生命週期。
   - `OwningHandle<T>`：持有圖拓撲的強引用，支援循環參照並由背景 `CycleCollector` 非同步安全回收。
   - `UnboundHandle<T>`：純旁觀者弱引用（只看不管生死），專為外掛模組隨時卸載防卡死、UI 介面暫時觀察與快取索引設計。
   - `OuroPtr<T>`：棧上與根參照守衛（Root Edge），徹底移除裸指標暴露以防止 UAF 與逃逸，透過 `operator()` 安全轉發成員呼叫，並由內部延遲快取指標在保證不脫水條件下提供原生極速執行。

2. **記憶體自動脫水與透明復水 (Dehydration & Transparent Rehydration)**：
   - 物件生命週期支援 `UnsavedNew`、`Clean`、`Dirty`、`Dehydrated` 四種儲存狀態。
   - 在記憶體壓力或 LRU 策略觸發時將 Dirty 物件序列化落盤並安全釋放記憶體 Payload，保留 ControlBlock。存取時透明按需自 `IStorageDriver` 復原。

3. **全域型別系統與安全多型轉型 (Type System & Safe Casting)**：
   - 控制區塊（ControlBlock）墓碑長存 64 位元唯一 `TypeID`，物件脫水換頁至磁碟後**仍可進行型別檢查與繼承判定，絕不引發非預期 I/O 穿透復水**。
   - C++ 高階包裝層 `OuroPtr<T>` 支援 `Is<U>()` 判定與 `As<U>()` 轉型；支援左值拷貝（安全增持根引用）與右值移動語意 `std::move(ptr).As<U>()`（**零引用計數變更開銷**，完美轉移所有權）。
   - 純 C ABI 完整導出 `ork_register_type`、`ork_get_object_type`、`ork_is_instance_of`、`ork_is_subclass_of`，為 C#、Rust 等跨語言 FFI 提供同等安全轉型基石。

4. **Base 通用現代基礎工具庫 (Base Foundation & Utilities)**：
   - **高效能跨平台執行緒池 (ThreadPool)**：支援動態彈性排程與 Future/Promise 非同步鏈結。
   - **動態模組載入器 (DynamicLibrary)**：禁絕手動卸載，具備物件生命週期反向錨定（Life-Bound Retention）。應用端將產生物件綁定後放棄 `load()` 初始句柄（或呼叫 `reset()`），待所有物件解構後自動安全卸載。
   - **標準 C++20 現代雜湊模組 (Hash Utilities)**：[Hash.hpp](include/ourokore/base/Hash.hpp) 全面支援編譯期常數 `constexpr`，內建 FNV-1a (32/64-bit)、CRC32 (IEEE 802.3)、MurmurHash3 (32-bit)、`HashCombine` 及使用者自訂字面量（`_fnv64`、`_crc32`），供型別計算、屬性鍵比對與資料完整性校驗使用。

5. **三層邊界隔離與跨語言 FFI 友善架構**：
   - 遵循「**C ABI 為底，各語言 Wrapper 為糖**」之核心設計哲學。
   - **Host 層獨佔特權**：全進程生命週期（`Init`、`Shutdown`、`Reset`）、基礎設施注入（儲存驅動、脫水器）、全域 GC 調度。
   - **Plugin/Component 隔離**：僅限使用受管物件、讀寫鎖與安全唯讀查詢，嚴禁碰觸系統級特權。
   - **底層純 C ABI**：所有功能均有完備的純 C 函式（`c_api/`），禁止 C++ 例外跨越 DLL 邊界，便於未來接入 C#、Rust、Python 與 Go。

---

## 📚 相關規範與文件指引

- **架構規範與開發守則**：[AGENTS.md](file:///c:/MySrc/OuroKore/core_cpp/AGENTS.md)
- **API 三層邊界與 FFI 準則**：[.agents/rules/api_boundaries.md](file:///c:/MySrc/OuroKore/core_cpp/.agents/rules/api_boundaries.md)
- **AI 助理架構維護技能指引**：[.agents/skills/ourokore/SKILL.md](file:///c:/MySrc/OuroKore/core_cpp/.agents/skills/ourokore/SKILL.md)
