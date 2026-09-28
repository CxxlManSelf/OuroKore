#include "TypeRegistry.h"
#include <mutex>

namespace ork
{

TypeRegistry &TypeRegistry::GetInstance()
{
  static TypeRegistry s_instance;
  return s_instance;
}

TypeRegistry::TypeRegistry()
{
  // 核心啟動時預先登記 OuroObject 為根型別
  m_types[kOuroObjectTypeID] = TypeNode{
      .type_id = kOuroObjectTypeID,
      .name = "OuroObject",
      .parent_type_id = ORK_INVALID_TYPE_ID
  };
}

bool TypeRegistry::RegisterType(ork_type_id_t type_id, std::string name, ork_type_id_t parent_type_id)
{
  if (type_id == ORK_INVALID_TYPE_ID || type_id == parent_type_id)
  {
    return false;
  }

  // 若未指定 parent_type_id 且非 OuroObject，預設掛載於 OuroObject 之下
  if (parent_type_id == ORK_INVALID_TYPE_ID && type_id != kOuroObjectTypeID)
  {
    parent_type_id = kOuroObjectTypeID;
  }

  std::unique_lock<std::shared_mutex> lock(m_mutex);

  // 循環繼承防護（防止 A -> B -> A 惡意或錯誤設定）
  if (parent_type_id != ORK_INVALID_TYPE_ID)
  {
    ork_type_id_t curr = parent_type_id;
    size_t depth = 0;
    while (curr != ORK_INVALID_TYPE_ID && depth < 256)
    {
      if (curr == type_id)
      {
        return false;  // 偵測到繼承環路，拒絕註冊
      }
      auto it = m_types.find(curr);
      if (it == m_types.end())
      {
        break;
      }
      curr = it->second.parent_type_id;
      ++depth;
    }
  }

  auto it = m_types.find(type_id);
  if (it != m_types.end())
  {
    // 已經註冊過，確保冪等性
    if (!name.empty() && it->second.name.empty())
    {
      it->second.name = std::move(name);
    }
    if (it->second.parent_type_id == ORK_INVALID_TYPE_ID || it->second.parent_type_id == kOuroObjectTypeID)
    {
      it->second.parent_type_id = parent_type_id;
    }
    return true;
  }

  m_types[type_id] = TypeNode{
      .type_id = type_id,
      .name = std::move(name),
      .parent_type_id = parent_type_id
  };
  return true;
}

bool TypeRegistry::IsSubclassOf(ork_type_id_t derived_type, ork_type_id_t base_type) const
{
  if (derived_type == ORK_INVALID_TYPE_ID || base_type == ORK_INVALID_TYPE_ID)
  {
    return false;
  }

  // 自身恆為自身之子型別（反射規範）
  if (derived_type == base_type)
  {
    return true;
  }

  std::shared_lock<std::shared_mutex> lock(m_mutex);

  ork_type_id_t curr = derived_type;
  size_t depth = 0;
  while (curr != ORK_INVALID_TYPE_ID && depth < 256)
  {
    if (curr == base_type)
    {
      return true;
    }

    auto it = m_types.find(curr);
    if (it == m_types.end())
    {
      // 若當前節點尚未在名冊中但 base_type 為 OuroObject，預設所有合法受管型別皆為 OuroObject
      if (base_type == kOuroObjectTypeID)
      {
        return true;
      }
      break;
    }

    curr = it->second.parent_type_id;
    ++depth;
  }

  return false;
}

bool TypeRegistry::GetTypeName(ork_type_id_t type_id, std::string &out_name) const
{
  std::shared_lock<std::shared_mutex> lock(m_mutex);
  auto it = m_types.find(type_id);
  if (it != m_types.end())
  {
    out_name = it->second.name;
    return true;
  }
  return false;
}

size_t TypeRegistry::GetTypeCount() const
{
  std::shared_lock<std::shared_mutex> lock(m_mutex);
  return m_types.size();
}

}  // namespace ork
