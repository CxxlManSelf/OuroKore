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

#### [MODIFY] [basic_tests.cpp](file:///c:/MySrc/OuroKore/develop/core_cpp/tests/basic_tests.cpp)
在測試主程式中，新增 `Test 8`, `Test 9` 與 `Test 10` 以驗證 `OwningHandle` 在容器 (如 `std::vector`) 中的使用行為。

---

## OwningHandle 陣列與容器使用測試計畫

針對 `OwningHandle` 在 `std::vector` 等容器中的使用情境（如擴容、批次插入、同/跨宿主 Move 等），我們將在 `tests/basic_tests.cpp` 中新增以下測試案例以驗證規格：

### 1. Test 8: ActiveOwnerGuard 容器批次插入測試
- **目的**：驗證使用 `ActiveOwnerGuard` 在 Thread-Local 暫時綁定宿主 (Owner) ID 後，於 `std::vector<ork::OwningHandle<SimpleObject>>` 批量 `push_back`/`emplace_back` 物件時，這些 Handle 都能自動綁定正確的宿主 ID。
- **步驟**：
  1. 宣告虛擬宿主 ID (例如 `8888`) 並建立 `ActiveOwnerGuard guard(8888);`。
  2. 建立數個 `SimpleObject` 並呼叫 `vector::emplace_back(child_ptr)`。
  3. 驗證所有 vector 內 `OwningHandle` 的 `GetOwnerID()` 皆為 `8888`，且目標物件的強引用計數為 1。

### 2. Test 9: 容器擴容之同宿主 Zero-Cost Move 測試
- **目的**：驗證當 `std::vector` 因動態擴容（Reallocation）導致記憶體重新分配、搬移 `OwningHandle` 時，由於 Owner ID 相同（皆為相同宿主），系統不會重複向底層 C API 註冊或解除邊，實現零效能開銷。
- **步驟**：
  1. 宣告宿主 ID 並建立 `ActiveOwnerGuard`。
  2. 建立 `std::vector<ork::OwningHandle<SimpleObject>>`，但不呼叫 `reserve`。
  3. 批量新增物件，使 `vector` 的 size 超過 capacity，觸發擴容重新分配。
  4. 驗證在此過程中：
     - 沒有任何物件因為 `Release` 被意外析構（`g_deconstruct_count` 依然為 0）。
     - 所有物件依然正常存活。
     - 手動模擬同宿主 `std::move`，驗證目標轉移成功，來源置零，且強引用計數並未被額外增減。

### 3. Test 10: 跨宿主 Move 語意與舊目標 Release 測試
- **目的**：驗證 `OwningHandle` 的跨宿主移動賦值運算子能正確釋放舊目標、轉移 Target ID、將來源置零，並正確在底層 Registry 中將有向邊由舊宿主更新為新宿主。
- **步驟**：
  1. 建立兩個不同宿主 (如 `parent1` 與 `parent2`)。
  2. 將 `childA` 綁定給 `parent1` 的 `OwningHandle`，`childB` 綁定給 `parent2` 的 `OwningHandle`。
  3. 將 `childB` 從 `parent2` Move 到 `parent1` 原本持有 `childA` 的 Handle 上：`parent1_handle = std::move(parent2_handle);`。
  4. 驗證：
     - `childA` 的強引用歸零並被析構（`g_deconstruct_count` 增加）。
     - `childB` 的宿主在 Registry 中被更新為 `parent1`。
     - `parent2_handle` 的 target 被安全置零。

---

## Verification Plan

### Automated Tests
- 在 `c:\MySrc\OuroKore\develop\core_cpp\build` 目錄下執行編譯及測試：
  ```powershell
  cmake --build . --config Debug
  ./bin/basic_tests
  ```

### Manual Verification
- 檢查測試輸出，確保 Test 8、Test 9 與 Test 10 皆成功印出 "Test X Passed"。
