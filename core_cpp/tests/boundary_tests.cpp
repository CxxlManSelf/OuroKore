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
  test_unauthorized([&]() { plugin_host.GetCycleSuspectCount(); }, "GetCycleSuspectCount");
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

  // 8. 驗證 HostContext::Reset() 與核心重新初始化（軟重啟）
  {
    auto restart_storage = std::make_shared<ork::InMemoryStorage>();
    ork::HostContext host2 = ork::Init(restart_storage);
    assert(host2.IsValid() == true);
    auto item2 = ork::CreateObject<PluginItem>();
    item2->value = 777;
    assert(ork::Save(item2) == true);

    // 執行 Reset，驗證能優雅收斂並重置白紙狀態
    host2.Reset();
    assert(host2.IsValid() == false);

    // 驗證 Reset 後舊物件受 CheckOwner 保護
    test_unauthorized([&]() { host2.FlushStorage(); }, "FlushStorage on Reset HostContext");

    // 驗證 Reset 後可順利再次 Init
    auto restart_storage2 = std::make_shared<ork::InMemoryStorage>();
    ork::HostContext host3 = ork::Init(restart_storage2);
    assert(host3.IsValid() == true);
    host3.Shutdown();
    std::cout << "  -> HostContext::Reset() 與軟重啟再次初始化驗證通過。" << std::endl;
  }

  // 9. 驗證 OuroCreationToken Passkey 編譯期私有與存取控制防禦
  {
    static_assert(!std::is_default_constructible_v<ork::detail::OuroCreationToken>,
                  "OuroCreationToken must NOT be default constructible!");
    static_assert(!std::is_constructible_v<ork::detail::OuroCreationToken, ork::HandleID>,
                  "OuroCreationToken must NOT be constructible from HandleID by public callers!");
    std::cout << "  -> OuroCreationToken Passkey 編譯期私有封鎖驗證通過（外部插件無法構造）。" << std::endl;
  }

  std::cout << "=== 所有邊界隔離與特權防禦測試全部順利通過！ ===" << std::endl;
  return 0;
}
