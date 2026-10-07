#pragma once

#include <ourokore/base/export.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <ourokore/base/utf8.hpp>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ork::base
{

/**
 * @brief 迭代式防爆棧節點刪除器（向後相容介面）
 *
 * 專為解決百萬層深樹在析構時引發的呼叫堆疊溢位 (Stack Overflow) 問題。
 * 樹狀結構已直接內建「迭代式展平析構（Iterative Flattening Destructor）」，
 * 在銷毀鏈路中以 O(1) 呼叫堆疊深度迭代清空，徹底杜絕 Stack Overflow。
 * 此介面保留作為向下相容之用，已無需啟動背景執行緒，100% 杜絕行程退出時的 UAF 與崩潰。
 */
class AsyncNodeDeletor
{
public:
  using Task = std::function<void()>;

  static void EnqueueTask(Task task)
  {
    if (task)
    {
      task();
    }
  }

  static void Wait() noexcept
  {
    // 迭代式析構為同步安全完成，無需等待
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
 * 4. 防爆棧析構：內建顯式堆疊迭代析構，將遞迴展平為堆積迴圈，杜絕巨深樹級聯析構爆棧。
 * 5. 全域 UTF-8：節點名稱強制使用 C++20 原生 std::u8string。
 */
template <typename D>
class TreeNodeBase
{
public:
  using NodePtr = std::shared_ptr<D>;
  using ConstNodePtr = std::shared_ptr<const D>;

protected:
  std::u8string m_name;  ///< 節點名稱 (UTF-8)
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

  // 工廠方法：建構節點（支援 D 及其衍生多型型別 SubT 與完美轉發構造參數）
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  static std::shared_ptr<SubT> MakeNode(const std::u8string &name = u8"", Args &&...args)
  {
    if constexpr (requires { SubT::CanCreateChild(name); })
    {
      if (!SubT::CanCreateChild(name))
      {
        return nullptr;
      }
    }
    else if constexpr (requires { D::CanCreateChild(name); })
    {
      if (!D::CanCreateChild(name))
      {
        return nullptr;
      }
    }

    if constexpr (requires { new SubT(name, std::forward<Args>(args)...); })
    {
      return std::shared_ptr<SubT>(new SubT(name, std::forward<Args>(args)...));
    }
    else if constexpr (requires { new SubT(std::forward<Args>(args)...); })
    {
      std::shared_ptr<SubT> node(new SubT(std::forward<Args>(args)...));
      node->SetName(name);
      return node;
    }
    else
    {
      static_assert(requires { new SubT(name, std::forward<Args>(args)...); } ||
                    requires { new SubT(std::forward<Args>(args)...); },
                    "SubT must be constructible either as new SubT(name, args...) or new SubT(args...)");
      return nullptr;
    }
  }

public:
  explicit TreeNodeBase(std::u8string name = u8"") :
      m_name(std::move(name)),
      m_treeMutex(std::make_shared<std::shared_mutex>())
  {
  }

  virtual ~TreeNodeBase()
  {
    // 防範深層樹遞迴析構引發呼叫堆疊溢位 (Stack Overflow)
    // 將遞迴鏈展平 (Flattening) 為堆積 (Heap) 上的顯式堆疊迭代，以 O(1) 呼叫深度安全釋放
    std::vector<NodePtr> nodes_to_delete;
    nodes_to_delete.reserve(m_elements.size());
    for (auto &elem : m_elements)
    {
      if (elem)
      {
        elem->m_parent.reset();
        nodes_to_delete.push_back(std::move(elem));
      }
    }
    m_elements.clear();
    m_nameMap.clear();

    while (!nodes_to_delete.empty())
    {
      NodePtr current = std::move(nodes_to_delete.back());
      nodes_to_delete.pop_back();

      if (current && current.use_count() == 1)
      {
        // current 僅由本地迭代棧唯一持有，其解構即將發生。
        // 為防範 current 解構時遞迴連鎖釋放其子節點，在此主動拔出其子節點並推入迭代向量中
        for (auto &child : current->m_elements)
        {
          if (child)
          {
            child->m_parent.reset();
            nodes_to_delete.push_back(std::move(child));
          }
        }
        current->m_elements.clear();
        current->m_nameMap.clear();
      }
    }
  }

  // 靜態鉤子預設實現：延伸類別可覆寫以加入自訂校驗
  static bool CanCreateChild([[maybe_unused]] const std::u8string &name)
  {
    return true;
  }

  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  static std::shared_ptr<SubT> CreateRoot(const std::u8string &name = u8"", Args &&...args)
  {
    std::shared_ptr<SubT> root = MakeNode<SubT>(name, std::forward<Args>(args)...);
    if (root)
    {
      root->m_self = root;
    }
    return root;
  }

  // 創建陣列根節點（便民別名）
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  static std::shared_ptr<SubT> CreateArray(const std::u8string &name = u8"", Args &&...args)
  {
    return CreateRoot<SubT>(name, std::forward<Args>(args)...);
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
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> AddChild(const std::u8string &name = u8"", Args &&...args)
  {
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    if (!name.empty() && m_nameMap.find(name) != m_nameMap.end())
    {
      return nullptr;  // 具名不可重複
    }

    std::shared_ptr<SubT> new_child = MakeNode<SubT>(name, std::forward<Args>(args)...);
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
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> AddBackChild(const std::u8string &name = u8"", Args &&...args)
  {
    return AddChild<SubT>(name, std::forward<Args>(args)...);
  }

  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> InsertBefore(const NodePtr &child_node, const std::u8string &name = u8"", Args &&...args)
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

    std::shared_ptr<SubT> new_child = MakeNode<SubT>(name, std::forward<Args>(args)...);
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

  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> InsertAfter(const NodePtr &child_node, const std::u8string &name = u8"", Args &&...args)
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

    std::shared_ptr<SubT> new_child = MakeNode<SubT>(name, std::forward<Args>(args)...);
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

  /**
   * @brief 原地構造並推入匿名陣列元素（支援衍生型別與完美轉發）
   */
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D> &&
             (!(sizeof...(Args) == 1 && (std::is_convertible_v<std::remove_cvref_t<Args>, NodePtr> && ...)))
  std::shared_ptr<SubT> PushElement(Args &&...args)
  {
    return AddChild<SubT>(u8"", std::forward<Args>(args)...);
  }

  /**
   * @brief 推入既有子節點指標（支援 D 或其衍生型別 SubT）
   */
  template <typename SubT = D>
    requires std::derived_from<SubT, D>
  bool PushElement(const std::shared_ptr<SubT> &element)
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

  /**
   * @brief 空指針多載（直接攔截 nullptr 字面量）
   */
  bool AttachChild(std::nullptr_t) noexcept
  {
    return false;
  }

  /**
   * @brief 掛載外部已構造好的子節點（支援 D 或其衍生型別 SubT）
   * @param child 待掛載之子節點指標
   * @return 若 child 為空，或回傳物件的名稱與同層既有具名節點重複，則回傳 false；成功掛載則回傳 true。
   */
  template <typename SubT = D>
    requires std::derived_from<SubT, D>
  bool AttachChild(const std::shared_ptr<SubT> &child)
  {
    if (!child)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());

    // 檢查回傳物件之名稱是否與同層具名名稱重複
    if (!child->m_name.empty() && m_nameMap.find(child->m_name) != m_nameMap.end())
    {
      return false;
    }

    child->PropagateTreeMutex(GetTreeMutexPtr());
    m_elements.push_back(child);
    if (!child->m_name.empty())
    {
      m_nameMap[child->m_name] = child;
    }
    NodePtr self_ptr = m_self.lock();
    child->SetParentAndSelf(self_ptr, child);
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
  auto begin()
  {
    return m_elements.begin();
  }
  auto end()
  {
    return m_elements.end();
  }
  auto begin() const
  {
    return m_elements.begin();
  }
  auto end() const
  {
    return m_elements.end();
  }
  auto rbegin()
  {
    return m_elements.rbegin();
  }
  auto rend()
  {
    return m_elements.rend();
  }
  auto rbegin() const
  {
    return m_elements.rbegin();
  }
  auto rend() const
  {
    return m_elements.rend();
  }

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
