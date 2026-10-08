# 09. 外掛 Heap 追蹤與記憶體防禦指南 (Heap Tracker & Plugin Heap)

本章節介紹 OuroKore 基礎模組（`ourokore_base`）中的外掛 Heap 追蹤與記憶體洩漏防禦設施（`HeapTracker`、`PluginHeap`、`TrackedNewDelete` 與 `heap_api.h`）。

---

## 🧭 1. 設計哲學與心智模型

1. **外掛動態庫（MODULE）記憶體洩漏與 CRT 邊界防禦**：
   - 在微核心與外掛架構中，動態載入的外掛模組（`MODULE` / DLL）在生命週期結束或熱重載卸載前，若遺留任何未釋放的堆配置（Heap Allocation），其程式碼段與虛擬函式表卸載後將引發嚴重的記憶體洩漏甚至懸空崩潰。
   - OuroKore 提供雙軌並行的 Heap 追蹤與檢驗系統，支援在編譯期彈性選擇 **方案 A** 或 **方案 B**。

2. **防重入分配器保護 (RawSystemAllocator Guard)**：
   - 追蹤器內部維護配置記錄表時，其本身的容器（如雜湊表或陣列）嚴格使用 `RawSystemAllocator`（直接繞過重載的 `operator new`，直通底層系統 API），徹底杜絕內部記帳容器觸發遞迴死鎖與爆棧。

3. **C++17 對齊記憶體原生相容**：
   - 追蹤器全面相容 C++17 對齊配置要求（Windows 平台調用 `_aligned_malloc` / `_aligned_free`，POSIX 平台調用 `posix_memalign` / `free`）。

---

## ⚙️ 2. 編譯期雙策略選擇 (Compile-Time Policy)

| 方案 | 識別巨集 | Debug 行為 | Release 行為 | 適用情境 |
| :--- | :--- | :--- | :--- | :--- |
| **方案 A (預設)** | `ORK_HEAP_POLICY_A` | 詳細診斷 (記錄檔名/行號/序號) | 完全關閉 (零開銷直通系統 malloc，無鎖無記帳) | 追求發布版極致原生速度 |
| **方案 B** | `ORK_HEAP_POLICY_B` | 詳細診斷 (記錄檔名/行號/序號) | 輕量原子無鎖計數 (記錄區塊與大小) | 發布版仍需驗收 Heap 是否清空 |

### CMake 編譯指定方式：
```bash
# 選擇方案 A (Debug 詳細 / Release 零開銷關閉)
cmake -B build -DOUROKORE_HEAP_POLICY=A

# 選擇方案 B (Debug 詳細 / Release 輕量無鎖原子計數)
cmake -B build -DOUROKORE_HEAP_POLICY=B
```

亦可在程式碼或 Target 編譯選項中手動定義：
```cpp
#define ORK_HEAP_POLICY ORK_HEAP_POLICY_A // 或 ORK_HEAP_POLICY_B
#include <ourokore/base/HeapTracker.hpp>
```

---

## 🚀 3. 全域透明運算子重載 (Global Overload Mode)

在外掛 MODULE 動態庫的任一主實作檔（如 `PluginMain.cpp`）中宣告：
```cpp
#include <ourokore/base/PluginHeap.hpp>

// 一行啟動該外掛模組全域 operator new/delete/new[]/delete[] 重載
ORK_ENABLE_PLUGIN_HEAP_TRACKING()
```
* **效果**：該外掛模組內部所有的 `new`、`delete` 以及 STL 容器（如 `std::vector`、`std::string` 等）之堆配置全部透明導向受管追蹤，業務程式碼無需修改任何一行。在方案 A 的 Release 組態下自動展開為空實作，零額外開銷。

---

## 🔍 4. 顯式受管 new / delete 巨集 (Explicit Tracked Mode)

若需在原始碼中精確標記檔案與行號位置：
```cpp
#include <ourokore/base/TrackedNewDelete.hpp>

// 1. 單一物件建立與釋放 (自動於編譯期捕捉 __FILE__ 與 __LINE__)
Monster* m = ORK_NEW(Monster, "Goblin", 100);
ORK_DELETE(m);

// 2. 陣列建立與釋放
int* buffer = ORK_NEW_ARRAY(int, 256);
ORK_DELETE_ARRAY(buffer, 256);

// 3. STL 容器整合 (TrackedAllocator)
std::vector<int, ork::TrackedAllocator<int>> my_vec;
```

---

## 🛡️ 5. 外掛結束前清空判定與洩漏診斷 (Zero-Leak Verification)

外掛在 `PluginShutdown()` 或 DLL 卸載前檢驗 Heap 狀態：
```cpp
extern "C" PLUGIN_EXPORT int32_t PluginShutdown() {
    // 1. 查詢是否已完全清空 (無任何殘留配置)
    if (!ork::PluginHeap::is_clean()) {
        // 2. 輸出格式化 UTF-8 洩漏清單 (含序號、位址、大小、檔名行號)
        std::cerr << ork::PluginHeap::dump_leaks_to_string("MyPlugin");
        return -1; // 告知宿主尚有未釋放資源
    }
    return 0; // 成功清空
}

// 嚴格斷言：若未清空立即印出報告並拋出 std::runtime_error
ork::PluginHeap::assert_clean("MyPlugin");

// RAII 守衛：離開作用域時自動檢查，若有洩漏自動輸出至 stderr
// 💡 PluginHeapGuard 內部採用內置固定緩衝區深拷貝，徹底杜絕外部暫時字串（如 "Test_" + name）懸空，
//    並保證 100% 零堆記憶體配置（Zero Heap Allocation Invariant），避免自身配置污染外掛的 HeapTracker 記帳產生偽誤報！
{
    ork::PluginHeapGuard guard("PluginScope");
    // 執行外掛邏輯...
}
```

---

## 🌐 6. 跨語言純 C ABI (`ourokore/base/heap_api.h`)

底層提供純 C ABI，供 C#、Rust、Python 進行記憶體檢查與 FFI 對接，保證跨語言邊界零例外外洩（所有函式一律修飾 `ORK_CALL` 呼叫慣例）：
* `void *ORK_CALL ork_heap_allocate(size, file, line)` / `void ORK_CALL ork_heap_deallocate(ptr)`
* `int32_t ORK_CALL ork_heap_is_clean()` -> 傳回 `1`（已清空）或 `0`（未清空）
* `uint64_t ORK_CALL ork_heap_get_active_allocations()` / `uint64_t ORK_CALL ork_heap_get_active_bytes()`
* `int32_t ORK_CALL ork_heap_dump_leaks(out_buf, buf_size)`
* `int32_t ORK_CALL ork_heap_assert_clean(context_name)`

---

## ⚠️ 7. 外掛開發避坑指南與高壓線禁忌

1. **嚴格禁止跨動態庫混用配置與釋放**：
   - 由外掛模組內部配置之記憶體，必須由該模組自行釋放，嚴禁在宿主或其他外掛中以標準 `free`/`delete` 釋放，以防不同 CRT 實例導致堆損壞。
2. **動態庫卸載前必須 100% 驗收清空**：
   - 配合 `DynamicLibrary` 的生命週期反向錨定或兩階段卸載掛鉤，在外掛退出前務必調用 `PluginHeap::is_clean()` 或 `PluginHeap::assert_clean()`，確保零洩漏再允許卸載。
3. **CMake 構建規範**：
   - 動態外掛必須以 `add_library(<name> MODULE ...)` 構建，嚴禁宣告為 `SHARED`，確保獨立動態加載與乾淨卸載能力。
