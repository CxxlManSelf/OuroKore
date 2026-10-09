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

  auto root = StringTreeNode::CreateRoot(u8"Root");
  assert(root != nullptr);
  assert(root->GetName() == u8"Root");

  // 增加具名字節點 (AddChild, PrependChild 與 InsertBefore)
  auto child1 = root->AddChild(u8"Child1");
  auto child2 = root->AddChild(u8"Child2");
  auto child0 = root->InsertBefore(child1, u8"Child0");
  auto head = root->PrependChild(u8"Head");

  assert(root->ChildCount() == 4);
  assert(root->GetFirstChild() == head);
  assert(root->HasChild(u8"Head"));
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
  assert(root->GetFirstChild() == head);
  assert(root->GetLastChild() == child2);

  // 測試父節點反查
  assert(child1->GetParent() == root);

  // 測試移除節點
  assert(root->RemoveChildByName(u8"Child0"));
  assert(root->RemoveChildByName(u8"Head"));
  assert(root->ChildCount() == 2);
  assert(!root->HasChild(u8"Child0"));
  assert(!root->HasChild(u8"Head"));

  std::cout << " -> 通過！" << std::endl;
}

void TestArrayOperations()
{
  std::cout << "[測試 4] 陣列形態與 O(1) 隨機下標存取測試..." << std::endl;

  auto array_node = StringTreeNode::CreateRoot(u8"Inventory");
  assert(array_node->ChildCount() == 0);

  // 追加元素（由資料內容自然驅動為陣列）
  auto elem0 = array_node->PushElement();
  elem0->SetData("草藥");

  auto elem1 = array_node->PushElement();
  elem1->SetData("魔法卷軸");

  auto elem2 = array_node->PushElement();
  elem2->SetData("雙手大劍");

  assert(array_node->ChildCount() == 3);

  // 測試 O(1) 隨機下標存取
  assert((*array_node)[0]->GetData() == "草藥");
  assert((*array_node)[1]->GetData() == "魔法卷軸");
  assert((*array_node)[2]->GetData() == "雙手大劍");

  // 測試遍歷走訪（使用 GetTreeMutex 讀鎖搭配標準 STL range-for）
  std::vector<std::string> collected;
  {
    std::shared_lock<std::shared_mutex> lock(array_node->GetTreeMutex());
    for (const auto &elem : *array_node)
    {
      if (elem)
      {
        collected.push_back(elem->GetData());
      }
    }
  }
  assert(collected.size() == 3);
  assert(collected[0] == "草藥");
  assert(collected[1] == "魔法卷軸");
  assert(collected[2] == "雙手大劍");

  // 測試反向走訪（使用 node->Reversed() 視圖糖衣）
  std::vector<std::string> rev_collected;
  {
    std::shared_lock<std::shared_mutex> lock(array_node->GetTreeMutex());
    for (const auto &elem : array_node->Reversed())
    {
      if (elem)
      {
        rev_collected.push_back(elem->GetData());
      }
    }
  }
  assert(rev_collected.size() == 3);
  assert(rev_collected[0] == "雙手大劍");
  assert(rev_collected[1] == "魔法卷軸");
  assert(rev_collected[2] == "草藥");

  // 測試下標移除
  assert(array_node->RemoveElementAt(1));
  assert(array_node->ChildCount() == 2);
  assert((*array_node)[0]->GetData() == "草藥");
  assert((*array_node)[1]->GetData() == "雙手大劍");

  std::cout << " -> 通過！" << std::endl;
}

void TestTreeIOSerialization()
{
  std::cout << "[測試 5] TreeIO 序列化與反序列化測試（含物件與陣列）..." << std::endl;

  auto player = StringTreeNode::CreateRoot(u8"Player");
  player->SetData("英雄角色");

  auto hp = player->AddChild(u8"HP");
  hp->SetData("100");

  auto inventory = player->AddChild(u8"Inventory");
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
  assert(restored_inv->ChildCount() == 2);
  assert((*restored_inv)[0]->GetData() == "草藥");
  assert((*restored_inv)[1]->GetData() == "黃金盔甲");

  // --- 測試緊湊模式 (CompactMode::Compact) ---
  std::ostringstream oss_compact;
  TreeIO::Serialize(oss_compact, player, CompactMode::Compact);
  std::string dsl_compact = oss_compact.str();
  std::cout << "Compact DSL: " << dsl_compact << std::endl;
  assert(dsl_compact == "[Player]=\"英雄角色\"{[HP]=\"100\"[Inventory]{\"草藥\"\"黃金盔甲\"}}");

  // 驗證緊湊字串反序列化還原！
  auto restored_compact = TreeIO::DeserializeFromString(dsl_compact);
  assert(restored_compact != nullptr);
  assert(restored_compact->GetName() == u8"Player");
  assert(restored_compact->GetData() == "英雄角色");
  assert((*restored_compact)[u8"HP"]->GetData() == "100");
  assert((*(*restored_compact)[u8"Inventory"])[0]->GetData() == "草藥");
  assert((*(*restored_compact)[u8"Inventory"])[1]->GetData() == "黃金盔甲");

  // 測試 SerializeCompact 便捷函式
  std::ostringstream oss_compact2;
  TreeIO::SerializeCompact(oss_compact2, player);
  assert(oss_compact2.str() == dsl_compact);

  std::cout << " -> 通過！" << std::endl;
}

void TestFaultTolerantFSM()
{
  std::cout << "[測試 6] 寬容型狀態機過濾雜訊與非法字元測試..." << std::endl;

  // 測試包含任意雜訊、多餘引號、多餘節點名、無效符號，以及包含 DSL 界定符的各類註解文字
  std::string messy_dsl = R"(
    // 單行註解測試：請勿載入 [FakeRoot] = "錯誤資料" { [FakeChild] = "999" }
    /* 區塊註解測試：
       [BlockedNode] = "被區塊註解阻擋" ( "陣列雜訊" )
    */
    # 腳本風格單行註解：# [HashNode] "HashData"

    這是一段任意說明文字，不在狀態內應全數無視！
    [GameConfig] = @$%~多餘無視符號說明~$%@ "版本 1.0.0" /* 註解避開多餘引號干擾 */
    {
        這裡是物件內部的說明文字，無視！
        // 物件內部行註解 [FakeInside] = "無效"
        /* 物件內部區塊註解 "FakeQuote" */
        [ServerIP] === "127.0.0.1" @#$%^&* // 行尾註解：注意伺服器 IP
        [Port] = "8080" # 行尾腳本註解 [IgnorePort]

        // 子節點清單測試（使用標準大括號容器與註解）
        [Blacklist] = {
            "192.168.1.100" // 列表行尾註解
            /* 區塊註解被註解掉的項目："999.999.999.999" */
            這一段純文字被無視
            "10.0.0.5"
            # 腳本註解被略過的項目："1.1.1.1"
            [SpecialIP] = "172.16.0.1"
        }
    }
  )";

  auto root = TreeIO::DeserializeFromString(messy_dsl);
  assert(root != nullptr);
  assert(root->GetName() == u8"GameConfig");
  assert(root->GetData() == "版本 1.0.0");
  assert(!root->HasChild(u8"FakeRoot"));
  assert(!root->HasChild(u8"BlockedNode"));
  assert(!root->HasChild(u8"FakeInside"));
  assert(root->HasChild(u8"ServerIP"));
  assert((*root)[u8"ServerIP"]->GetData() == "127.0.0.1");
  assert(root->HasChild(u8"Port"));
  assert((*root)[u8"Port"]->GetData() == "8080");

  auto bl = (*root)[u8"Blacklist"];
  assert(bl != nullptr);
  assert(bl->ChildCount() == 3);
  assert((*bl)[0]->GetData() == "192.168.1.100");
  assert((*bl)[1]->GetData() == "10.0.0.5");
  assert((*bl)[2]->GetData() == "172.16.0.1");
  assert((*bl)[2]->GetName() == u8"SpecialIP");

  std::cout << " -> 通過！" << std::endl;
}

void TestBinaryAndEscapeHandling()
{
  std::cout << "[測試 7] 0~255 二進位位元組與脫字元安全測試..." << std::endl;

  auto root = StringTreeNode::CreateRoot(u8"Special]Node");
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
  std::cout << "[測試 3] 多執行緒並發讀寫鎖安全測試..." << std::endl;

  auto root = StringTreeNode::CreateRoot(u8"SharedRoot");
  for (int i = 0; i < 50; ++i)
  {
    std::string name = "Node" + std::to_string(i);
    root->AddChild(ork::utf8::to_u8string(name))->SetData("Init");
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
  std::cout << "[測試 8] 深層階層顯式堆疊迭代防爆棧析構測試 (20,000 層)..." << std::endl;

  {
    auto root = StringTreeNode::CreateRoot(u8"DeepRoot");
    auto current = root;
    for (int i = 0; i < 20000; ++i)
    {
      current = current->AddChild(u8"DeepChild");
    }
  }

  // 展平析構為同步安全完成

  std::cout << " -> 通過！" << std::endl;
}

void TestUnifiedDualMode()
{
  std::cout << "[測試 9] 單一容器雙模態統合、互通性與形態自動推導測試..." << std::endl;

  auto hero = StringTreeNode::CreateRoot(u8"Hero");

  // 1. 新增具名子節點
  auto hp = hero->AddChild(u8"HP");
  hp->SetData("100");
  assert(hero->ChildCount() == 1);
  assert(hero->Size() == 1);

  // 2. 混入匿名元素
  auto item0 = hero->PushElement();
  item0->SetData("生鏽鐵劍");
  assert(hero->Size() == 2);

  // 3. 測試下標與名稱存取的互通自洽性
  assert((*hero)[0] == hp);                     // 下標 0 拿到的就是 HP 節點！
  assert((*hero)[u8"HP"] == hp);               // 名稱 HP 拿到的也是同一個 HP 節點！
  assert((*hero)[1] == item0);                  // 下標 1 拿到匿名元素
  assert((*hero)[0]->GetData() == "100");
  assert((*hero)[1]->GetData() == "生鏽鐵劍");

  // 4. 混合結構序列化（統一使用大括號，具名與匿名共存）
  std::ostringstream oss;
  TreeIO::SerializeCompact(oss, hero);
  std::string compact_dsl = oss.str();
  std::cout << "  混合結構 Compact DSL: " << compact_dsl << std::endl;
  assert(compact_dsl == "[Hero]{[HP]=\"100\"\"生鏽鐵劍\"}");

  // 5. 反序列化驗證
  auto restored = TreeIO::DeserializeFromString(compact_dsl);
  assert(restored != nullptr);
  assert(restored->Size() == 2);
  assert((*restored)[0]->GetData() == "100");
  assert((*restored)[u8"HP"]->GetData() == "100");
  assert((*restored)[1]->GetData() == "生鏽鐵劍");

  std::cout << " -> 通過！" << std::endl;
}

// 應用端自定義的 CRTP 領域資料節點類別
class CustomEntityNode : public TreeNodeBase<CustomEntityNode>
{
public:
  std::string entity_tag;
  int level{1};

  explicit CustomEntityNode(std::u8string name = u8"") :
      TreeNodeBase<CustomEntityNode>(std::move(name))
  {
  }

  void SetData(const std::string &data)
  {
    entity_tag = data;
  }

  [[nodiscard]] std::string GetData() const
  {
    return entity_tag;
  }
};

void TestCustomCRTPNode()
{
  std::cout << "[測試 10] 自定義 CRTP 節點延伸類別替換與 TreeIO 序列化/反序列化測試..." << std::endl;

  // 1. 應用端使用自定義節點建立樹
  auto hero = CustomEntityNode::CreateRoot(u8"CustomHero");
  hero->entity_tag = "勇者";
  hero->level = 99;

  auto weapon = hero->AddChild(u8"Weapon");
  weapon->entity_tag = "傳說之劍";

  auto skills = hero->AddChild(u8"Skills");
  auto s1 = skills->PushElement();
  s1->entity_tag = "火球術";

  // 2. 測試自定義節點之 TreeIO::Serialize 序列化
  std::ostringstream oss;
  TreeIO::SerializeCompact(oss, hero);
  std::string dsl = oss.str();
  std::cout << "  Custom CRTP Node DSL: " << dsl << std::endl;

  std::string str_dsl = TreeIO::SerializeToString(hero, CompactMode::Compact);
  assert(str_dsl == dsl);

  // 3. 測試 DeserializeFromString<CustomEntityNode> 精準型別推導與回傳值
  auto restored = TreeIO::DeserializeFromString<CustomEntityNode>(dsl);

  // 驗證編譯期與執行期回傳型別為精準的 std::shared_ptr<CustomEntityNode>
  static_assert(std::is_same_v<decltype(restored), std::shared_ptr<CustomEntityNode>>);
  assert(restored != nullptr);
  assert(restored->GetName() == u8"CustomHero");
  assert(restored->entity_tag == "勇者");

  // 驗證子節點型別亦為 CustomEntityNode
  auto restored_weapon = (*restored)[u8"Weapon"];
  static_assert(std::is_same_v<decltype(restored_weapon), std::shared_ptr<CustomEntityNode>>);
  assert(restored_weapon != nullptr);
  assert(restored_weapon->entity_tag == "傳說之劍");

  auto restored_skills = (*restored)[u8"Skills"];
  assert(restored_skills != nullptr);
  assert((*restored_skills)[0]->entity_tag == "火球術");

  // 4. 測試傳入自定義 Node Setter Handler
  auto restored_with_handler = TreeIO::DeserializeFromString<CustomEntityNode>(
      dsl,
      [](const std::shared_ptr<CustomEntityNode> &node, const std::string &val)
      {
        node->entity_tag = "OVERRIDE_" + val;
      }
  );
  assert(restored_with_handler->entity_tag == "OVERRIDE_勇者");
  assert((*restored_with_handler)[u8"Weapon"]->entity_tag == "OVERRIDE_傳說之劍");

  // 5. 測試完全無 GetData() / SetData() 的純領域節點，全權由 lambda 自行處理 node
  class PureCustomNode : public TreeNodeBase<PureCustomNode>
  {
  public:
    std::string my_label;
    int my_val{0};

    explicit PureCustomNode(std::u8string name = u8"") :
        TreeNodeBase<PureCustomNode>(std::move(name))
    {
    }
  };

  auto pure_hero = PureCustomNode::CreateRoot(u8"PureHero");
  pure_hero->my_label = "Warrior";
  pure_hero->my_val = 100;

  // 序列化：直接把 node 傳給 data_to_string 自行處理，完全不需要 GetData()
  std::string pure_dsl = TreeIO::SerializeToString(
      pure_hero,
      [](const PureCustomNode &n) { return n.my_label + ":" + std::to_string(n.my_val); },
      0,
      CompactMode::Compact
  );
  std::cout << "  Pure Node DSL: " << pure_dsl << std::endl;
  assert(pure_dsl == "[PureHero]=\"Warrior:100\"");

  // 反序列化：反向由 handler 自行解構並寫入 node 欄位
  auto restored_pure = TreeIO::DeserializeFromString<PureCustomNode>(
      pure_dsl,
      [](const std::shared_ptr<PureCustomNode> &node, const std::string &s)
      {
        auto colon = s.find(':');
        if (colon != std::string::npos)
        {
          node->my_label = s.substr(0, colon);
          node->my_val = std::stoi(s.substr(colon + 1));
        }
      }
  );
  assert(restored_pure != nullptr);
  assert(restored_pure->GetName() == u8"PureHero");
  assert(restored_pure->my_label == "Warrior");
  assert(restored_pure->my_val == 100);

  std::cout << " -> 通過！" << std::endl;
}

void TestTreeSharedMutex()
{
  std::cout << "[測試 2] 樹級讀寫鎖共享機制與跨樹獨立性測試..." << std::endl;

  auto root = StringTreeNode::CreateRoot(u8"TreeRoot");

  // 1. 建立子節點，驗證鎖被繼承與共享 (所有節點共享同一個 std::shared_mutex 記憶體位址)
  auto child1 = root->AddChild(u8"Child1");
  auto child2 = root->AddChild(u8"Child2");
  auto grandchild = child1->AddChild(u8"GrandChild");

  assert(&child1->GetTreeMutex() == &root->GetTreeMutex());
  assert(&child2->GetTreeMutex() == &root->GetTreeMutex());
  assert(&grandchild->GetTreeMutex() == &root->GetTreeMutex());

  // 2. 獨立樹鎖隔離測試
  auto standalone = StringTreeNode::CreateRoot(u8"Standalone");
  auto subchild = standalone->AddChild(u8"SubChild");
  assert(&standalone->GetTreeMutex() != &root->GetTreeMutex());
  assert(&standalone->GetTreeMutex() == &subchild->GetTreeMutex());

  // 3. 節點移除自立新鎖測試
  assert(root->RemoveChild(child2));
  assert(&child2->GetTreeMutex() != &root->GetTreeMutex());

  // 4. 子節點由父節點移除自立新鎖測試
  assert(child1->RemoveChild(grandchild));
  assert(&grandchild->GetTreeMutex() != &root->GetTreeMutex());

  std::cout << " -> 通過！" << std::endl;
}

void TestUnboxingAndAnonymousContainerSafety()
{
  std::cout << "[測試 11] 單元素匿名容器與精準拆箱拓撲保全測試..." << std::endl;

  // 1. 頂層匿名單元素容器：絕不可被脫殼降級為葉節點
  {
    auto single_arr = TreeIO::DeserializeFromString("{\"OnlyOneItem\"}");
    assert(single_arr != nullptr);
    assert(single_arr->ChildCount() == 1);
    assert((*single_arr)[0] != nullptr);
    assert((*single_arr)[0]->GetData() == "OnlyOneItem");
    assert(single_arr->GetName().empty());  // 匿名容器不應帶有 __ROOT__ 魔術名稱
  }

  // 2. 頂層匿名多元素容器：與單元素容器結構完全一致
  {
    auto multi_arr = TreeIO::DeserializeFromString("{\"ItemA\" \"ItemB\"}");
    assert(multi_arr != nullptr);
    assert(multi_arr->ChildCount() == 2);
    assert((*multi_arr)[0]->GetData() == "ItemA");
    assert((*multi_arr)[1]->GetData() == "ItemB");
  }

  // 3. 頂層匿名單欄位容器：外層容器絕不可被破壞
  {
    auto single_obj = TreeIO::DeserializeFromString("{ [Setting] = \"On\" }");
    assert(single_obj != nullptr);
    assert(single_obj->ChildCount() == 1);
    assert(single_obj->HasChild(u8"Setting"));
    assert((*single_obj)[u8"Setting"]->GetData() == "On");
    assert(single_obj->GetName().empty());
  }

  // 4. 頂層匿名多欄位容器
  {
    auto multi_obj = TreeIO::DeserializeFromString("{ [A] = \"1\" [B] = \"2\" }");
    assert(multi_obj != nullptr);
    assert(multi_obj->HasChild(u8"A"));
    assert(multi_obj->HasChild(u8"B"));
    assert((*multi_obj)[u8"A"]->GetData() == "1");
    assert((*multi_obj)[u8"B"]->GetData() == "2");
  }

  // 5. 頂層具名根節點：應安全拆箱，傳回以該名稱為根的實體
  {
    auto named_root = TreeIO::DeserializeFromString("[Player] = \"Hero\" { [HP] = \"100\" }");
    assert(named_root != nullptr);
    assert(named_root->GetName() == u8"Player");
    assert(named_root->GetData() == "Hero");
    assert(named_root->HasChild(u8"HP"));
    assert((*named_root)[u8"HP"]->GetData() == "100");
  }

  // 6. 頂層具名單元素容器：應安全拆箱為該具名容器
  {
    auto named_arr = TreeIO::DeserializeFromString("[Inventory]{\"Sword\"}");
    assert(named_arr != nullptr);
    assert(named_arr->GetName() == u8"Inventory");
    assert(named_arr->ChildCount() == 1);
    assert((*named_arr)[0]->GetData() == "Sword");
  }

  std::cout << " -> 通過！" << std::endl;
}

void TestDeepTreeDeserialization()
{
  std::cout << "[測試 12] 巨深階層顯式堆疊非遞迴反序列化防爆棧測試 (10,000 層)..." << std::endl;

  const int depth = 10000;
  std::string deep_dsl;
  deep_dsl.reserve(depth * 10);

  // 構造 10,000 層的深層巢狀物件 DSL
  for (int i = 0; i < depth; ++i)
  {
    deep_dsl += "[L";
    deep_dsl += std::to_string(i);
    deep_dsl += "]{";
  }
  deep_dsl += "[Leaf]=\"Success\"";
  for (int i = 0; i < depth; ++i)
  {
    deep_dsl += "}";
  }

  // 驗證反序列化使用 Heap 顯式堆疊，以 O(1) Call Stack 深度安全解析 10,000 層
  auto root = TreeIO::DeserializeFromString(deep_dsl);
  assert(root != nullptr);
  assert(root->GetName() == u8"L0");

  std::cout << " -> 反序列化完成，開始安全迭代解構..." << std::endl;
  root.reset();

  std::cout << " -> 通過！" << std::endl;
}

void TestConsecutiveEmptyNodes()
{
  std::cout << "[測試 13] 連續具名空節點與連續匿名空元素序列化/反序列化測試..." << std::endl;

  // 1. 連續具名空節點（Flag / 標籤樹）
  {
    auto root = StringTreeNode::CreateRoot(u8"Flags");
    root->AddChild(u8"EnableHDR");
    root->AddChild(u8"EnableVsync");
    root->AddChild(u8"EnableAA");

    assert(root->ChildCount() == 3);

    // 序列化
    std::string dsl = TreeIO::SerializeToString(root, CompactMode::Compact);
    std::cout << "  Flags DSL: " << dsl << std::endl;

    // 反序列化
    auto restored = TreeIO::DeserializeFromString(dsl);
    assert(restored != nullptr);
    assert(restored->GetName() == u8"Flags");
    assert(restored->ChildCount() == 3);
    assert(restored->HasChild(u8"EnableHDR"));
    assert(restored->HasChild(u8"EnableVsync"));
    assert(restored->HasChild(u8"EnableAA"));
  }

  // 2. 連續匿名空陣列元素（純空字串元素）
  {
    auto arr = StringTreeNode::CreateRoot(u8"EmptyList");
    arr->PushElement();
    arr->PushElement();
    arr->PushElement();
    assert(arr->ChildCount() == 3);

    // 序列化
    std::string dsl = TreeIO::SerializeToString(arr, CompactMode::Compact);
    std::cout << "  EmptyList DSL: " << dsl << std::endl;

    // 反序列化
    auto restored = TreeIO::DeserializeFromString(dsl);
    assert(restored != nullptr);
    assert(restored->GetName() == u8"EmptyList");
    assert(restored->ChildCount() == 3);
    assert((*restored)[0]->GetData().empty());
    assert((*restored)[1]->GetData().empty());
    assert((*restored)[2]->GetData().empty());
  }

  // 3. 空節點與帶值節點交替混排
  {
    auto mix = StringTreeNode::CreateRoot(u8"MixConfig");
    mix->AddChild(u8"FlagA");
    mix->AddChild(u8"FlagB");
    mix->AddChild(u8"ServerIP")->SetData("127.0.0.1");
    mix->AddChild(u8"FlagC");
    mix->AddChild(u8"Port")->SetData("8080");

    assert(mix->ChildCount() == 5);

    std::string dsl = TreeIO::SerializeToString(mix, CompactMode::Compact);
    auto restored = TreeIO::DeserializeFromString(dsl);
    assert(restored != nullptr);
    assert(restored->ChildCount() == 5);
    assert(restored->HasChild(u8"FlagA"));
    assert(restored->HasChild(u8"FlagB"));
    assert(restored->HasChild(u8"ServerIP"));
    assert((*restored)[u8"ServerIP"]->GetData() == "127.0.0.1");
    assert(restored->HasChild(u8"FlagC"));
    assert(restored->HasChild(u8"Port"));
    assert((*restored)[u8"Port"]->GetData() == "8080");
  }

  // 4. 手寫無等號連續標籤 DSL：{[Tag1][Tag2][Tag3]}
  {
    auto restored = TreeIO::DeserializeFromString("{[Tag1][Tag2][Tag3]}");
    assert(restored != nullptr);
    assert(restored->ChildCount() == 3);
    assert(restored->HasChild(u8"Tag1"));
    assert(restored->HasChild(u8"Tag2"));
    assert(restored->HasChild(u8"Tag3"));
  }

  // 5. 驗證 Compact 模式下的連續空標籤與帶值節點混合（關鍵字等號必然保留）
  {
    auto hero = StringTreeNode::CreateRoot(u8"Hero");
    hero->AddChild(u8"Passive1");
    hero->AddChild(u8"Passive2");
    hero->AddChild(u8"Skill")->SetData("Fireball");
    hero->AddChild(u8"Passive3");

    std::string dsl_compact = TreeIO::SerializeToString(hero, CompactMode::Compact);
    std::cout << "  Compact Hero DSL: " << dsl_compact << std::endl;
    assert(dsl_compact == "[Hero]{[Passive1][Passive2][Skill]=\"Fireball\"[Passive3]}");

    auto restored = TreeIO::DeserializeFromString(dsl_compact);
    assert(restored != nullptr);
    assert(restored->GetName() == u8"Hero");
    assert(restored->ChildCount() == 4);
    assert(restored->HasChild(u8"Passive1"));
    assert(restored->HasChild(u8"Passive2"));
    assert(restored->HasChild(u8"Skill"));
    assert((*restored)[u8"Skill"]->GetData() == "Fireball");
    assert(restored->HasChild(u8"Passive3"));
  }

  std::cout << " -> 通過！" << std::endl;
}

void TestArrayOfObjectsSerialization()
{
  std::cout << "[測試 14] 陣列內包含多個匿名子物件的序列化與反序列化測試..." << std::endl;

  // 1. 程式碼建構陣列包含多個匿名子物件（由父節點原地延伸）
  auto arr = StringTreeNode::CreateRoot(u8"");
  auto obj1 = arr->PushElement();
  obj1->AddChild(u8"item1")->SetData("A");

  auto obj2 = arr->PushElement();
  obj2->AddChild(u8"item2")->SetData("B");

  // 序列化
  std::string dsl = TreeIO::SerializeToString(arr);
  std::cout << "  Array of Objects DSL:\n" << dsl << std::endl;

  // 反序列化
  auto restored = TreeIO::DeserializeFromString(dsl);
  assert(restored != nullptr);
  assert(restored->ChildCount() == 2);

  auto r_obj1 = (*restored)[0];
  assert(r_obj1 != nullptr);
  assert(r_obj1->HasChild(u8"item1"));
  assert((*r_obj1)[u8"item1"]->GetData() == "A");

  auto r_obj2 = (*restored)[1];
  assert(r_obj2 != nullptr);
  assert(r_obj2->HasChild(u8"item2"));
  assert((*r_obj2)[u8"item2"]->GetData() == "B");

  // 2. 直接以標準 DSL 文字反序列化驗證（包含匿名子容器標頭 []）
  std::string user_dsl = R"(
  []
  {
    []
    {
      [item1] = "A"
    }
    []
    {
      [item2] = "B"
    }
  }
  )";

  auto user_restored = TreeIO::DeserializeFromString(user_dsl);
  assert(user_restored != nullptr);
  assert(user_restored->ChildCount() == 2);

  auto u_obj1 = (*user_restored)[0];
  assert(u_obj1 != nullptr);
  assert(u_obj1->HasChild(u8"item1"));
  assert((*u_obj1)[u8"item1"]->GetData() == "A");

  auto u_obj2 = (*user_restored)[1];
  assert(u_obj2 != nullptr);
  assert(u_obj2->HasChild(u8"item2"));
  assert((*u_obj2)[u8"item2"]->GetData() == "B");

  std::cout << " -> 通過！" << std::endl;
}

void TestTagAndAnonymousChildSequence()
{
  std::cout << "[測試 15] 純標籤與匿名子節點交替序列語法測試..." << std::endl;

  // 測試核心問題：「有名字無資料，下一個順位是沒有名字」
  // 在沒有等號的情況下，[IsAdmin] 作為純標籤結束，後面的 "草藥" 作為獨立匿名節點！
  std::string dsl = R"(
  [Player] = "Hero"
  {
    [IsAdmin]
    "草藥"
    "黃金盔甲"
    [Role] = "Warrior"
  }
  )";

  auto player = TreeIO::DeserializeFromString(dsl);
  assert(player != nullptr);
  assert(player->GetName() == u8"Player");
  assert(player->GetData() == "Hero");
  assert(player->ChildCount() == 4);

  // 子節點 0: [IsAdmin]（純標籤，無資料）
  assert((*player)[0]->GetName() == u8"IsAdmin");
  assert((*player)[0]->GetData().empty());

  // 子節點 1: "草藥"（匿名節點）
  assert((*player)[1]->GetName().empty());
  assert((*player)[1]->GetData() == "草藥");

  // 子節點 2: "黃金盔甲"（匿名節點）
  assert((*player)[2]->GetName().empty());
  assert((*player)[2]->GetData() == "黃金盔甲");

  // 子節點 3: [Role] = "Warrior"
  assert((*player)[3]->GetName() == u8"Role");
  assert((*player)[3]->GetData() == "Warrior");

  // 測試 Compact 模式下的無空格序列
  std::string compact_dsl = TreeIO::SerializeToString(player, CompactMode::Compact);
  std::cout << "  Tag + Anon Compact DSL: " << compact_dsl << std::endl;
  assert(compact_dsl == "[Player]=\"Hero\"{[IsAdmin]\"草藥\"\"黃金盔甲\"[Role]=\"Warrior\"}");

  // 反序列化 Compact 驗證
  auto restored = TreeIO::DeserializeFromString(compact_dsl);
  assert(restored != nullptr);
  assert(restored->ChildCount() == 4);
  assert((*restored)[0]->GetName() == u8"IsAdmin");
  assert((*restored)[1]->GetData() == "草藥");
  assert((*restored)[2]->GetData() == "黃金盔甲");
  assert((*restored)[3]->GetName() == u8"Role");
  assert((*restored)[3]->GetData() == "Warrior");

  std::cout << " -> 通過！" << std::endl;
}

// =============================================================================
// [測試 16] 多型衍生階層模板與 C++20 std::derived_from 強型別支援測試
// =============================================================================
class BaseEntity : public TreeNodeBase<BaseEntity>
{
public:
  using Base = TreeNodeBase<BaseEntity>;
  virtual ~BaseEntity() = default;
  virtual std::string GetEntityType() const
  {
    return "BaseEntity";
  }

protected:
  explicit BaseEntity(std::u8string name = u8"") :
      Base(std::move(name))
  {
  }

  template <typename D>
  friend class TreeNodeBase;
};

class MonsterEntity : public BaseEntity
{
public:
  int hp = 0;
  int atk = 0;

  MonsterEntity(std::u8string name, int in_hp, int in_atk) :
      BaseEntity(std::move(name)), hp(in_hp), atk(in_atk)
  {
  }

  std::string GetEntityType() const override
  {
    return "Monster";
  }
};

class ItemEntity : public BaseEntity
{
public:
  int price = 0;

  // 測試不需要 name 的特殊建構子
  explicit ItemEntity(int in_price) :
      BaseEntity(u8""), price(in_price)
  {
  }

  std::string GetEntityType() const override
  {
    return "Item";
  }
};

class NotAnEntity
{
};

void TestPolymorphicDerivedNodeTemplate()
{
  std::cout << "[測試 16] 多型衍生階層模板與 C++20 std::derived_from 強型別支援測試..." << std::endl;

  // 1. 編譯期 Concept 約束防禦驗證
  static_assert(std::derived_from<MonsterEntity, BaseEntity>);
  static_assert(std::derived_from<ItemEntity, BaseEntity>);
  static_assert(!std::derived_from<NotAnEntity, BaseEntity>);

  // 2. 透過 CreateRoot<MonsterEntity> 建立強型別根節點與轉發參數
  std::shared_ptr<MonsterEntity> boss = BaseEntity::CreateRoot<MonsterEntity>(u8"BossDragon", 5000, 350);
  assert(boss != nullptr);
  assert(boss->GetName() == u8"BossDragon");
  assert(boss->hp == 5000);
  assert(boss->atk == 350);
  assert(boss->GetEntityType() == "Monster");

  // 3. 透過 AddChild<MonsterEntity> 新增具名衍生節點（零手動轉型直出）
  std::shared_ptr<MonsterEntity> minion = boss->AddChild<MonsterEntity>(u8"Goblin", 100, 15);
  assert(minion != nullptr);
  assert(minion->GetName() == u8"Goblin");
  assert(minion->hp == 100);
  assert(minion->atk == 15);
  assert(minion->GetEntityType() == "Monster");
  assert(boss->ChildCount() == 1);

  // 4. 透過 PushElement<ItemEntity> 原地構造自訂建構子衍生節點
  std::shared_ptr<ItemEntity> potion = boss->PushElement<ItemEntity>(50);
  assert(potion != nullptr);
  assert(potion->price == 50);
  assert(potion->GetEntityType() == "Item");
  assert(boss->ChildCount() == 2);

  // 5. 透過 InsertBefore<MonsterEntity> 在 minion 前方插入精英怪
  std::shared_ptr<MonsterEntity> elite = boss->InsertBefore<MonsterEntity>(minion, u8"OrcWarrior", 500, 60);
  assert(elite != nullptr);
  assert(elite->GetName() == u8"OrcWarrior");
  assert(elite->hp == 500);
  assert(boss->ChildCount() == 3);
  assert((*boss)[0] == elite);
  assert((*boss)[1] == minion);
  assert((*boss)[2] == potion);

  // 6. 透過 InsertAfter<ItemEntity> 在 potion 後方插入稀有道具
  std::shared_ptr<ItemEntity> sword = boss->InsertAfter<ItemEntity>(potion, u8"Excalibur", 9999);
  assert(sword != nullptr);
  assert(sword->GetName() == u8"Excalibur");
  assert(sword->price == 9999);
  assert(boss->ChildCount() == 4);
  assert((*boss)[3] == sword);

  // 7. 向下相容預設型別呼叫（未指定模板參數時預設為 D = BaseEntity）
  std::shared_ptr<BaseEntity> default_node = boss->AddChild(u8"NeutralBeacon");
  assert(default_node != nullptr);
  assert(default_node->GetName() == u8"NeutralBeacon");
  assert(default_node->GetEntityType() == "BaseEntity");

  std::shared_ptr<BaseEntity> anon_default = boss->PushElement();
  assert(anon_default != nullptr);
  assert(anon_default->GetName().empty());

  // 8. 透過 AddChild 原地延伸構造衍生節點
  auto monster = boss->AddChild<MonsterEntity>(u8"WanderingGhost", 80, 25);
  assert(boss->FindChildByName(u8"WanderingGhost") == monster);
  assert(boss->FindChildByName(u8"WanderingGhost")->GetEntityType() == "Monster");

  std::cout << " -> 通過！" << std::endl;
}

void TestAttachChildAndFactoryDeserialization()
{
  std::cout << "[測試 17] TreeIO 特權掛載、工廠模式反序列化與資料毀損判定...";

  // 1. 測試 TreeIO 工廠模式反序列化（不預先造 node，送 string 與 name 給工廠）
  std::string dsl = R"(
    [Player] = "Hero" {
      [HP] = "100"
      [Weapon] = "Sword"
    }
  )";

  auto custom_factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<TreeNode<std::string>>
  {
    auto node = TreeNode<std::string>::CreateRoot(name);
    if (node)
    {
      node->SetData("FABRICATED_" + data);
    }
    return node;
  };

  auto restored = TreeIO::DeserializeFromString<TreeNode<std::string>>(dsl, custom_factory);
  assert(restored != nullptr);
  assert(restored->GetName() == u8"Player");
  assert(restored->GetData() == "FABRICATED_Hero");
  assert((*restored)[u8"HP"]->GetData() == "FABRICATED_100");
  assert((*restored)[u8"Weapon"]->GetData() == "FABRICATED_Sword");

  // 3. 測試工廠製造失敗（回傳 nullptr）視為資料毀損，整棵樹中止並回傳 nullptr
  std::string corrupt_dsl = R"(
    [Player] = "Hero" {
      [HP] = "CORRUPTED_VALUE"
      [Weapon] = "Sword"
    }
  )";

  auto strict_factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<TreeNode<std::string>>
  {
    if (data == "CORRUPTED_VALUE")
    {
      return nullptr;  // 工廠判定資料損壞，無法造出物件！
    }
    auto node = TreeNode<std::string>::CreateRoot(name);
    if (node)
    {
      node->SetData(data);
    }
    return node;
  };

  auto corrupt_result = TreeIO::DeserializeFromString<TreeNode<std::string>>(corrupt_dsl, strict_factory);
  assert(corrupt_result == nullptr);  // 必須完全中止並回傳 nullptr！

  // 3. 測試工廠回傳的物件發現與同層具名名稱重複，視為資料毀損中止並回傳 nullptr
  std::string duplicate_dsl = R"(
    [Player] = "Hero" {
      [Item] = "Sword"
      [Item] = "Shield"
    }
  )";

  auto normal_factory = [](const std::u8string &name, const std::string &data) -> std::shared_ptr<TreeNode<std::string>>
  {
    auto node = TreeNode<std::string>::CreateRoot(name);
    if (node)
    {
      node->SetData(data);
    }
    return node;
  };

  auto duplicate_result = TreeIO::DeserializeFromString<TreeNode<std::string>>(duplicate_dsl, normal_factory);
  assert(duplicate_result == nullptr);  // 同層名稱重複，掛載失敗，視為資料毀損回傳 nullptr！

  std::cout << " -> 通過！" << std::endl;
}

void TestTreeCleanupTracker()
{
  std::cout << "[測試 18] TreeCleanupTracker 弱引用共享鎖全節點清除驗證測試..." << std::endl;

  TreeCleanupTracker root_tracker;
  TreeCleanupTracker detached_tracker;

  {
    auto root = StringTreeNode::CreateRoot(u8"RootNode");
    root_tracker = root->GetCleanupTracker();

    assert(root_tracker.IsAlive());
    assert(!root_tracker.AreAllNodesCleanedUp());
    assert(!root_tracker.IsCleanedUp());

    auto child1 = root->AddChild(u8"Child1");
    auto child2 = root->AddChild(u8"Child2");
    auto grandChild = child1->AddChild(u8"GrandChild");

    // 建立 Detached 子節點並測試獨立生命週期
    bool detached_ok = child2->DetachFromParent();
    assert(detached_ok);
    assert(root->ChildCount() == 1);
    assert(!root->HasChild(u8"Child2"));
    assert(root->FindChildByName(u8"Child2") == nullptr);
    assert(child2->GetParent() == nullptr);

    detached_tracker = child2->GetCleanupTracker();
    assert(detached_tracker.IsAlive());
    assert(!detached_tracker.AreAllNodesCleanedUp());

    // 依然存活
    assert(root_tracker.IsAlive());
  }

  // 離開區塊後，root, child1, grandChild 均已完全解構釋放
  assert(root_tracker.AreAllNodesCleanedUp());
  assert(root_tracker.IsCleanedUp());
  assert(!root_tracker.IsAlive());

  // detached 節點 child2 也在區塊結束時釋放
  assert(detached_tracker.AreAllNodesCleanedUp());
  assert(detached_tracker.IsCleanedUp());
  assert(!detached_tracker.IsAlive());

  std::cout << " -> 通過！" << std::endl;
}

// ---------------------------------------------------------------------------
// 測試 19：同層子節點搬移與重排測試 (Sibling Reordering)
// ---------------------------------------------------------------------------
void TestSiblingReordering()
{
  std::cout << "[測試 19] 同層子節點順序搬移與重排測試 (MoveChildBefore/After/ToIndex)..." << std::flush;

  auto root = StringTreeNode::CreateRoot(u8"Root");
  auto a = root->AddChild(u8"A");
  auto b = root->AddChild(u8"B");
  auto c = root->AddChild(u8"C");
  auto d = root->AddChild(u8"D");

  // 初始順序: [A, B, C, D]
  assert(root->GetElementAt(0) == a);
  assert(root->GetElementAt(1) == b);
  assert(root->GetElementAt(2) == c);
  assert(root->GetElementAt(3) == d);

  // 1. MoveChildBefore: 將 A 移到 C 之前 -> [B, A, C, D]
  bool ok = root->MoveChildBefore(a, c);
  assert(ok);
  assert(root->GetElementAt(0) == b);
  assert(root->GetElementAt(1) == a);
  assert(root->GetElementAt(2) == c);
  assert(root->GetElementAt(3) == d);

  // 2. MoveChildBefore: 將 D 移到 B 之前 -> [D, B, A, C]
  ok = root->MoveChildBefore(d, b);
  assert(ok);
  assert(root->GetElementAt(0) == d);
  assert(root->GetElementAt(1) == b);
  assert(root->GetElementAt(2) == a);
  assert(root->GetElementAt(3) == c);

  // 3. MoveChildAfter: 將 D 移到 A 之後 -> [B, A, D, C]
  ok = root->MoveChildAfter(d, a);
  assert(ok);
  assert(root->GetElementAt(0) == b);
  assert(root->GetElementAt(1) == a);
  assert(root->GetElementAt(2) == d);
  assert(root->GetElementAt(3) == c);

  // 4. MoveChildToIndex: 將 C 移至 index 0 -> [C, B, A, D]
  ok = root->MoveChildToIndex(c, 0);
  assert(ok);
  assert(root->GetElementAt(0) == c);
  assert(root->GetElementAt(1) == b);
  assert(root->GetElementAt(2) == a);
  assert(root->GetElementAt(3) == d);

  // 5. MoveChildToIndex 超出邊界自動 clamp: 將 C 移至 index 999 -> [B, A, D, C]
  ok = root->MoveChildToIndex(c, 999);
  assert(ok);
  assert(root->GetElementAt(0) == b);
  assert(root->GetElementAt(1) == a);
  assert(root->GetElementAt(2) == d);
  assert(root->GetElementAt(3) == c);

  // 6. 子節點自身的捷徑糖衣: b->MoveAfter(c) -> [A, D, C, B]
  ok = b->MoveAfter(c);
  assert(ok);
  assert(root->GetElementAt(0) == a);
  assert(root->GetElementAt(1) == d);
  assert(root->GetElementAt(2) == c);
  assert(root->GetElementAt(3) == b);

  // a->MoveToIndex(2) -> [D, C, A, B]
  ok = a->MoveToIndex(2);
  assert(ok);
  assert(root->GetElementAt(0) == d);
  assert(root->GetElementAt(1) == c);
  assert(root->GetElementAt(2) == a);
  assert(root->GetElementAt(3) == b);

  // b->MoveBefore(d) -> [B, D, C, A]
  ok = b->MoveBefore(d);
  assert(ok);
  assert(root->GetElementAt(0) == b);
  assert(root->GetElementAt(1) == d);
  assert(root->GetElementAt(2) == c);
  assert(root->GetElementAt(3) == a);

  // 7. 驗證名稱字典長存且精準尋址不受順序搬移影響
  assert(root->FindChildByName(u8"A") == a);
  assert(root->FindChildByName(u8"B") == b);
  assert(root->FindChildByName(u8"C") == c);
  assert(root->FindChildByName(u8"D") == d);

  // 8. 邊界與錯誤檢查: 自身移給自身、nullptr、跨親代節點
  assert(root->MoveChildBefore(a, a) == true);   // 無變化直接成功
  assert(root->MoveChildAfter(a, a) == true);
  assert(root->MoveChildBefore(a, nullptr) == false);
  assert(root->MoveChildBefore(nullptr, a) == false);

  auto anotherRoot = StringTreeNode::CreateRoot(u8"Other");
  auto otherChild = anotherRoot->AddChild(u8"X");
  assert(root->MoveChildBefore(a, otherChild) == false); // 不同父節點
  assert(root->MoveChildAfter(otherChild, a) == false);

  std::cout << " -> 通過！" << std::endl;
}

void TestExplicitAnonymousContainerDisambiguation()
{
  std::cout << "[測試 20] 顯式 [] 匿名容器標頭與帶資料匿名容器消歧義測試..." << std::endl;

  // 1. 測試使用者指定之 Allman 風格匿名容器結構（字串 + 獨立平級容器）
  std::string dsl1 = R"(
[]
{
  "ItemData"
  []
  {
    "Sub1"
    "Sub2"
  }
}
)";

  auto root1 = TreeIO::DeserializeFromString(dsl1);
  assert(root1 != nullptr);
  assert(root1->GetName().empty());
  assert(root1->ChildCount() == 2);  // 必須為 2 個平級兄弟，絕不誤吞成父子！

  // 檢查元素 0: 純字串葉節點
  auto elem0 = (*root1)[0];
  assert(elem0 != nullptr);
  assert(elem0->GetName().empty());
  assert(elem0->GetData() == "ItemData");
  assert(elem0->ChildCount() == 0);

  // 檢查元素 1: 獨立匿名子容器
  auto elem1 = (*root1)[1];
  assert(elem1 != nullptr);
  assert(elem1->GetName().empty());
  assert(elem1->GetData().empty());
  assert(elem1->ChildCount() == 2);
  assert((*elem1)[0]->GetData() == "Sub1");
  assert((*elem1)[1]->GetData() == "Sub2");

  // 序列化回 DSL，驗證格式化 Allman 輸出結構
  std::string exported_dsl1 = TreeIO::SerializeToString(root1);
  std::cout << "  Exported DSL1:\n" << exported_dsl1 << std::endl;
  assert(exported_dsl1.find("[]") != std::string::npos);

  // 2. 測試帶資料之匿名容器：[] = "ContainerPayload" { ... }
  std::string dsl2 = R"(
[]
{
  "ItemData"
  [] = "ContainerPayload"
  {
    "Sub1"
    "Sub2"
  }
}
)";

  auto root2 = TreeIO::DeserializeFromString(dsl2);
  assert(root2 != nullptr);
  assert(root2->ChildCount() == 2);

  auto with_data_container = (*root2)[1];
  assert(with_data_container != nullptr);
  assert(with_data_container->GetName().empty());
  assert(with_data_container->GetData() == "ContainerPayload");  // 成功持有資料！
  assert(with_data_container->ChildCount() == 2);                 // 成功持有子節點！
  assert((*with_data_container)[0]->GetData() == "Sub1");
  assert((*with_data_container)[1]->GetData() == "Sub2");

  // 序列化回 DSL，驗證帶資料標頭格式
  std::string exported_dsl2 = TreeIO::SerializeToString(root2);
  std::cout << "  Exported DSL2:\n" << exported_dsl2 << std::endl;
  assert(exported_dsl2.find("[] = \"ContainerPayload\"") != std::string::npos);

  // 3. 測試 Compact 模式下的序列化與反序列化
  std::string compact_dsl2 = TreeIO::SerializeToString(root2, CompactMode::Compact);
  std::cout << "  Compact DSL2: " << compact_dsl2 << std::endl;
  assert(compact_dsl2 == "[]{\"ItemData\"[]=\"ContainerPayload\"{\"Sub1\"\"Sub2\"}}");

  auto restored_compact2 = TreeIO::DeserializeFromString(compact_dsl2);
  assert(restored_compact2 != nullptr);
  assert(restored_compact2->ChildCount() == 2);
  assert((*restored_compact2)[1]->GetData() == "ContainerPayload");
  assert((*restored_compact2)[1]->ChildCount() == 2);

  // 4. 測試無標頭裸大括號前後符號視為雜訊，內部葉節點平級保留
  std::string noise_dsl = R"(
[]
{
  "ItemData"
  {
    "Sub1"
    "Sub2"
  }
  "ItemB"
}
)";
  auto noise_root = TreeIO::DeserializeFromString(noise_dsl);
  assert(noise_root != nullptr);
  assert(noise_root->ChildCount() == 4);
  assert((*noise_root)[0]->GetData() == "ItemData");
  assert((*noise_root)[1]->GetData() == "Sub1");
  assert((*noise_root)[2]->GetData() == "Sub2");
  assert((*noise_root)[3]->GetData() == "ItemB");

  std::cout << " -> 通過！" << std::endl;
}

void TestUtf8NameValidation()
{
  std::cout << "[測試 21] 節點名稱 UTF-8 合法性檢驗與 Fail-Fast 防禦測試..." << std::endl;

  // 1. 單元驗證 ork::utf8::is_valid 算法
  assert(ork::utf8::is_valid(""));
  assert(ork::utf8::is_valid("Hello_World-123"));
  assert(ork::utf8::is_valid("繁體中文節點名稱"));
  assert(ork::utf8::is_valid(u8"測試C++20字面量"));
  assert(ork::utf8::is_valid("Emoji🚀🔥"));

  // 非法前導位元組
  assert(!ork::utf8::is_valid(std::string("\xFF")));
  assert(!ork::utf8::is_valid(std::string("\xFE")));
  assert(!ork::utf8::is_valid(std::string("\x80")));  // 孤立續接字節

  // 超長編碼 (Overlong)
  assert(!ork::utf8::is_valid(std::string("\xC0\x80", 2)));
  assert(!ork::utf8::is_valid(std::string("\xC1\x80", 2)));
  assert(!ork::utf8::is_valid(std::string("\xE0\x80\x80", 3)));
  assert(!ork::utf8::is_valid(std::string("\xF0\x80\x80\x80", 4)));

  // UTF-16 代理字元 (Surrogates: U+D800..U+DFFF)
  assert(!ork::utf8::is_valid(std::string("\xED\xA0\x80", 3)));  // U+D800
  assert(!ork::utf8::is_valid(std::string("\xED\xBF\xBF", 3)));  // U+DFFF

  // 超出 Unicode 上限 (> U+10FFFF)
  assert(!ork::utf8::is_valid(std::string("\xF4\x90\x80\x80", 4)));

  // 截斷的多位元組序列
  assert(!ork::utf8::is_valid(std::string("\xC2", 1)));
  assert(!ork::utf8::is_valid(std::string("\xE4\xB8", 2)));
  assert(!ork::utf8::is_valid(std::string("\xF0\x9F\x9A", 3)));

  // 2. TreeIO 反序列化合法 UTF-8 名稱測試
  std::string valid_dsl = R"(
[玩家角色] = "勇者"
{
  [裝備欄]
  {
    [傳奇雙手劍] = "攻擊力+500"
  }
}
)";
  auto valid_root = TreeIO::DeserializeFromString(valid_dsl);
  assert(valid_root != nullptr);
  assert(valid_root->GetName() == u8"玩家角色");
  assert(valid_root->ChildCount() == 1);
  auto equip_slot = (*valid_root)[0];
  assert(equip_slot->GetName() == u8"裝備欄");
  assert(equip_slot->ChildCount() == 1);
  assert((*equip_slot)[0]->GetName() == u8"傳奇雙手劍");
  assert((*equip_slot)[0]->GetData() == "攻擊力+500");

  // 3. TreeIO 反序列化非法 UTF-8 名稱 -> 觸發 Fail-Fast 回傳 nullptr
  std::string bad_dsl1 = "[";
  bad_dsl1.append("\xFF\xFE");  // 非法前導位元組
  bad_dsl1.append("] = \"Data\"");
  auto bad_root1 = TreeIO::DeserializeFromString(bad_dsl1);
  assert(bad_root1 == nullptr);  // 必須被安全攔截並傳回 nullptr！

  std::string bad_dsl2 = "[";
  bad_dsl2.append("\xED\xA0\x80");  // UTF-16 代理字元
  bad_dsl2.append("] = \"Data\"");
  auto bad_root2 = TreeIO::DeserializeFromString(bad_dsl2);
  assert(bad_root2 == nullptr);

  std::string bad_dsl3 = "[Prefix_";
  bad_dsl3.append("\xE4\xB8");  // 截斷的 3-byte 中文字元
  bad_dsl3.append("] { \"Child\" }");
  auto bad_root3 = TreeIO::DeserializeFromString(bad_dsl3);
  assert(bad_root3 == nullptr);

  std::cout << " -> 通過！" << std::endl;
}

int main()
{
  std::cout << "========================================" << std::endl;
  std::cout << "  OuroKore Tree & TreeIO 單元測試開始  " << std::endl;
  std::cout << "========================================" << std::endl;

  TestBasicTreeOperations();
  TestTreeSharedMutex();
  TestConcurrencySafety();
  TestArrayOperations();
  TestTreeIOSerialization();
  TestFaultTolerantFSM();
  TestBinaryAndEscapeHandling();
  TestDeepTreeDestruction();
  TestUnifiedDualMode();
  TestCustomCRTPNode();
  TestUnboxingAndAnonymousContainerSafety();
  TestDeepTreeDeserialization();
  TestConsecutiveEmptyNodes();
  TestArrayOfObjectsSerialization();
  TestTagAndAnonymousChildSequence();
  TestPolymorphicDerivedNodeTemplate();
  TestAttachChildAndFactoryDeserialization();
  TestTreeCleanupTracker();
  TestSiblingReordering();
  TestExplicitAnonymousContainerDisambiguation();
  TestUtf8NameValidation();

  std::cout << "========================================" << std::endl;
  std::cout << "  全數 21 項單元測試 100% 成功通過！   " << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
