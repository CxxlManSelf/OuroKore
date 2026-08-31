#pragma once

#include "IAutoDehydrator.hpp"

namespace ork
{

/**
 * @brief 預設空實作外掛模組 (No-Op Auto Dehydrator)
 *
 * 什麼都不做，所有操作均為空實作，回傳值為 0 或 false。
 * 作為系統未指定自訂策略時的預設實例，確保極致純淨、零開銷與零副作用。
 */
class NoOpAutoDehydrator : public IAutoDehydrator
{
public:
  void Register(HandleID /*id*/, size_t /*size_bytes*/) override {}
  void Unregister(HandleID /*id*/) override {}
  bool IsTracked(HandleID /*id*/) const override { return false; }
  size_t GetTrackedMemoryBytes() const override { return 0; }

  void OnObjectAccess(HandleID /*id*/) override {}
  size_t TriggerDehydration() override { return 0; }
};

}  // namespace ork
