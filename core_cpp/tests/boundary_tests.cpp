#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

#include "ourokore/host/OuroHost.hpp"
#include "ourokore/component/builtin/InMemoryStorage.hpp"
#include "ourokore/component/builtin/NoOpAutoDehydrator.hpp"

class PluginItem : public ork::OuroObject
{
public:
  int value{100};
  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty("value", value);
  }
  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty("value", value);
  }
};

int main()
{
  std::cout << "=== 執行 OuroKore 外掛邊界隔離與特權防禦測試 (Boundary Tests) ===" << std::endl;

  // 1. 編譯期驗證：HostContext 必須為 Move-Only（禁止複製，保證主控權唯一）
  static_assert(!std::is_copy_constructible_v<ork::HostContext>, "HostContext must NOT be copy-constructible!");
  static_assert(!std::is_copy_assignable_v<ork::HostContext>, "HostContext must NOT be copy-assignable!");
  static_assert(std::is_move_constructible_v<ork::HostContext>, "HostContext must be move-constructible.");
  static_assert(std::is_move_assignable_v<ork::HostContext>, "HostContext must be move-assignable.");

  // 2. 主程式以標準身分啟動核心，取得合法之 HostContext
  auto storage = std::make_shared<ork::InMemoryStorage>();
  auto dehydrator = std::make_shared<ork::NoOpAutoDehydrator>();
  auto host = ork::Init(storage, dehydrator);

  assert(host.IsValid() == true);
  assert(bool(host) == true);
  std::cout << "  -> 主程式成功取得合法 HostContext。" << std::endl;

  // 3. 模擬第三方外掛（Plugin）呼叫 ork::Init 嘗試取得控制權
  auto plugin_storage = std::make_shared<ork::InMemoryStorage>();
  auto plugin_dehydrator = std::make_shared<ork::NoOpAutoDehydrator>();
  auto plugin_host = ork::Init(plugin_storage, plugin_dehydrator);

  // 外掛取得之 HostContext 必須無效
  assert(plugin_host.IsValid() == false);
  assert(!plugin_host);
  std::cout << "  -> 第三方外掛二次呼叫 Init() 成功被核心拒絕，獲得無效 HostContext。" << std::endl;

  // 4. 驗證外掛嘗試透過無效 HostContext 呼叫任何特權方法，一律拋出例外拒絕
  auto test_unauthorized = [](auto &&fn, const char *name) {
    bool caught = false;
    try
    {
      fn();
    }
    catch (const std::runtime_error &e)
    {
      caught = true;
    }
    assert(caught && "Unauthorized call MUST throw std::runtime_error!");
    std::cout << "     * 成功阻斷外掛未授權調用: " << name << std::endl;
  };

  test_unauthorized([&]() { plugin_host.Shutdown(); }, "Shutdown");
  test_unauthorized([&]() { plugin_host.FlushStorage(); }, "FlushStorage");
  test_unauthorized([&]() { plugin_host.FlushDeferredDeletions(); }, "FlushDeferredDeletions");
  test_unauthorized([&]() { plugin_host.SetDeferredDeleteMode(true); }, "SetDeferredDeleteMode");
  test_unauthorized([&]() { plugin_host.CollectCycles(); }, "CollectCycles");
  test_unauthorized([&]() { plugin_host.SetAutoDehydrator(plugin_dehydrator); }, "SetAutoDehydrator");
  test_unauthorized([&]() { plugin_host.GetAutoDehydrator(); }, "GetAutoDehydrator");
  test_unauthorized([&]() { plugin_host.SetStorageDriver(plugin_storage); }, "SetStorageDriver");
  test_unauthorized([&]() { plugin_host.GetStorageDriver(); }, "GetStorageDriver");
  test_unauthorized([&]() { plugin_host.GetThreadPool(); }, "GetThreadPool");

  // 5. 驗證第三方外掛雖無特權，但正常業務功能（物件建立、CRUD、Save/Load）完全不受影響
  auto item = ork::CreateObject<PluginItem>();
  item->value = 999;
  assert(ork::Save(item) == true);
  assert(storage->Contains(item.GetTargetID()) == true);
  std::cout << "  -> 第三方外掛之常規物件建立與業務存檔功能運作正常。" << std::endl;

  // 6. 驗證 HostContext 移動語意（所有權轉移）
  ork::HostContext moved_host = std::move(host);
  assert(moved_host.IsValid() == true);
  assert(host.IsValid() == false);

  // 原物件已轉移，呼叫轉移後的舊物件亦會拋出例外
  test_unauthorized([&]() { host.FlushStorage(); }, "FlushStorage on Moved-from HostContext");

  // 新物件正常執行特權操作
  moved_host.FlushStorage();
  std::cout << "  -> HostContext 移動語意與所有權轉移安全驗證通過。" << std::endl;

  // 7. 優雅終止
  moved_host.Shutdown();
  assert(moved_host.IsValid() == false);
  std::cout << "  -> 優雅終止成功完成。" << std::endl;

  std::cout << "=== 所有邊界隔離與特權防禦測試全部順利通過！ ===" << std::endl;
  return 0;
}
