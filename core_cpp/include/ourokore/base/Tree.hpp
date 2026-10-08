#pragma once

#include <ourokore/base/export.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
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

class TreeIO;

/**
 * @brief 樹狀節點清理狀態追蹤器 (Tree Cleanup Tracker)
 *
 * 原理：整棵樹的所有節點均共享同一個 m_treeMutex (std::shared_ptr<std::shared_mutex>)。
 * 本追蹤器僅包裝該共享鎖的弱引用 (std::weak_ptr<std::shared_mutex>)，不干涉或延長節點生命週期。
 * 當且僅當所有節點均已解構釋放時，該共享鎖將隨之銷毀 (expired)。
 * 藉此以 O(1) 時間與極小記憶體開銷，精準判定「整棵樹的所有節點是否均已全數清除釋放」。
 */
class TreeCleanupTracker
{
public:
  TreeCleanupTracker() = default;
  explicit TreeCleanupTracker(std::weak_ptr<std::shared_mutex> mutex) noexcept :
      m_mutexWeak(std::move(mutex))
  {
  }

  /// @brief 檢查整棵樹的所有節點是否均已全數清除 (鎖已銷毀即代表所有持有節點均已析構)
  [[nodiscard]] bool AreAllNodesCleanedUp() const noexcept
  {
    return m_mutexWeak.expired();
  }

  /// @brief 別名捷徑：是否已全數清理
  [[nodiscard]] bool IsCleanedUp() const noexcept
  {
    return m_mutexWeak.expired();
  }

  /// @brief 檢查樹是否仍有節點存活
  [[nodiscard]] bool IsAlive() const noexcept
  {
    return !m_mutexWeak.expired();
  }

  /// @brief 取得目前仍持有該鎖的參照計數 (即剩餘節點與持有者數量預估)
  [[nodiscard]] long UseCount() const noexcept
  {
    return m_mutexWeak.use_count();
  }

  /// @brief 取得底層 weak_ptr 原生物件
  [[nodiscard]] const std::weak_ptr<std::shared_mutex> &GetRawWeakPtr() const noexcept
  {
    return m_mutexWeak;
  }

private:
  std::weak_ptr<std::shared_mutex> m_mutexWeak;
};

/**
 * @brief 基底樹狀物件節點 (CRTP 架構，支援異質物件階層)
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
  friend class TreeIO;

public:
  using NodePtr = std::shared_ptr<D>;
  using ConstNodePtr = std::shared_ptr<const D>;

protected:
  /**
   * @brief 受保護建構子：僅供衍生領域物件節點類別於構造初始化時調用
   *
   * 外部使用者禁止直接實例化未封裝之 TreeNodeBase，應透過衍生類別或 CreateRoot()。
   */
  explicit TreeNodeBase(std::u8string name = u8"") :
      m_name(std::move(name)),
      m_treeMutex(std::make_shared<std::shared_mutex>())
  {
  }

private:
  std::u8string m_name;  ///< 節點名稱 (UTF-8)
  // --- 唯一真實子節點儲存通道 (連續記憶體快取友善，支援 O(1) 循序/下標隨機存取) ---
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

  [[nodiscard]] std::shared_ptr<std::shared_mutex> GetTreeMutexPtr() const noexcept
  {
    if (!m_treeMutex)
    {
      m_treeMutex = std::make_shared<std::shared_mutex>();
    }
    return m_treeMutex;
  }

  // 工廠方法：建構節點（私有內部受控調用）
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  static std::shared_ptr<SubT> MakeNode(const std::u8string &name = u8"", Args &&...args)
  {
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
      static_assert(
          requires { new SubT(name, std::forward<Args>(args)...); } ||
              requires { new SubT(std::forward<Args>(args)...); },
          "SubT must be constructible either as new SubT(name, args...) or new SubT(args...)"
      );
      return nullptr;
    }
  }

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

public:
  [[nodiscard]] std::shared_mutex &GetTreeMutex() const noexcept
  {
    if (!m_treeMutex)
    {
      m_treeMutex = std::make_shared<std::shared_mutex>();
    }
    return *m_treeMutex;
  }

  /**
   * @brief 取得樹狀節點清理狀態追蹤器 (TreeCleanupTracker)
   *
   * 透過弱引用整棵樹共享的讀寫鎖 (m_treeMutex)，在不干涉節點生命週期的前提下，
   * 以 O(1) 效率監控所有節點是否均已完全清除釋放。
   */
  [[nodiscard]] TreeCleanupTracker GetCleanupTracker() const noexcept
  {
    return TreeCleanupTracker(GetTreeMutexPtr());
  }

  /**
   * @brief 斷開與父節點的關聯並自立為新樹（從父節點完全移除並分配專屬共享鎖）
   * @return 若成功從父節點斷開則傳回 true；若原本即無父節點則傳回 false。
   */
  bool DetachFromParent()
  {
    NodePtr parent = m_parent.lock();
    NodePtr self = m_self.lock();
    if (parent && self)
    {
      return parent->RemoveChild(self);
    }
    return false;
  }

  /**
   * @brief 將自身移動至同層目標兄弟節點之前（同層重排捷徑）
   * @param target 同層目標兄弟節點
   * @return 若自身或目標無效、或不在同一父節點下則回傳 false；移動成功傳回 true。
   */
  bool MoveBefore(const NodePtr &target)
  {
    NodePtr parent = m_parent.lock();
    NodePtr self = m_self.lock();
    if (parent && self && target)
    {
      return parent->MoveChildBefore(self, target);
    }
    return false;
  }

  /**
   * @brief 將自身移動至同層目標兄弟節點之後（同層重排捷徑）
   * @param target 同層目標兄弟節點
   * @return 若自身或目標無效、或不在同一父節點下則回傳 false；移動成功傳回 true。
   */
  bool MoveAfter(const NodePtr &target)
  {
    NodePtr parent = m_parent.lock();
    NodePtr self = m_self.lock();
    if (parent && self && target)
    {
      return parent->MoveChildAfter(self, target);
    }
    return false;
  }

  /**
   * @brief 將自身移動至父節點指定之下標位置（同層重排捷徑）
   * @param new_index 目標下標位置
   * @return 若自身無父節點則回傳 false；移動成功傳回 true。
   */
  bool MoveToIndex(size_t new_index)
  {
    NodePtr parent = m_parent.lock();
    NodePtr self = m_self.lock();
    if (parent && self)
    {
      return parent->MoveChildToIndex(self, new_index);
    }
    return false;
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
  // 子節點與陣列元素數量與存取通道 (統合單一序列)
  // =========================================================================

  [[nodiscard]] size_t ChildCount() const
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
   * @brief 新增具名或匿名子物件節點（追加至尾端，O(1)）
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

  /**
   * @brief 在最前端插入具名或匿名子物件節點（Prepend）
   */
  template <typename SubT = D, typename... Args>
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> PrependChild(const std::u8string &name = u8"", Args &&...args)
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
    m_elements.insert(m_elements.begin(), new_child);
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
    requires std::derived_from<SubT, D>
  std::shared_ptr<SubT> PushElement(Args &&...args)
  {
    return AddChild<SubT>(u8"", std::forward<Args>(args)...);
  }

  // =========================================================================
  // 同層順序搬移與重排方法 (Sibling Reordering)
  // =========================================================================

  /**
   * @brief 將既有同層子節點移動至目標兄弟節點之前 (MoveChildBefore)
   * @param child 待移動之子節點
   * @param target 目標兄弟節點
   * @return 若 child 或 target 為空、或不在當前節點容器內則回傳 false；移動成功傳回 true。
   */
  bool MoveChildBefore(const NodePtr &child, const NodePtr &target)
  {
    if (!child || !target)
    {
      return false;
    }
    if (child == target)
    {
      return true;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    auto it_child = std::find(m_elements.begin(), m_elements.end(), child);
    if (it_child == m_elements.end())
    {
      return false;
    }
    auto it_target = std::find(m_elements.begin(), m_elements.end(), target);
    if (it_target == m_elements.end())
    {
      return false;
    }

    if (it_child < it_target)
    {
      std::rotate(it_child, it_child + 1, it_target);
    }
    else
    {
      std::rotate(it_target, it_child, it_child + 1);
    }
    return true;
  }

  /**
   * @brief 將既有同層子節點移動至目標兄弟節點之後 (MoveChildAfter)
   * @param child 待移動之子節點
   * @param target 目標兄弟節點
   * @return 若 child 或 target 為空、或不在當前節點容器內則回傳 false；移動成功傳回 true。
   */
  bool MoveChildAfter(const NodePtr &child, const NodePtr &target)
  {
    if (!child || !target)
    {
      return false;
    }
    if (child == target)
    {
      return true;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    auto it_child = std::find(m_elements.begin(), m_elements.end(), child);
    if (it_child == m_elements.end())
    {
      return false;
    }
    auto it_target = std::find(m_elements.begin(), m_elements.end(), target);
    if (it_target == m_elements.end())
    {
      return false;
    }

    if (it_child < it_target)
    {
      std::rotate(it_child, it_child + 1, it_target + 1);
    }
    else
    {
      std::rotate(it_target + 1, it_child, it_child + 1);
    }
    return true;
  }

  /**
   * @brief 將既有同層子節點移動至指定下標位置 (MoveChildToIndex)
   * @param child 待移動之子節點
   * @param new_index 目標下標（若大於等於元素數量則移動至最末端）
   * @return 若 child 為空、或不在當前節點容器內則回傳 false；移動成功傳回 true。
   */
  bool MoveChildToIndex(const NodePtr &child, size_t new_index)
  {
    if (!child)
    {
      return false;
    }
    std::unique_lock<std::shared_mutex> lock(GetTreeMutex());
    if (m_elements.empty())
    {
      return false;
    }
    auto it_child = std::find(m_elements.begin(), m_elements.end(), child);
    if (it_child == m_elements.end())
    {
      return false;
    }

    if (new_index >= m_elements.size())
    {
      new_index = m_elements.size() - 1;
    }

    size_t old_index = static_cast<size_t>(std::distance(m_elements.begin(), it_child));
    if (old_index == new_index)
    {
      return true;
    }

    if (old_index < new_index)
    {
      std::rotate(it_child, it_child + 1, m_elements.begin() + new_index + 1);
    }
    else
    {
      std::rotate(m_elements.begin() + new_index, it_child, it_child + 1);
    }
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
