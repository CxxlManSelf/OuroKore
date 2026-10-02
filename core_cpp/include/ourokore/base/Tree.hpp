#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <ourokore/base/export.h>
#include <ourokore/base/utf8.hpp>

namespace ork::base
{

/**
 * @brief 樹狀節點的形態類別 (Node Kind)
 */
enum class NodeKind : uint8_t
{
  Object = 0,  ///< 物件/字典節點：包含具名子節點集合（m_nameIndex 為主）
  Array = 1    ///< 陣列節點：包含純循序元素集合（支援 O(1) 隨機下標存取）
};

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
 * 3. 執行緒安全：採用 std::shared_mutex 提供多讀單寫（Shared/Unique Lock）機制。
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
  NodeKind m_kind{NodeKind::Object};    ///< 節點型態 (Object 或 Array)

  // --- 具名字節點通道 (Object Mode) ---
  std::list<NodePtr> m_children;                                                ///< 循序子節點列表
  std::unordered_map<std::u8string, NodePtr> m_nameIndex;                       ///< 名稱索引 (O(1) 尋址)
  std::unordered_map<D *, typename std::list<NodePtr>::iterator> m_childIndex;  ///< 反向指標索引 (O(1) 插入重排)

  // --- 陣列元素通道 (Array Mode，連續記憶體極速隨機存取) ---
  std::vector<NodePtr> m_elements;                                              ///< 循序陣列元素

  // 拓撲關聯
  std::weak_ptr<D> m_parent;  ///< 父節點弱引用
  std::weak_ptr<D> m_self;    ///< 自身弱引用

  // 執行緒安全鎖
  mutable std::shared_mutex m_mutex;

  void SetParentAndSelf(const NodePtr &parent, const NodePtr &self)
  {
    m_parent = parent;
    m_self = self;
  }

public:
  /**
   * @brief 斷開與父節點的關聯
   */
  void DetachFromParent()
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_parent.reset();
  }

  // 工廠方法：建構節點並掛載非同步防爆棧析構器
  static NodePtr MakeNode(const std::u8string &name, NodeKind kind = NodeKind::Object)
  {
    if (!D::CanCreateChild(name))
    {
      return nullptr;
    }

    std::shared_ptr<D> node(new D(name), [](D *p) {
      if (p)
      {
        AsyncNodeDeletor::EnqueueTask([p]() { delete p; });
      }
    });

    if (node)
    {
      node->m_kind = kind;
    }
    return node;
  }

public:
  explicit TreeNodeBase(std::u8string name = u8"") :
      m_name(std::move(name))
  {
  }

  virtual ~TreeNodeBase()
  {
    // 解構前主動斷開子節點 parent 弱指針，防範循環懸空
    for (auto &child : m_children)
    {
      if (child)
      {
        child->m_parent.reset();
      }
    }
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

  // 創建根節點
  static NodePtr CreateRoot(const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    NodePtr root = MakeNode(name, kind);
    if (root)
    {
      root->m_self = root;
    }
    return root;
  }

  // --- 基本屬性 ---
  [[nodiscard]] const std::u8string &GetName() const noexcept
  {
    return m_name;
  }

  void SetName(const std::u8string &name)
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_name = name;
  }

  [[nodiscard]] NodeKind GetKind() const noexcept
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_kind;
  }

  void SetKind(NodeKind kind) noexcept
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_kind = kind;
  }

  [[nodiscard]] bool IsArray() const noexcept
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_kind == NodeKind::Array;
  }

  [[nodiscard]] bool IsObject() const noexcept
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_kind == NodeKind::Object;
  }

  // 取得父節點與自身
  [[nodiscard]] NodePtr GetParent()
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_parent.lock();
  }

  [[nodiscard]] ConstNodePtr GetParent() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_parent.lock();
  }

  [[nodiscard]] NodePtr GetSelf()
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_self.lock();
  }

  [[nodiscard]] ConstNodePtr GetSelf() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_self.lock();
  }

  // =========================================================================
  // 陣列元素操作通道 (Array Mode - O(1) 隨機下標存取)
  // =========================================================================

  /**
   * @brief 取得陣列元素個數 (O(1))
   */
  [[nodiscard]] size_t ElementCount() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_elements.size();
  }

  /**
   * @brief 陣列下標隨機存取 (O(1))
   */
  [[nodiscard]] NodePtr GetElementAt(size_t index)
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (index < m_elements.size())
    {
      return m_elements[index];
    }
    return nullptr;
  }

  [[nodiscard]] ConstNodePtr GetElementAt(size_t index) const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (index < m_elements.size())
    {
      return m_elements[index];
    }
    return nullptr;
  }

  /**
   * @brief 運算子隨機存取陣列元素 (O(1))
   */
  NodePtr operator[](size_t index)
  {
    return GetElementAt(index);
  }

  ConstNodePtr operator[](size_t index) const
  {
    return GetElementAt(index);
  }

  /**
   * @brief 向陣列尾端追加元素 (O(1))
   */
  NodePtr PushElement(NodeKind element_kind = NodeKind::Object)
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_kind = NodeKind::Array;  // 自動切換為陣列形態

    NodePtr new_elem = MakeNode(u8"", element_kind);
    if (!new_elem)
    {
      return nullptr;
    }

    m_elements.push_back(new_elem);
    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    new_elem->SetParentAndSelf(self_ptr, new_elem);
    return new_elem;
  }

  /**
   * @brief 向陣列尾端追加既有節點 (O(1))
   */
  bool PushElement(const NodePtr &element)
  {
    if (!element)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_kind = NodeKind::Array;

    m_elements.push_back(element);
    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    element->SetParentAndSelf(self_ptr, element);
    return true;
  }

  /**
   * @brief 移除指定下標的陣列元素
   */
  bool RemoveElementAt(size_t index)
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (index >= m_elements.size())
    {
      return false;
    }

    NodePtr elem = m_elements[index];
    m_elements.erase(m_elements.begin() + index);
    lock.unlock();

    if (elem)
    {
      std::unique_lock<std::shared_mutex> elem_lock(elem->m_mutex);
      elem->m_parent.reset();
    }
    return true;
  }

  /**
   * @brief 清空所有陣列元素
   */
  void ClearElements()
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::vector<NodePtr> copy = std::move(m_elements);
    m_elements.clear();
    lock.unlock();

    for (auto &elem : copy)
    {
      if (elem)
      {
        std::unique_lock<std::shared_mutex> elem_lock(elem->m_mutex);
        elem->m_parent.reset();
      }
    }
  }

  // =========================================================================
  // 具名字節點操作通道 (Object Mode)
  // =========================================================================

  /**
   * @brief 運算子依照名稱存取子節點 (O(1))
   */
  NodePtr operator[](const std::u8string &name)
  {
    return FindChildByName(name);
  }

  ConstNodePtr operator[](const std::u8string &name) const
  {
    return FindChildByName(name);
  }

  /**
   * @brief 新增具名字節點至開頭 (O(1))
   */
  NodePtr AddFrontChild(const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    if (!name.empty() && m_nameIndex.find(name) != m_nameIndex.end())
    {
      return nullptr;  // 名稱不可重複
    }

    NodePtr new_child = MakeNode(name, kind);
    if (!new_child)
    {
      return nullptr;
    }

    auto it = m_children.insert(m_children.begin(), new_child);
    m_childIndex[new_child.get()] = it;
    if (!name.empty())
    {
      m_nameIndex[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  /**
   * @brief 新增具名字節點至尾端 (O(1))
   */
  NodePtr AddBackChild(const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    if (!name.empty() && m_nameIndex.find(name) != m_nameIndex.end())
    {
      return nullptr;  // 名稱不可重複
    }

    NodePtr new_child = MakeNode(name, kind);
    if (!new_child)
    {
      return nullptr;
    }

    auto it = m_children.insert(m_children.end(), new_child);
    m_childIndex[new_child.get()] = it;
    if (!name.empty())
    {
      m_nameIndex[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  /**
   * @brief 在指定子節點前插入新節點 (O(1))
   */
  NodePtr InsertBefore(const NodePtr &child_node, const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    if (!child_node)
    {
      return nullptr;
    }
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto index_it = m_childIndex.find(child_node.get());
    if (index_it == m_childIndex.end())
    {
      return nullptr;
    }

    if (!name.empty() && m_nameIndex.find(name) != m_nameIndex.end())
    {
      return nullptr;
    }

    NodePtr new_child = MakeNode(name, kind);
    if (!new_child)
    {
      return nullptr;
    }

    auto new_it = m_children.insert(index_it->second, new_child);
    m_childIndex[new_child.get()] = new_it;
    if (!name.empty())
    {
      m_nameIndex[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  /**
   * @brief 在指定子節點後插入新節點 (O(1))
   */
  NodePtr InsertAfter(const NodePtr &child_node, const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    if (!child_node)
    {
      return nullptr;
    }
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto index_it = m_childIndex.find(child_node.get());
    if (index_it == m_childIndex.end())
    {
      return nullptr;
    }

    if (!name.empty() && m_nameIndex.find(name) != m_nameIndex.end())
    {
      return nullptr;
    }

    NodePtr new_child = MakeNode(name, kind);
    if (!new_child)
    {
      return nullptr;
    }

    auto new_it = m_children.insert(std::next(index_it->second), new_child);
    m_childIndex[new_child.get()] = new_it;
    if (!name.empty())
    {
      m_nameIndex[name] = new_child;
    }

    NodePtr self_ptr = m_self.lock();
    lock.unlock();

    new_child->SetParentAndSelf(self_ptr, new_child);
    return new_child;
  }

  /**
   * @brief 按名稱尋找子節點 (O(1))
   */
  [[nodiscard]] NodePtr FindChildByName(const std::u8string &name)
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end())
    {
      return it->second;
    }
    return nullptr;
  }

  [[nodiscard]] ConstNodePtr FindChildByName(const std::u8string &name) const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end())
    {
      return it->second;
    }
    return nullptr;
  }

  /**
   * @brief 檢查是否存在指定名稱之子節點 (O(1))
   */
  [[nodiscard]] bool HasChild(const std::u8string &name) const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_nameIndex.find(name) != m_nameIndex.end();
  }

  /**
   * @brief 取得子節點總數 (O(1))
   */
  [[nodiscard]] size_t ChildCount() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_children.size();
  }

  /**
   * @brief 移除子節點 (O(1))
   */
  bool RemoveChild(const NodePtr &child)
  {
    if (!child)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto index_it = m_childIndex.find(child.get());
    if (index_it == m_childIndex.end())
    {
      return false;
    }

    auto list_it = index_it->second;
    if (!child->GetName().empty())
    {
      m_nameIndex.erase(child->GetName());
    }

    m_children.erase(list_it);
    m_childIndex.erase(index_it);
    lock.unlock();

    std::unique_lock<std::shared_mutex> child_lock(child->m_mutex);
    child->m_parent.reset();
    return true;
  }

  /**
   * @brief 依據名稱移除子節點 (O(1))
   */
  bool RemoveChildByName(const std::u8string &name)
  {
    NodePtr child = FindChildByName(name);
    if (child)
    {
      return RemoveChild(child);
    }
    return false;
  }

  /**
   * @brief 清空所有子節點
   */
  void ClearChildren()
  {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::list<NodePtr> copy = std::move(m_children);
    m_nameIndex.clear();
    m_childIndex.clear();
    lock.unlock();

    for (auto &child : copy)
    {
      if (child)
      {
        std::unique_lock<std::shared_mutex> child_lock(child->m_mutex);
        child->m_parent.reset();
      }
    }
  }

  // --- 首尾子節點快速存取 (O(1)) ---
  [[nodiscard]] NodePtr GetFirstChild()
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_children.empty() ? nullptr : m_children.front();
  }

  [[nodiscard]] ConstNodePtr GetFirstChild() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_children.empty() ? nullptr : m_children.front();
  }

  [[nodiscard]] NodePtr GetLastChild()
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_children.empty() ? nullptr : m_children.back();
  }

  [[nodiscard]] ConstNodePtr GetLastChild() const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_children.empty() ? nullptr : m_children.back();
  }

  // --- 遍歷走訪 ---
  void ForEachChild(const std::function<void(const NodePtr &)> &callback)
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::list<NodePtr> copy = m_children;
    lock.unlock();

    for (const auto &child : copy)
    {
      callback(child);
    }
  }

  void ForEachChild(const std::function<void(const ConstNodePtr &)> &callback) const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::list<NodePtr> copy = m_children;
    lock.unlock();

    for (const auto &child : copy)
    {
      callback(child);
    }
  }

  void ForEachElement(const std::function<void(const NodePtr &)> &callback)
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<NodePtr> copy = m_elements;
    lock.unlock();

    for (const auto &elem : copy)
    {
      callback(elem);
    }
  }

  void ForEachElement(const std::function<void(const ConstNodePtr &)> &callback) const
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<NodePtr> copy = m_elements;
    lock.unlock();

    for (const auto &elem : copy)
    {
      callback(elem);
    }
  }

  // 子節點迭代器
  auto begin() { return m_children.begin(); }
  auto end() { return m_children.end(); }
  auto rbegin() { return m_children.rbegin(); }
  auto rend() { return m_children.rend(); }
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
  static NodePtr CreateRoot(const std::u8string &name = u8"", NodeKind kind = NodeKind::Object)
  {
    return Base::CreateRoot(name, kind);
  }

  template <typename D>
  friend class TreeNodeBase;
};

// 便利型別別名
using StringTreeNode = TreeNode<std::string>;
using Tree = TreeNode<std::string>;

}  // namespace ork::base
