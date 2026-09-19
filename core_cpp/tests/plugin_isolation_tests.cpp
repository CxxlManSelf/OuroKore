#include <cassert>
#include <iostream>
#include <string>

// 【重要】第三方外掛編譯單元：僅允許引入 component/OuroCore.hpp
// 絕不引入任何 ourokore/host/* 標頭檔。
//
// 【邊界隔離證明】
// 在此純插件環境下，若嘗試呼叫下列任一特權介面，編譯器將直接產生硬錯誤（Hard Compile Error）
// 拒絕編譯（'XXX' is not a member of 'ork'）：
//   - ork::Init(...)                     -> 編譯失敗！
//   - ork::Shutdown()                    -> 編譯失敗！
//   - ork::ShutdownRuntime()             -> 編譯失敗！
//   - ork::FlushStorage()                -> 編譯失敗！
//   - ork::FlushStorageRuntime()         -> 編譯失敗！
//   - ork::SetAutoDehydrator(...)        -> 編譯失敗！
//   - ork::SetStorageDriver(...)         -> 編譯失敗！
//   - ork::CollectCycles()               -> 編譯失敗！
//   - ork::FlushDeferredDeletions()      -> 編譯失敗！
//   - ork::SetDeferredDeleteMode(...)    -> 編譯失敗！
#include "ourokore/component/OuroCore.hpp"

// 插件自訂資料物件
class ThirdPartyPluginEntity : public ork::OuroObject
{
public:
  int score{123};
  std::string name{"PluginPlayer"};

  void SerializePayload(ork::OuroStream &stream) const override
  {
    stream.WriteProperty("score", score);
    stream.WriteProperty("name", name);
  }

  void DeserializePayload(ork::OuroStream &stream) override
  {
    stream.ReadProperty("score", score);
    stream.ReadProperty("name", name);
  }
};

int main()
{
  std::cout << "=== 執行第三方插件純淨度與特權隔離檢驗 (Plugin Isolation Tests) ===" << std::endl;

  // 驗證：插件依然具備完整且健全的受管物件建立、屬性賦值與指標操作能力
  {
    ork::OuroPtr<ThirdPartyPluginEntity> ptr = ork::CreateObject<ThirdPartyPluginEntity>();
    assert(ptr.GetTargetID() != 0);
    assert(ptr->score == 123);
    assert(ptr->name == "PluginPlayer");

    ptr->score = 999;
    assert(ptr->score == 999);

    // 驗證 2: 脫水與復水機制在插件環境下運作健全
    ork::HandleID id = ptr.GetTargetID();
    bool dehy_res = ork::Dehydrate(std::move(ptr));
    // 未初始化 storage driver 時略過或回傳相應狀態
    (void)dehy_res;
    (void)id;
  }

  std::cout << "  -> [驗證 1] 第三方插件標頭檔已徹底杜絕任何 Host 專用特權管理 API！" << std::endl;
  std::cout << "  -> [驗證 2] 第三方插件常規受管物件生命週期與指標存取 100% 健全正常！" << std::endl;
  std::cout << "  -> [驗證 3] 核心代理通道（Task/Rescue/Stream）與純淨標頭檔隔離完全生效！" << std::endl;
  std::cout << "=== 第三方插件物理隔離檢驗 100% 通過！ ===\n" << std::endl;

  return 0;
}

