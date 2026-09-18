#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"

namespace ork
{

/**
 * @brief Pack an OuroObject's Payload and Edge Roster into any OuroStream.
 * @note Core only packs Payload + Edge Roster (Slot Name -> Target HandleID).
 * Core does NOT dictate physical disk layout or magic numbers.
 */
inline void PackBlueprint(const OuroObject &obj, OuroStream &stream)
{
  // 1. Serialize Pure Payload
  obj.SerializePayload(stream);

  // 2. Automatically traverse m_registered_handles roster to pack Edge Roster
  const auto &handles = obj.GetRegisteredHandles();
  uint32_t edge_count = static_cast<uint32_t>(handles.size());

  stream.WriteBytes(reinterpret_cast<const uint8_t *>(&edge_count), sizeof(edge_count));

  for (const auto &[slot_name, handle_ptr] : handles)
  {
    stream.WriteStringRaw(slot_name);
    const auto &target_ids = handle_ptr->GetTargetIDs();
    uint32_t target_count = static_cast<uint32_t>(target_ids.size());
    stream.WriteBytes(reinterpret_cast<const uint8_t *>(&target_count), sizeof(target_count));
    for (HandleID tid : target_ids)
    {
      stream.WriteBytes(reinterpret_cast<const uint8_t *>(&tid), sizeof(tid));
    }
  }
}

/**
 * @brief Unpack an OuroObject's Payload and Edge Roster from any OuroStream.
 *
 * 具備強例外安全（Strong Exception Safety）與全方位反序列化防禦：
 * 1. 串流長度與邊界檢查（防止截斷串流崩潰）
 * 2. 記憶體爆炸防禦（Sanity Upper Bounds，防止巨量配置與整數溢位 OOM 攻擊）
 * 3. 重複 Slot 槽位 Fail-Fast 防禦（防止同一 slot 重複反序列化引發狀態紊亂）
 * 4. 兩階段套用（Two-Phase Apply）：全數資料驗證與解析成功後才原子套用至 Handle Roster
 */
inline void UnpackBlueprint(OuroObject &obj, OuroStream &stream)
{
  // 1. Deserialize Pure Payload
  obj.DeserializePayload(stream);

  // 2. Deserialize Edge Roster if stream still has remaining bytes
  if (stream.HasRemainingBytes())
  {
    constexpr size_t kMaxEdgeCount = 100000;
    constexpr size_t kMaxTargetsPerSlot = 100000;
    constexpr size_t kMaxSlotNameLength = 1024;

    size_t remaining = stream.GetRemainingBytes();
    if (remaining < sizeof(uint32_t))
    {
      throw OuroCorruptedStreamException("OuroKore UnpackBlueprint Error: Truncated stream before edge_count.");
    }

    uint32_t edge_count = 0;
    stream.ReadBytes(reinterpret_cast<uint8_t *>(&edge_count), sizeof(edge_count));

    // 防禦 1: 記憶體爆炸與整數溢位檢查（每條邊至少需要 slot 長度(4B) + target_count(4B) = 8B）
    if (edge_count > kMaxEdgeCount || static_cast<size_t>(edge_count) * 8 > stream.GetRemainingBytes())
    {
      throw OuroCorruptedStreamException(
          "OuroKore UnpackBlueprint Error: edge_count exceeds stream bounds or maximum limit."
      );
    }

    // 兩階段暫存結構：驗證全部通過後再統一套用，提供強例外安全保證
    std::vector<std::pair<std::string, std::vector<HandleID>>> parsed_edges;
    parsed_edges.reserve(edge_count);
    std::unordered_set<std::string> seen_slots;

    for (uint32_t i = 0; i < edge_count; ++i)
    {
      if (!stream.HasRemainingBytes())
      {
        throw OuroCorruptedStreamException("OuroKore UnpackBlueprint Error: Stream truncated while reading edge roster.");
      }

      std::string slot_name = stream.ReadStringRaw();
      if (slot_name.empty() || slot_name.size() > kMaxSlotNameLength)
      {
        throw OuroCorruptedStreamException("OuroKore UnpackBlueprint Error: Invalid or oversized slot name detected.");
      }

      // 防禦 2: 重複 Slot 槽位 Fail-Fast 防護
      if (!seen_slots.insert(slot_name).second)
      {
        throw OuroDuplicateKeyException(
            "OuroKore UnpackBlueprint Error: Duplicate slot name detected in edge roster: " + slot_name
        );
      }

      if (stream.GetRemainingBytes() < sizeof(uint32_t))
      {
        throw OuroCorruptedStreamException(
            "OuroKore UnpackBlueprint Error: Stream truncated before target_count in slot: " + slot_name
        );
      }

      uint32_t target_count = 0;
      stream.ReadBytes(reinterpret_cast<uint8_t *>(&target_count), sizeof(target_count));

      // 防禦 3: 單一 Slot 的 Target 數量上限與串流長度檢查（防止 OOM 記憶體配置炸彈）
      if (target_count > kMaxTargetsPerSlot ||
          static_cast<size_t>(target_count) * sizeof(HandleID) > stream.GetRemainingBytes())
      {
        throw OuroCorruptedStreamException(
            "OuroKore UnpackBlueprint Error: target_count exceeds stream bounds in slot: " + slot_name
        );
      }

      std::vector<HandleID> tids;
      tids.reserve(target_count);
      for (uint32_t j = 0; j < target_count; ++j)
      {
        HandleID tid = 0;
        stream.ReadBytes(reinterpret_cast<uint8_t *>(&tid), sizeof(tid));
        if (tid != 0)
        {
          tids.push_back(tid);
        }
      }

      parsed_edges.emplace_back(std::move(slot_name), std::move(tids));
    }

    // 第二階段：原子套用（全部驗證通過，安全更新 Handle Roster）
    const auto &handles = obj.GetRegisteredHandles();
    for (const auto &[slot_name, tids] : parsed_edges)
    {
      auto it = handles.find(slot_name);
      if (it != handles.end())
      {
        it->second->ReleaseAll();
        for (HandleID tid : tids)
        {
          it->second->AddTarget(tid);
        }
      }
    }
  }
}

}  // namespace ork
