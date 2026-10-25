#pragma once

#include <memory>
#include <string_view>
#include "ourokore/base/Result.hpp"
#include "ourokore/component/Types.hpp"

namespace ork
{

/**
 * @brief OuroKore 標準零件與 WebAssembly 沙盒物件提供者介面 (Capability-Based IPartProvider)
 *
 * 【設計哲學與安全架構規範】
 * 1. OuroKore 官方標準契約：
 *    本介面定義了 OuroKore 生態中「零件物件創世與永續復原」的抽象能力契約，
 *    屬於 SDK 核心標準規格，不綁定任何特定底層外掛（如 OrkGatewayDriver）。
 * 2. 最小特權原則 (ISP) 與跨沙盒安全：
 *    本介面僅開放「全新零件創世」與「永續資料復原」兩大功能，完全隔離核心的進程級特權 (Shutdown、GC 等)。
 *    宿主主程式可將此介面實體作為受管能力權柄（Object Capability），安全傳遞給 WebAssembly (WASM)
 *    沙盒環境或第三方組裝工廠使用。
 * 3. 雙向身分體系相容：
 *    產出之物件生命週期完整受 64 位元整數 HandleID、ControlBlock、讀寫鎖與 CycleCollector 守護。
 */
class IPartProvider
{
public:
  virtual ~IPartProvider() = default;

  /**
   * @brief 1. 取得全新零件物件 (創世生成)
   *
   * 依據三段式工廠標籤定位目標模組（或 WASM 二進位 Bytecode），實例化並登記至核心圖拓撲。
   *
   * @param factory_tag 三段式工廠提示 (Vendor/Plugin/Version)，例如 "Epic/Physics/v1.0"
   * @param init_params 初始化參數 (文字 DSL、JSON 或二進位 Payload，預設為空)
   * @return 成功回傳受管實體之 HandleID；失敗回傳強型別 Status (如 NotFound、InvalidArgument)
   */
  virtual Result<HandleID> CreateNewPart(
      std::string_view factory_tag,
      std::string_view init_params = "") = 0;

  /**
   * @brief 2. 經由永續資料得到零件物件 (CAS 藍圖/狀態還原)
   *
   * 依據內容定址 CIDv1 或儲存庫識別碼檢索藍圖，還原物件狀態並接回 OuroKore 託管網狀圖。
   *
   * @param persistence_id 永續識別標識 (如 CAS 標準 CIDv1 雜湊字串，或儲存庫實體識別碼)
   * @return 成功回傳復水/還原後之受管實體 HandleID；失敗回傳強型別 Status
   */
  virtual Result<HandleID> RestorePart(
      std::string_view persistence_id) = 0;
};

}  // namespace ork
