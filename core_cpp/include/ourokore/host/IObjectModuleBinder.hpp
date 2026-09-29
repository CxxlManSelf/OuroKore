#pragma once

#include <memory>
#include "ourokore/base/DynamicLibrary.hpp"
#include "ourokore/component/Types.hpp"

namespace ork
{

/**
 * @brief 物件模組載入器專屬綁定介面（最小特權原則）
 *
 * 專為專職外掛管理單元（如 PluginManager、ComponentFactory）設計之輕量權限介面。
 * 允許其在動態庫中建立受管物件後，將動態庫實例錨定至物件的 ControlBlock 墓碑中，
 * 避免外洩具備進程級停機與破壞性特權之完整 HostContext。
 */
class IObjectModuleBinder
{
public:
  virtual ~IObjectModuleBinder() = default;

  /**
   * @brief 為特定受管物件綁定動態模組載入器（錨定 DLL 生命週期於 ControlBlock 墓碑）
   * @param id 目標受管物件之 HandleID
   * @param loader 動態庫載入器實例
   */
  virtual void SetObjectModuleLoader(HandleID id, const ork::DynamicLibrary &loader) = 0;

  /**
   * @brief 取得目標物件當前綁定之動態模組載入器
   * @param id 目標受管物件之 HandleID
   * @return 若有綁定傳回 DynamicLibrary 實例；若未綁定或物件不存在傳回空實例
   */
  virtual ork::DynamicLibrary GetObjectModuleLoader(HandleID id) const = 0;
};

}  // namespace ork
