#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ourokore/base/Tree.hpp>
#include <ourokore/base/TreeIO.hpp>

using namespace ork::base;

void TestBasicTreeOperations()
{
  std::cout << "[測試 1] 基本樹節點操作與具名索引測試..." << std::endl;

  auto root = Tree::CreateRoot(u8"Root");
  assert(root != nullptr);
  assert(root->GetName() == u8"Root");
  assert(root->IsObject());

  // 增加具名字節點
  auto child1 = root->AddBackChild(u8"Child1");
  auto child2 = root->AddBackChild(u8"Child2");
  auto child0 = root->AddFrontChild(u8"Child0");

  assert(root->ChildCount() == 3);
  assert(root->HasChild(u8"Child0"));
  assert(root->HasChild(u8"Child1"));
  assert(root->HasChild(u8"Child2"));
  assert(!root->HasChild(u8"NonExistent"));

  // 測試名稱索引 O(1)
  assert(root->FindChildByName(u8"Child1") == child1);
  assert((*root)[u8"Child2"] == child2);

  // 測試資料存取
  child1->SetData("Hello World");
  assert(child1->GetData() == "Hello World");

  // 測試首尾存取
  assert(root->GetFirstChild() == child0);
  assert(root->GetLastChild() == child2);

  // 測試父節點反查
  assert(child1->GetParent() == root);

  // 測試移除節點
  assert(root->RemoveChildByName(u8"Child0"));
  assert(root->ChildCount() == 2);
  assert(!root->HasChild(u8"Child0"));

  std::cout << " -> 通過！" << std::endl;
}

void TestArrayOperations()
{
  std::cout << "[測試 2] 陣列形態與 O(1) 隨機下標存取測試..." << std::endl;

  auto array_node = Tree::CreateRoot(u8"Inventory", NodeKind::Array);
  assert(array_node->IsArray());
  assert(array_node->ElementCount() == 0);

  // 追加元素
  auto elem0 = array_node->PushElement();
  elem0->SetData("草藥");

  auto elem1 = array_node->PushElement();
  elem1->SetData("魔法卷軸");

  auto elem2 = array_node->PushElement();
  elem2->SetData("雙手大劍");

  assert(array_node->ElementCount() == 3);

  // 測試 O(1) 隨機下標存取
  assert((*array_node)[0]->GetData() == "草藥");
  assert((*array_node)[1]->GetData() == "魔法卷軸");
  assert((*array_node)[2]->GetData() == "雙手大劍");

  // 測試遍歷走訪
  std::vector<std::string> collected;
  array_node->ForEachElement([&collected](const auto &elem) {
    collected.push_back(elem->GetData());
  });
  assert(collected.size() == 3);
  assert(collected[0] == "草藥");
  assert(collected[1] == "魔法卷軸");
  assert(collected[2] == "雙手大劍");

  // 測試下標移除
  assert(array_node->RemoveElementAt(1));
  assert(array_node->ElementCount() == 2);
  assert((*array_node)[0]->GetData() == "草藥");
  assert((*array_node)[1]->GetData() == "雙手大劍");

  std::cout << " -> 通過！" << std::endl;
}

void TestTreeIOSerialization()
{
  std::cout << "[測試 3] TreeIO 序列化與反序列化測試（含物件與陣列）..." << std::endl;

  auto player = Tree::CreateRoot(u8"Player");
  player->SetData("英雄角色");

  auto hp = player->AddBackChild(u8"HP");
  hp->SetData("100");

  auto inventory = player->AddBackChild(u8"Inventory", NodeKind::Array);
  auto item1 = inventory->PushElement();
  item1->SetData("草藥");
  auto item2 = inventory->PushElement();
  item2->SetData("黃金盔甲");

  // 序列化成文字 DSL (預設縮排格式)
  std::ostringstream oss;
  TreeIO::Serialize(oss, player, [](const std::string &s) { return s; }, 2);
  std::string dsl = oss.str();

  std::cout << "DSL 匯出結果：\n" << dsl << std::endl;

  // 反序列化解析
  std::istringstream iss(dsl);
  auto restored = TreeIO::Deserialize(iss);

  assert(restored != nullptr);
  assert(restored->GetName() == u8"Player");
  assert(restored->GetData() == "英雄角色");
  assert(restored->HasChild(u8"HP"));
  assert((*restored)[u8"HP"]->GetData() == "100");

  auto restored_inv = (*restored)[u8"Inventory"];
  assert(restored_inv != nullptr);
  assert(restored_inv->IsArray());
  assert(restored_inv->ElementCount() == 2);
  assert((*restored_inv)[0]->GetData() == "草藥");
  assert((*restored_inv)[1]->GetData() == "黃金盔甲");

  // --- 測試 3 種緊湊模式 (CompactMode) ---
  // 1. 保留 = 的緊湊模式 (WithEqual)
  std::ostringstream oss_with_eq;
  TreeIO::Serialize(oss_with_eq, player, CompactMode::WithEqual);
  std::string dsl_with_eq = oss_with_eq.str();
  std::cout << "Compact (WithEqual) DSL: " << dsl_with_eq << std::endl;
  assert(dsl_with_eq == "[Player]=\"英雄角色\"{[HP]=\"100\"[Inventory](\"草藥\"\"黃金盔甲\")}");

  // 2. 不保留 = 的極致緊湊模式 (WithoutEqual)
  std::ostringstream oss_without_eq;
  TreeIO::Serialize(oss_without_eq, player, CompactMode::WithoutEqual);
  std::string dsl_without_eq = oss_without_eq.str();
  std::cout << "Compact (WithoutEqual) DSL: " << dsl_without_eq << std::endl;
  assert(dsl_without_eq == "[Player]\"英雄角色\"{[HP]\"100\"[Inventory](\"草藥\"\"黃金盔甲\")}");

  // 驗證不保留等號模式的字串完全能被成功反序列化還原！
  auto restored_without_eq = TreeIO::DeserializeFromString(dsl_without_eq);
  assert(restored_without_eq != nullptr);
  assert(restored_without_eq->GetName() == u8"Player");
  assert(restored_without_eq->GetData() == "英雄角色");
  assert((*restored_without_eq)[u8"HP"]->GetData() == "100");
  assert((*(*restored_without_eq)[u8"Inventory"])[0]->GetData() == "草藥");
  assert((*(*restored_without_eq)[u8"Inventory"])[1]->GetData() == "黃金盔甲");

  // 測試 SerializeCompact 便捷函式 (預設為 WithEqual)
  std::ostringstream oss_compact2;
  TreeIO::SerializeCompact(oss_compact2, player);
  assert(oss_compact2.str() == dsl_with_eq);

  // 測試 SerializeCompact 指定 WithoutEqual
  std::ostringstream oss_compact3;
  TreeIO::SerializeCompact(oss_compact3, player, CompactMode::WithoutEqual);
  assert(oss_compact3.str() == dsl_without_eq);

  std::cout << " -> 通過！" << std::endl;
}

void TestFaultTolerantFSM()
{
  std::cout << "[測試 4] 寬容型狀態機過濾雜訊與非法字元測試..." << std::endl;

  // 測試包含任意雜訊、多餘引號、多餘節點名、無效符號的文字
  std::string messy_dsl = R"(
    這是一段任意說明文字，不在狀態內應全數無視！
    [GameConfig] = [多餘無視標籤] "版本 1.0.0" "第二段引號視為多餘無視"
    {
        這裡是物件內部的說明文字，無視！
        [ServerIP] === "127.0.0.1" @#$%^&*
        [Port] = "8080"

        // 陣列元素測試
        [Blacklist] = (
            "192.168.1.100"
            這一段純文字被無視
            "10.0.0.5"
            [SpecialIP] "172.16.0.1"
        )
    }
  )";

  auto root = TreeIO::DeserializeFromString(messy_dsl);
  assert(root != nullptr);
  assert(root->GetName() == u8"GameConfig");
  assert(root->GetData() == "版本 1.0.0");
  assert(root->HasChild(u8"ServerIP"));
  assert((*root)[u8"ServerIP"]->GetData() == "127.0.0.1");
  assert((*root)[u8"Port"]->GetData() == "8080");

  auto bl = (*root)[u8"Blacklist"];
  assert(bl != nullptr);
  assert(bl->IsArray());
  assert(bl->ElementCount() == 3);
  assert((*bl)[0]->GetData() == "192.168.1.100");
  assert((*bl)[1]->GetData() == "10.0.0.5");
  assert((*bl)[2]->GetData() == "172.16.0.1");
  assert((*bl)[2]->GetName() == u8"SpecialIP");

  std::cout << " -> 通過！" << std::endl;
}

void TestBinaryAndEscapeHandling()
{
  std::cout << "[測試 5] 0~255 二進位位元組與脫字元安全測試..." << std::endl;

  auto root = Tree::CreateRoot(u8"Special\\]Node");
  // 建立包含引號、反斜線、換行與 0x00 空字元的 Payload
  std::string binary_data = "Line1\nLine2\t\"Quotes\"\\\\Path\\to\\file";
  binary_data.push_back('\0');
  binary_data += "AfterNull";

  root->SetData(binary_data);

  std::ostringstream oss;
  TreeIO::Serialize(oss, root);
  std::string dsl = oss.str();

  std::istringstream iss(dsl);
  auto restored = TreeIO::Deserialize(iss);

  assert(restored != nullptr);
  assert(restored->GetName() == u8"Special]Node");
  assert(restored->GetData() == binary_data);

  std::cout << " -> 通過！" << std::endl;
}

void TestConcurrencySafety()
{
  std::cout << "[測試 6] 多執行緒並發讀寫鎖安全測試..." << std::endl;

  auto root = Tree::CreateRoot(u8"SharedRoot");
  for (int i = 0; i < 50; ++i)
  {
    std::string name = "Node" + std::to_string(i);
    root->AddBackChild(ork::utf8::to_u8string(name))->SetData("Init");
  }

  std::vector<std::thread> threads;
  // 讀取執行緒
  for (int t = 0; t < 4; ++t)
  {
    threads.emplace_back([root]() {
      for (int i = 0; i < 200; ++i)
      {
        int target = i % 50;
        std::string name = "Node" + std::to_string(target);
        auto child = (*root)[ork::utf8::to_u8string(name)];
        if (child)
        {
          std::string data = child->GetData();
          assert(!data.empty());
        }
      }
    });
  }

  // 寫入執行緒
  for (int t = 0; t < 2; ++t)
  {
    threads.emplace_back([root, t]() {
      for (int i = 0; i < 100; ++i)
      {
        int target = (i + t * 10) % 50;
        std::string name = "Node" + std::to_string(target);
        auto child = (*root)[ork::utf8::to_u8string(name)];
        if (child)
        {
          child->SetData("Updated_" + std::to_string(i));
        }
      }
    });
  }

  for (auto &t : threads)
  {
    t.join();
  }

  std::cout << " -> 通過！" << std::endl;
}

void TestDeepTreeDestruction()
{
  std::cout << "[測試 7] 深層階層非同步防爆棧析構測試..." << std::endl;

  {
    auto root = Tree::CreateRoot(u8"DeepRoot");
    auto current = root;
    for (int i = 0; i < 5000; ++i)
    {
      current = current->AddBackChild(u8"DeepChild");
    }
  }

  // 等待非同步析構排空
  AsyncNodeDeletor::Wait();

  std::cout << " -> 通過！" << std::endl;
}

void TestUnifiedDualMode()
{
  std::cout << "[測試 8] 單一容器雙模態統合、互通性與形態自動推導測試..." << std::endl;

  auto hero = Tree::CreateRoot(u8"Hero");
  assert(hero->IsObject());  // 空容器預設為 Object

  // 1. 新增具名子節點
  auto hp = hero->AddBackChild(u8"HP");
  hp->SetData("100");
  assert(hero->IsObject());  // 只有具名，維持 Object
  assert(hero->ChildCount() == 1);
  assert(hero->ElementCount() == 1);

  // 2. 混入匿名元素
  auto item0 = hero->PushElement();
  item0->SetData("生鏽鐵劍");
  // 混入匿名元素後，自動判定為陣列模式！(elements.size() > nameMap.size())
  assert(hero->IsArray());
  assert(!hero->IsObject());
  assert(hero->Size() == 2);

  // 3. 測試下標與名稱存取的互通自洽性
  assert((*hero)[0] == hp);                     // 下標 0 拿到的就是 HP 節點！
  assert((*hero)[u8"HP"] == hp);               // 名稱 HP 拿到的也是同一個 HP 節點！
  assert((*hero)[1] == item0);                  // 下標 1 拿到匿名元素
  assert((*hero)[0]->GetData() == "100");
  assert((*hero)[1]->GetData() == "生鏽鐵劍");

  // 4. 混合結構序列化（陣列小括號模式，具名與匿名共存）
  std::ostringstream oss;
  TreeIO::SerializeCompact(oss, hero);
  std::string compact_dsl = oss.str();
  std::cout << "  混合結構 Compact DSL: " << compact_dsl << std::endl;
  assert(compact_dsl == "[Hero]{[HP]=\"100\"\"生鏽鐵劍\"}" || compact_dsl == "[Hero]([HP]=\"100\"\"生鏽鐵劍\")");

  // 5. 反序列化驗證
  auto restored = TreeIO::DeserializeFromString(compact_dsl);
  assert(restored != nullptr);
  assert(restored->Size() == 2);
  assert((*restored)[0]->GetData() == "100");
  assert((*restored)[u8"HP"]->GetData() == "100");
  assert((*restored)[1]->GetData() == "生鏽鐵劍");

  std::cout << " -> 通過！" << std::endl;
}

int main()
{
  std::cout << "========================================" << std::endl;
  std::cout << "  OuroKore Tree & TreeIO 單元測試開始  " << std::endl;
  std::cout << "========================================" << std::endl;

  TestBasicTreeOperations();
  TestArrayOperations();
  TestTreeIOSerialization();
  TestFaultTolerantFSM();
  TestBinaryAndEscapeHandling();
  TestConcurrencySafety();
  TestDeepTreeDestruction();
  TestUnifiedDualMode();

  std::cout << "========================================" << std::endl;
  std::cout << "  全數 8 項單元測試 100% 成功通過！    " << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
