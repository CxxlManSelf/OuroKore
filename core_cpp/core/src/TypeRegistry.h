#pragma once

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "ourokore/base/Hash.hpp"
#include "ourokore/c_api/core.h"

namespace ork
{

/// @brief OuroObject 核心基類之預設 TypeID（直接採用 base 通用 Fnv1a64 計算）
constexpr ork_type_id_t kOuroObjectTypeID = ork::base::Fnv1a64("OuroObject");

struct TypeNode
{
  ork_type_id_t type_id{0};
  std::string name;
  ork_type_id_t parent_type_id{0};
};

/**
 * @brief 核心全域型別註冊表（TypeRegistry）
 * 負責維護所有託管元件型別（Component Types）之識別碼、名稱與繼承層級圖。
 * 執行緒安全，支援高併發讀取與無鎖/讀鎖快速繼承判定。
 */
class TypeRegistry
{
public:
  static TypeRegistry &GetInstance();

  TypeRegistry(const TypeRegistry &) = delete;
  TypeRegistry &operator=(const TypeRegistry &) = delete;

  /**
   * @brief 註冊新型別或更新型別繼承關係（冪等保證）
   */
  bool RegisterType(ork_type_id_t type_id, std::string name, ork_type_id_t parent_type_id);

  /**
   * @brief 判定 derived_type 是否為 base_type 或其衍生子型別
   */
  bool IsSubclassOf(ork_type_id_t derived_type, ork_type_id_t base_type) const;

  /**
   * @brief 查詢型別名稱
   */
  bool GetTypeName(ork_type_id_t type_id, std::string &out_name) const;

  /**
   * @brief 取得已註冊之型別總數
   */
  size_t GetTypeCount() const;

private:
  TypeRegistry();
  ~TypeRegistry() = default;

  mutable std::shared_mutex m_mutex;
  std::unordered_map<ork_type_id_t, TypeNode> m_types;
};

}  // namespace ork
