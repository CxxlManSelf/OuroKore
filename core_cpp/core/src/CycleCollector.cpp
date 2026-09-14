#include "CycleCollector.h"

#include <algorithm>
#include <queue>

#include "ControlBlock.h"
#include "Registry.h"

namespace ork
{

CycleCollector &CycleCollector::GetInstance()
{
  static CycleCollector instance;
  return instance;
}

CycleCollector::CycleCollector()
{
  Start();
}

CycleCollector::~CycleCollector()
{
  Stop();
}

void CycleCollector::Start()
{
  if (m_running.load(std::memory_order_acquire))
  {
    return;
  }

  m_running.store(true, std::memory_order_release);
  m_worker_thread = std::thread([this]() { WorkerLoop(); });
}

void CycleCollector::Stop()
{
  if (!m_running.load(std::memory_order_acquire))
  {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_running.store(false, std::memory_order_release);
    m_cv.notify_all();
  }

  if (m_worker_thread.joinable())
  {
    m_worker_thread.join();
  }
}

void CycleCollector::PushSuspect(HandleID target_id)
{
  if (target_id == ORK_ROOT_ID)
  {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_suspect_queue.push_back(target_id);
    m_cv.notify_one();
  }
}

size_t CycleCollector::SuspectCount() const
{
  std::lock_guard<std::mutex> lock(m_queue_mutex);
  return m_suspect_queue.size();
}

void CycleCollector::CollectCyclesExplicit()
{
  // 取出當前所有排隊中的嫌疑犯進行批次分析
  std::vector<HandleID> current_batch;
  {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    current_batch.swap(m_suspect_queue);
  }

  m_known_alive_in_run.clear();

  for (HandleID suspect_id : current_batch)
  {
    ProcessSuspect(suspect_id);
  }

  m_known_alive_in_run.clear();
}

void CycleCollector::WorkerLoop()
{
  while (m_running.load(std::memory_order_acquire))
  {
    std::vector<HandleID> current_batch;
    {
      std::unique_lock<std::mutex> lock(m_queue_mutex);
      m_cv.wait(lock, [this]() {
        return !m_running.load(std::memory_order_acquire) || !m_suspect_queue.empty();
      });

      if (!m_running.load(std::memory_order_acquire) && m_suspect_queue.empty())
      {
        break;
      }

      current_batch.swap(m_suspect_queue);
    }

    m_known_alive_in_run.clear();

    for (HandleID suspect_id : current_batch)
    {
      if (!m_running.load(std::memory_order_acquire))
      {
        break;
      }
      ProcessSuspect(suspect_id);
    }

    m_known_alive_in_run.clear();
  }
}

void CycleCollector::ProcessSuspect(HandleID suspect_id)
{
  ControlBlock *cb = Registry::GetInstance().GetControlBlock(suspect_id);
  if (!cb)
  {
    return;
  }

  // 雙重存活審查 (Double-Check Guard)
  // 若物件已被標記為正在拆解，或其 StrongCount == 0（肉體已死/已在延遲銷毀中），略過
  if (cb->m_is_destructing.load(std::memory_order_acquire) ||
      cb->m_strong_count.load(std::memory_order_acquire) == 0)
  {
    cb->m_in_suspect_queue.store(false, std::memory_order_release);
    return;
  }

  // 重置入隊旗標，若後續判定存活後又斷邊，允許再次送檢
  cb->m_in_suspect_queue.store(false, std::memory_order_release);

  // 執行逆向圖論走訪 (Upstream BFS)
  std::unordered_set<HandleID> visited;
  std::queue<HandleID> q;
  std::vector<HandleID> component;

  q.push(suspect_id);
  visited.insert(suspect_id);

  bool has_external_root = false;

  while (!q.empty())
  {
    HandleID curr = q.front();
    q.pop();
    component.push_back(curr);

    // 若當前節點已被確認存活（觸及之前的存活快取），則整條路徑存活
    if (m_known_alive_in_run.find(curr) != m_known_alive_in_run.end())
    {
      has_external_root = true;
      break;
    }

    ControlBlock *curr_cb = Registry::GetInstance().GetControlBlock(curr);
    if (!curr_cb)
    {
      continue;
    }

    // 第一層防禦：微秒級名冊快照 (Micro-Snapshot)
    std::vector<HandleID> owners_snapshot;
    {
      std::lock_guard<std::mutex> owners_lock(curr_cb->m_owners_mutex);
      owners_snapshot = curr_cb->m_owners;
    }

    // 若節點無任何 owner（入度為 0），代表非閉環依賴（可能為外部持有或併發過渡態），不視為孤島
    if (owners_snapshot.empty())
    {
      has_external_root = true;
      break;
    }

    for (HandleID owner : owners_snapshot)
    {
      // 觸及 ORK_ROOT_ID，宣告存活 (Early Exit)
      if (owner == ORK_ROOT_ID)
      {
        has_external_root = true;
        break;
      }

      // 若該 owner 為外部非託管宿主（在 Registry 中無 ControlBlock），視為外部持有存活
      ControlBlock *owner_cb = Registry::GetInstance().GetControlBlock(owner);
      if (!owner_cb)
      {
        has_external_root = true;
        break;
      }

      // 若該 owner 已被標記為已知存活，亦直接存活
      if (m_known_alive_in_run.find(owner) != m_known_alive_in_run.end())
      {
        has_external_root = true;
        break;
      }

      // 單次走訪防環標記 (Visited Set)
      if (visited.find(owner) == visited.end())
      {
        visited.insert(owner);
        q.push(owner);
      }
    }

    if (has_external_root)
    {
      break;
    }
  }

  if (has_external_root)
  {
    // 跨次走訪快取標記：將本次已走訪確認連向 Root/外部的節點加入 known_alive 快取
    for (HandleID id : visited)
    {
      m_known_alive_in_run.insert(id);
    }
    return;
  }

  // 初步判定為死結孤島，進入第二、三、四層防禦協議進行二階段確認
  DestructIsland(component);
}

void CycleCollector::DestructIsland(const std::vector<HandleID> &island_nodes)
{
  if (island_nodes.empty())
  {
    return;
  }

  // 將孤島節點按 HandleID 排序，消除哲學家就餐死鎖
  std::vector<HandleID> sorted_nodes = island_nodes;
  std::sort(sorted_nodes.begin(), sorted_nodes.end());
  sorted_nodes.erase(std::unique(sorted_nodes.begin(), sorted_nodes.end()), sorted_nodes.end());

  std::unordered_set<HandleID> island_set(sorted_nodes.begin(), sorted_nodes.end());

  // 取得各節點的 ControlBlock
  std::vector<std::pair<HandleID, ControlBlock *>> active_blocks;
  active_blocks.reserve(sorted_nodes.size());
  for (HandleID id : sorted_nodes)
  {
    ControlBlock *cb = Registry::GetInstance().GetControlBlock(id);
    if (cb)
    {
      active_blocks.emplace_back(id, cb);
    }
  }

  if (active_blocks.size() != sorted_nodes.size())
  {
    // 有節點查無 ControlBlock（包含外部非託管節點），非純受管孤島，放棄拆解
    return;
  }

  // 第三層防禦：孤島二階段依序鎖定與二次複查 (Two-Phase Lock & Verify)
  // 依 HandleID 順序逐一獲取 m_owners_mutex 鎖
  std::vector<std::unique_lock<std::mutex>> locks;
  locks.reserve(active_blocks.size());
  for (auto &pair : active_blocks)
  {
    locks.emplace_back(pair.second->m_owners_mutex);
  }

  // 記錄孤島內部真實存在的互指邊緣名冊
  std::vector<std::pair<HandleID, HandleID>> edges_to_remove;
  for (auto &pair : active_blocks)
  {
    HandleID target_id = pair.first;
    ControlBlock *cb = pair.second;
    if (cb->m_owners.empty())
    {
      // 入度為 0 但仍在孤島佇列，可能為過渡狀態，放棄拆解
      return;
    }
    for (HandleID owner : cb->m_owners)
    {
      if (island_set.find(owner) == island_set.end())
      {
        // 發現外部 Owner（例如併發加邊或連回 Root），孤島已復活，立刻放棄拆解！
        return;
      }
      edges_to_remove.emplace_back(owner, target_id);
    }
  }

  // 第四層防禦：標記 Destructing 狀態（全面禁止併發加邊與殭屍復活）
  for (auto &pair : active_blocks)
  {
    pair.second->m_is_destructing.store(true, std::memory_order_release);
  }

  // 釋放所有 owners 鎖，準備發動外科手術式斷鏈
  locks.clear();

  // 外科手術式斷鏈 (Silent Unbind)：切斷孤島成員之間的相互依賴
  // 靜音模式：禁止斷鏈觸發再次入隊
  for (const auto &edge : edges_to_remove)
  {
    Registry::GetInstance().UnregisterEdge(edge.first, edge.second, /*silent=*/true);
  }

  // 斷鏈後，強計數歸零之孤島成員將由 DeferredDeleteQueue 接管物理銷毀
  // 收集器本身不卡在 delete 上，立即回頭處理下一批嫌疑犯
}

}  // namespace ork
