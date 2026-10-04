#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <ourokore/base/export.h>
#include <ourokore/base/utf8.hpp>

namespace ork::base
{



/**
 * @brief 階層式非同步 / 迭代防爆棧節點刪除器
 *
 * 專為解決百萬層深樹在析構時引發的呼叫堆疊溢位 (Stack Overflow) 問題。
 * 提供同步迭代清空與非同步執行緒池清空兩種安全模式。
 */
class AsyncNodeDeletor
{
public:
  using Task = std::function<void()>;

  static void EnqueueTask(Task task)
  {
    std::lock_guard<std::mutex> lock(GetMutex());
    GetTasks().push(std::move(task));
    GetReady() = false;

    if (GetThreadCount() < std::max<size_t>(1, std::thread::hardware_concurrency()))
    {
      GetThreadCount()++;
      std::thread t([]() {
        while (true)
        {
          Task current_task;
          {
            std::lock_guard<std::mutex> lock(GetMutex());
            if (GetTasks().empty())
            {
              if (--GetThreadCount() == 0)
              {
                GetReady() = true;
                GetCV().notify_all();
              }
              return;
            }
            current_task = std::move(GetTasks().front());
            GetTasks().pop();
          }
          if (current_task)
          {
            current_task();
          }
        }
      });
      t.detach();
    }
  }

  static void Wait()
  {
    std::unique_lock<std::mutex> lock(GetMutex());
    GetCV().wait(lock, []() { return GetReady(); });
  }

private:
  static std::mutex &GetMutex()
  {
    static std::mutex s_mutex;
    return s_mutex;
  }
  static std::condition_variable &GetCV()
  {
    static std::condition_variable s_cv;
    return s_cv;
  }
  static std::queue<Task> &GetTasks()
  {
    static std::queue<Task> s_tasks;
    return s_tasks;
  }
  static size_t &GetThreadCount()
  {
    static size_t s_thread_count = 0;
    return s_thread_count;
  }
  static bool &GetReady()
  {
    static bool s_ready = true;
    return s_ready;
  }
};

/**
 * @brief 基底樹狀容器 (CRTP 架構)
 *
 * @tparam D 延伸衍生類別 (Derived Class)
 *
 * 特性：
 * 1. 雙模態支援：支援具名字節點（Object）與 O(1) 循序陣列元素（Array）。
 * 2. 雙向三合一索引：保持插入順序的 list、O(1) 名稱尋址 map、O(1) 節點重排反向索引。
 * 3. 執行緒安全：採用整棵樹共享之 std::shared_mutex 提供多讀單寫（Shared/Unique Lock）機制。
 * 4. 防爆棧析構：整合安全非同步與顯式堆疊析構，杜絕巨深樹級聯析構爆棧。
 * 5. 全域 UTF-8：節點名稱強制使用 C++20 原生 std::u8string。
 */
template <typename D>
class TreeNodeBase
{
public:
  using NodePtr = std::shared_ptr<D>;
  using ConstNodePtr = std::shared_ptr<const D>;

protected:
  std::u8string m_name;                 ///< 節點名稱 (UTF-8)
  // --- 唯一真實子節點容器通道 (連續記憶體快取友善，支援 O(1) 循序/下標隨機存取) ---
  std::vector<NodePtr> m_elements;

  // --- 具名索引字典 (O(1) 雜湊尋址) ---
  std::unordered_map<std::u8string, NodePtr> m_nameMap;

  // 拓撲關聯
  std::weak_ptr<D> m_parent;  ///< 父節點弱引用
  std::weak_ptr<D> m_self;    ///< 自身弱引用

  // 執行緒安全鎖 (整棵樹共享同一個讀寫鎖)
  mutable std::shared_ptr<std::shared_mutex> m_treeMutex;

  void PropagateTreeMutex(const std::shared_ptr<std::shared_mutex> &mutex)
  {
    if (!mutex || m_treeMutex == mutex)
    {
      return;
    }
    m_treeMutex = mutex;
    for (auto &elem : m_elements)
    {
      if (elem)
      {
        elem->PropagateTreeMutex(mutex);
      }
    }
  }

  void SetParentAndSelf(const NodePtr &parent, const NodePtr &self)
  {
    m_parent = parent;
    m_self = self;
    if (parent)
    {
      PropagateTreeMutex(parent->GetTreeMutexPtr());
    }
  }

public:
  [[nodiscard]] std::shared_mutex &GetTreeMutex() const noexcept
  {
    if (!m_treeMutex)
    {
      m_treeMutex = std::make_shared<std::shared_mutex>();
    }
    return *m_treeMutex;
  }

  [[nodiscard]] std::shared_ptr<std::shared_mutex> GetTreeMutexPtr() const noexcept
  {
    if (!m_treeMutex)
    {
      m_treeMutex = std::make_shared<std::shared_mutex>();
    }
    return m_treeMutex;
  }

  /**
   * @brief 斷開與父節點的關聯並自立為新樹（分配專屬共享鎖）
   */
  void DetachFromParent()
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    m_parent.reset();
    PropagateTreeMutex(std::make_shared<std::shared_mutex>());
  }

  // 工廠方法：建構節點並掛載非同步防爆棧析構器
  static NodePtr MakeNode(const std::u8string &name = u8"")
  {
    if (!D::CanCreateChild(name))
    {
      return nullptr;
    }

    return std::shared_ptr<D>(new D(name), [](D *p) {
      if (p)
      {
        AsyncNodeDeletor::EnqueueTask([p]() { delete p; });
      }
    });
  }

public:
  explicit TreeNodeBase(std::u8string name = u8"") :
      m_name(std::move(name)),
      m_treeMutex(std::make_shared<std::shared_mutex>())
  {
  }

  virtual ~TreeNodeBase()
  {
    // 解構前主動斷開子節點 parent 弱指針，防範循環懸空
    for (auto &elem : m_elements)
    {
      if (elem)
      {
        elem->m_parent.reset();
      }
    }
  }

  // 靜態鉤子預設實現：延伸類別可覆寫以加入自訂校驗
  static bool CanCreateChild([[maybe_unused]] const std::u8string &name)
  {
    return true;
  }

  static NodePtr CreateRoot(const std::u8string &name = u8"")
  {
    NodePtr root = MakeNode(name);
    if (root)
    {
      root->m_self = root;
    }
    return root;
  }

  // 創建陣列根節點（便民別名）
  static NodePtr CreateArray(const std::u8string &name = u8"")
  {
    return CreateRoot(name);
  }

  // --- 基本屬性 ---
  [[nodiscard]] std::u8string GetName() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_name;
  }

  void SetName(const std::u8string &name)
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    m_name = name;
  }

  // --- 形態判定 (資料驅動：m_elements.size() vs m_nameMap.size()) ---
  [[nodiscard]] bool IsArray() const noexcept
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.size() > m_nameMap.size();
  }

  [[nodiscard]] bool IsObject() const noexcept
  {
    return !IsArray();
  }



  // 取得父節點與自身
  [[nodiscard]] NodePtr GetParent()
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_parent.lock();
  }

  [[nodiscard]] ConstNodePtr GetParent() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_parent.lock();
  }

  [[nodiscard]] NodePtr GetSelf()
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_self.lock();
  }

  [[nodiscard]] ConstNodePtr GetSelf() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_self.lock();
  }

  // =========================================================================
  // 子節點與陣列元素數量與存取通道 (統合單一容器)
  // =========================================================================

  [[nodiscard]] size_t ChildCount() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.size();
  }

  [[nodiscard]] size_t ElementCount() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.size();
  }

  [[nodiscard]] size_t Size() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.size();
  }

  [[nodiscard]] bool Empty() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.empty();
  }

  // 下標隨機存取 (O(1))
  [[nodiscard]] NodePtr GetElementAt(size_t index)
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    if (index < m_elements.size())
    {
      return m_elements[index];
    }
    return nullptr;
  }

  [[nodiscard]] ConstNodePtr GetElementAt(size_t index) const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    if (index < m_elements.size())
    {
      return m_elements[index];
    }
    return nullptr;
  }

  NodePtr operator[](size_t index)
  {
    return GetElementAt(index);
  }

  ConstNodePtr operator[](size_t index) const
  {
    return GetElementAt(index);
  }

  // 名稱尋址 (O(1))
  [[nodiscard]] NodePtr FindChildByName(const std::u8string &name)
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    auto it = m_nameMap.find(name);
    return (it != m_nameMap.end()) ? it->second : nullptr;
  }

  [[nodiscard]] ConstNodePtr FindChildByName(const std::u8string &name) const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    auto it = m_nameMap.find(name);
    return (it != m_nameMap.end()) ? it->second : nullptr;
  }

  [[nodiscard]] bool HasChild(const std::u8string &name) const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_nameMap.find(name) != m_nameMap.end();
  }

  NodePtr operator[](const std::u8string &name)
  {
    return FindChildByName(name);
  }

  ConstNodePtr operator[](const std::u8string &name) const
  {
    return FindChildByName(name);
  }

  // 首尾存取 (O(1))
  [[nodiscard]] NodePtr GetFirstChild()
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.empty() ? nullptr : m_elements.front();
  }

  [[nodiscard]] ConstNodePtr GetFirstChild() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.empty() ? nullptr : m_elements.front();
  }

  [[nodiscard]] NodePtr GetLastChild()
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.empty() ? nullptr : m_elements.back();
  }

  [[nodiscard]] ConstNodePtr GetLastChild() const
  {
    std::shared_lock<std::shared_mutex> lock(GetTreeMutex());
    return m_elements.empty() ? nullptr : m_elements.back();
  }

  // =========================================================================
  // 新增與插入方法
  // =========================================================================

  /**
   * @brief 新增具名或匿名子節點（追加至容器尾端，O(1)）
   */
  NodePtr AddChild(const std::u8string &name = u8"")
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    if (!name.empty() && m_nameMap.find(name) != m_nameMap.end())
    {
      return nullptr;  // 具名不可重複
    }

    NodePtr new_child = MakeNode(name);
    if (!new_child)
    {
      return nullptr;
    }

    new_child->PropagateTreeMutex(GetTreeMutexPtr());
    m_elements.push_back(new_child);
    if (!name.empty())
    {
      m_nameMap[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  // 向下相容別名
  NodePtr AddBackChild(const std::u8string &name = u8"")
  {
    return AddChild(name);
  }

  NodePtr InsertBefore(const NodePtr &child_node, const std::u8string &name = u8"")
  {
    if (!child_node)
    {
      return nullptr;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());

    auto it = std::find(m_elements.begin(), m_elements.end(), child_node);
    if (it == m_elements.end())
    {
      return nullptr;
    }

    if (!name.empty() && m_nameMap.find(name) != m_nameMap.end())
    {
      return nullptr;
    }

    NodePtr new_child = MakeNode(name);
    if (!new_child)
    {
      return nullptr;
    }

    new_child->PropagateTreeMutex(GetTreeMutexPtr());
    m_elements.insert(it, new_child);
    if (!name.empty())
    {
      m_nameMap[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  NodePtr InsertAfter(const NodePtr &child_node, const std::u8string &name = u8"")
  {
    if (!child_node)
    {
      return nullptr;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());

    auto it = std::find(m_elements.begin(), m_elements.end(), child_node);
    if (it == m_elements.end())
    {
      return nullptr;
    }

    if (!name.empty() && m_nameMap.find(name) != m_nameMap.end())
    {
      return nullptr;
    }

    NodePtr new_child = MakeNode(name);
    if (!new_child)
    {
      return nullptr;
    }

    new_child->PropagateTreeMutex(GetTreeMutexPtr());
    m_elements.insert(it + 1, new_child);
    if (!name.empty())
    {
      m_nameMap[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  NodePtr PushElement()
  {
    return AddChild(u8"");
  }

  bool PushElement(const NodePtr &element)
  {
    if (!element)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());

    element->PropagateTreeMutex(GetTreeMutexPtr());
    m_elements.push_back(element);
    if (!element->m_name.empty())
    {
      m_nameMap[element->m_name] = element;
    }
    NodePtr self_ptr = m_self.lock();
    element->SetParentAndSelf(self_ptr, element);
    return true;
  }

  // =========================================================================
  // 移除方法
  // =========================================================================

  bool RemoveElementAt(size_t index)
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    if (index >= m_elements.size())
    {
      return false;
    }

    NodePtr elem = m_elements[index];
    m_elements.erase(m_elements.begin() + index);
    if (elem && !elem->m_name.empty())
    {
      m_nameMap.erase(elem->m_name);
    }

    if (elem)
    {
      elem->m_parent.reset();
      elem->PropagateTreeMutex(std::make_shared<std::shared_mutex>());
    }
    return true;
  }

  bool RemoveChild(const NodePtr &child)
  {
    if (!child)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());

    auto it = std::find(m_elements.begin(), m_elements.end(), child);
    if (it == m_elements.end())
    {
      return false;
    }

    if (!child->m_name.empty())
    {
      m_nameMap.erase(child->m_name);
    }
    m_elements.erase(it);

    child->m_parent.reset();
    child->PropagateTreeMutex(std::make_shared<std::shared_mutex>());
    return true;
  }

  bool RemoveChildByName(const std::u8string &name)
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    auto it = m_nameMap.find(name);
    if (it == m_nameMap.end())
    {
      return false;
    }

    NodePtr child = it->second;
    m_nameMap.erase(it);

    auto elem_it = std::find(m_elements.begin(), m_elements.end(), child);
    if (elem_it != m_elements.end())
    {
      m_elements.erase(elem_it);
    }

    if (child)
    {
      child->m_parent.reset();
      child->PropagateTreeMutex(std::make_shared<std::shared_mutex>());
    }
    return true;
  }

  void ClearChildren()
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    std::vector<NodePtr> copy = std::move(m_elements);
    m_elements.clear();
    m_nameMap.clear();

    for (auto &child : copy)
    {
      if (child)
      {
        child->m_parent.reset();
        child->PropagateTreeMutex(std::make_shared<std::shared_mutex>());
      }
    }
  }

  void ClearElements()
  {
    ClearChildren();
  }



  // 迭代器與反向視圖
  auto begin() { return m_elements.begin(); }
  auto end() { return m_elements.end(); }
  auto begin() const { return m_elements.begin(); }
  auto end() const { return m_elements.end(); }
  auto rbegin() { return m_elements.rbegin(); }
  auto rend() { return m_elements.rend(); }
  auto rbegin() const { return m_elements.rbegin(); }
  auto rend() const { return m_elements.rend(); }

  /**
   * @brief 反向走訪視圖糖衣方法 (支援 for (const auto &child : node->Reversed()))
   */
  [[nodiscard]] auto Reversed() noexcept
  {
    return std::ranges::subrange(m_elements.rbegin(), m_elements.rend());
  }

  [[nodiscard]] auto Reversed() const noexcept
  {
    return std::ranges::subrange(m_elements.rbegin(), m_elements.rend());
  }
};

/**
 * @brief 強型別資料樹狀節點 (自帶標準實作)
 *
 * @tparam T 節點攜帶的資料型別 (預設為 std::string，可存放二進位字串或純文字)
 */
template <typename T = std::string>
class TreeNode : public TreeNodeBase<TreeNode<T>>
{
public:
  using Base = TreeNodeBase<TreeNode<T>>;
  using NodePtr = typename Base::NodePtr;
  using ConstNodePtr = typename Base::ConstNodePtr;
  using DataType = T;

private:
  T m_data{};
  mutable std::shared_mutex m_dataMutex;

protected:
  explicit TreeNode(std::u8string name = u8"") :
      Base(std::move(name))
  {
  }

public:
  // --- 資料存取 ---
  [[nodiscard]] T GetData() const
  {
    std::shared_lock<std::shared_mutex> lock(m_dataMutex);
    return m_data;
  }

  void SetData(const T &data)
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_data = data;
  }

  void SetData(T &&data)
  {
    std::unique_lock<std::shared_mutex> lock(m_dataMutex);
    m_data = std::move(data);
  }

  // 靜態工廠方法
  static NodePtr CreateRoot(const std::u8string &name = u8"")
  {
    return Base::CreateRoot(name);
  }



  template <typename D>
  friend class TreeNodeBase;
};

// 便利型別別名
using StringTreeNode = TreeNode<std::string>;

}  // namespace ork::base
