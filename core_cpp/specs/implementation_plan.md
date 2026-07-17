# OuroKore Phase 1 核心架構實作計畫

本計畫書根據 NotebookLM 筆記《OuroKore 程式庫建立》中最新定義的 Phase 1 設計規格，規劃 OuroKore 概念程式庫之雙核記憶體管理與三大指標武器庫的 C++ 實作。

## User Review Required

以下是實作中的關鍵設計決策，請使用者確認：

1. **`OwningHandle` 的絕對禁錮與生命週期分流**：
   - **禁錮規範**：`OwningHandle` 只允許作為 `OuroObject` 的成員變數（代表物件內部的拓撲連線）。禁止在主程式堆疊 (Stack) 或全域變數中宣告。
   - **執行期防線**：在 `OwningHandle` 指派或非預設建構時，若偵測到其建構上下文的 `m_owner_id == ORK_ROOT_ID` (0)，代表被宣告於外部/Stack，系統將拋出執行期異常（`std::runtime_error`）以進行物理隔離。
   - **工廠介面變更**：`ork::CreateObject<T>()` 將直接回傳 **`OuroPtr<T>`** 而非 `OwningHandle<T>`。回傳的 `OuroPtr` 會被標記為 `m_is_root = true`，自動註冊 `ORK_ROOT_ID` 到目標的邊（強引用計數 +1），並在析構時自動註銷邊（強引用計數 -1）來釋放物件。

2. **兩階段解鎖與 `OuroPtr` 執行合約**：
   - **禁止直接操作**：`OwningHandle` 僅代表靜態擁有權，**不重載 `->` 運算子**。開發者無法直接透過 Handle 存取 Target 的成員。
   - **安全操作提取**：若要操作 Target 物件，必須藉由建置 `OuroPtr`（例如 `OuroPtr<Child> ptr(m_child, write_mode)`）來鎖定控制區塊的 `m_rw_lock`（讀寫鎖），透過 `OuroPtr` 的 RAII 進行安全且執行緒安全的業務執行與自動解鎖。
   - **父物件自備 Lock**：父物件必須自備 mutex 保護其肚子裡的 `OwningHandle` 成員之讀寫指派，以防並發競爭。

3. **Thread-Local 執行期上下文 (`ActiveOwnerContext` / `ActiveOwnerGuard`)**：
   - 為了讓 `OwningHandle` 作為物件成員建構時，自動將 `m_owner_id` 設為父物件 ID，我們將在 C++ 層實作 `thread_local` 的 Active Owner ID 上下文。
   - 在 `ork::CreateObject` 建構期間，會透過此機制隱式傳遞父物件 ID。
   - **巢狀建構防禦 (RAII Guard)**：為了解決物件在建構子中呼叫 `CreateObject` 產生子物件的巢狀（Nested）情況，我們將實作 RAII 風格的 `ActiveOwnerGuard`。在進入 `CreateObject` 時備份當前執行緒的 Owner ID，並在離開作用域（析構）時自動還原為備份的 Owner ID。這能保證不論呼叫棧有多深，每個 `OwningHandle` 都能在指派時取得正確的 Owner ID，且具備異常安全性。

4. **`WeakHandle` 的 Lazy Pruning 與 Const-correctness**：
   - `WeakHandle::IsAlive() const` 在發現強引用歸零時，需將內部的 `m_target_id` 清空並通知 Registry 釋放弱計數。我們將 `m_target_id` 宣告為 `mutable` 以繞過 `const` 限制。

---

## Proposed Changes

### OuroKore Core Component

#### [NEW] [OuroObject.hpp](file:///c:/MySrc/OuroKore/develop/core_cpp/include/ourokore/component/OuroObject.hpp)
定義所有具體物件實體（肉體）的虛擬基底類別。
- 強制宣告 `virtual ~OuroObject() = default;`
- 強制宣告 `virtual uint64_t GetTypeID() const = 0;` (供未來 RTTI 序列化使用)

#### [NEW] [Handles.hpp](file:///c:/MySrc/OuroKore/develop/core_cpp/include/ourokore/component/Handles.hpp)
實作 `ork::OwningHandle<T>`, `ork::WeakHandle<T>` 與 `ork::OuroPtr<T>` 範本類別。
- **`OuroPtr<T>`**：
  - 增加 `m_is_root` 與 `m_is_write` 成員。
  - 當作為工廠產生的 Root 憑證時，建構時向 Registry 註冊 `ORK_ROOT_ID` 邊，解構時註銷並釋放強引用。
  - 提供 RAII 方式鎖定目標物件的讀寫鎖。
  - 提供 `operator->()` 與 `operator*()` 存取具體 Payload。
- **`OwningHandle<T>`**：
  - 持有 `HandleID m_target_id` 與 `HandleID m_owner_id`。
  - **不提供** `operator->()` 或 `operator*()` 運算子。
  - 建構時透過 C API 向 Registry 註冊有向邊（`RegisterEdge`），增加強引用。
  - 析構與指派時，註銷有向邊，扣減強引用。
  - **執行期安全防禦**：若 `m_owner_id == ORK_ROOT_ID` 且 `m_target_id != 0`，拋出異常阻止外部使用。
- **`WeakHandle<T>`**：
  - 持有 `mutable HandleID m_target_id`。
  - 增加弱引用計數。
  - 實作 `IsAlive() const`，若偵測到已死，則自我閹割（`m_target_id = 0`）並扣減弱引用。

#### [NEW] [core.h](file:///c:/MySrc/OuroKore/develop/core_cpp/include/ourokore/c_api/core.h)
公開的 C ABI 邊界。
- **嚴格的 C 語言隔離**：使用 `extern "C"` 保護，絕不使用 C++ 的 `class` 宣告。對外提供不透明的結構體指標 `typedef struct OuroObject OuroObject;`，主要透過 `HandleID`（`uint64_t`）與系統進行互動。
- **異常防禦與錯誤碼**：所有匯出函式強制標記呼叫慣例巨集 `ORK_API`（如 `__cdecl`），且統一回傳 `int32_t` 錯誤碼（成功為 `0`，失敗為錯誤代碼），避免 C++ 異常穿透 FFI 導致崩潰。
- **核心 C API 接口**：提供註冊物件、註冊/註銷邊、上鎖與解鎖、讀寫狀態查詢等 C 接口。

#### [NEW] [ControlBlock.h](file:///c:/MySrc/OuroKore/develop/core_cpp/core/src/ControlBlock.h)
控制區塊（靈魂）的私有 C++ 定義。
- `std::atomic<uint32_t> m_strong_count;`
- `std::atomic<uint32_t> m_weak_count;`
- `std::shared_mutex m_rw_lock;` (讀寫鎖)
- `OuroObject* m_payload;`
- `std::vector<HandleID> m_owners;` (Owner ID Roster，允許重複以支援多個邊)
- `std::mutex m_owners_mutex;` (保護名冊)

#### [NEW] [Registry.h](file:///c:/MySrc/OuroKore/develop/core_cpp/core/src/Registry.h) & [Registry.cpp](file:///c:/MySrc/OuroKore/develop/core_cpp/core/src/Registry.cpp)
全域註冊表，管理所有的控制區塊與其對應的隨機 `HandleID`。
- 提供安全執行緒的雜湊表（使用讀寫鎖保護的 `std::unordered_map<HandleID, ControlBlock*>`）。
- 實作隨機 ID 的核發與邊緣註冊/註銷。
- 實作強/弱引用計數的歸零回收邏輯（強引用歸零則銷毀 Payload，強弱皆歸零則釋放 Control Block）。

#### [NEW] [core.cpp](file:///c:/MySrc/OuroKore/develop/core_cpp/core/src/core.cpp)
- 實作 `core.h` 所宣告的 C ABI 接口。
- **全面異常攔截**：每一個 C 匯出函式內部都使用 `try { ... } catch (...) { ... }` 結構進行包覆，將 C++ 的任何異常轉化為 C ABI 錯誤碼回傳。
- **Thread-Local 執行期上下文**：提供執行緒局部變數 (TLS) 的全域管理方法，供 `ActiveOwnerContext` 查詢當前建構物件的 Parent ID。

---

## Verification Plan

### Automated Tests
我們將建立一個簡單的測試專案 `basic_tests` 來驗證核心記憶體管理行為。

#### [NEW] [CMakeLists.txt (tests)](file:///c:/MySrc/OuroKore/develop/core_cpp/tests/CMakeLists.txt)
在根目錄新增測試子目錄，編譯 `tests/basic_tests.cpp` 並連結 `ourokore_core`。

#### [NEW] [basic_tests.cpp](file:///c:/MySrc/OuroKore/develop/core_cpp/tests/basic_tests.cpp)
- **測試 1：物件生成與生命週期**
  - 驗證 `CreateObject` 回傳 `OuroPtr`。
  - 驗證外部 `OuroPtr` 銷毀時，Payload 被正確 delete。
- **測試 2：Owner ID Roster 的自動註冊與限制**
  - 建立父物件 A 與子欄位 B，驗證物件 B 的 Control Block 中包含物件 A 的 ID。
  - 驗證若在 Stack 上試圖初始化有效的 `OwningHandle`，會觸發執行期異常。
- **測試 3：WeakHandle 的 Lazy Pruning**
  - 建立 `WeakHandle` 並讓 `OuroPtr` 銷毀。
  - 呼叫 `IsAlive()`，驗證其回傳 `false` 且 `m_target_id` 自動清空，且弱引用計數扣減至 0。
- **測試 4：防範菱形繼承**
  - 驗證菱形繼承的類別無法通過 `CreateObject` 編譯。
