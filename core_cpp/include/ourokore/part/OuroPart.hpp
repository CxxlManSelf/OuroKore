#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"

namespace ork
{

/**
 * @brief 次世代零件基礎類別 (OuroPart)
 *
 * 繼承自 OuroObject，保有核心靈肉分離、讀寫鎖與圖論引用回收之完整能力。
 * 同時擴充次世代專屬能力：
 * 1. 模組與模具標籤宣告 (GetModuleTag)
 * 2. 次世代標準零件槽位第二套名冊 (m_part_handles)
 * 3. 虛擬 SerializeHandles/DeserializeHandles 責任鏈自動打包私有名冊與次世代名冊
 */
class OuroPart : public OuroObject
{
public:
  virtual ~OuroPart() = default;

  /**
   * @brief 取得此零件對應之次世代模組標籤 (如 "CxxlMan/GoodPlugin/ver1.0" 或 WASM CID)
   */
  virtual std::string_view GetModuleTag() const = 0;

  /**
   * @brief 註冊次世代 PartHandle 到第二套名冊 (m_part_handles)
   */
  void RegisterPartHandle(OwningContainerHandle *handle)
  {
    if (!handle) return;
    const std::string &name = handle->GetSlotName();

    // 1. 第二套名冊防重複
    if (m_part_handles.find(name) != m_part_handles.end())
    {
      throw std::runtime_error("OuroKore Part Error: Duplicate PartHandle Slot Name detected: " + name);
    }
    // 2. 防範與父類第一套私有名冊同名
    if (this->GetRegisteredHandles().find(name) != this->GetRegisteredHandles().end())
    {
      throw std::runtime_error("OuroKore Part Error: PartHandle Slot Name collides with private OwningHandle slot: " + name);
    }
    m_part_handles[name] = handle;
  }

  /**
   * @brief 取得次世代第二套名冊
   */
  const std::unordered_map<std::string, OwningContainerHandle *> &GetPartHandles() const noexcept
  {
    return m_part_handles;
  }

  /**
   * @brief 次世代 handles 序列化：先調用父類打包私有名冊，再打包次世代零件名冊
   */
  void SerializeHandles(OuroStream &stream) const override
  {
    // 1. 父類打包第一套私有名冊 (m_registered_handles)
    OuroObject::SerializeHandles(stream);

    // 2. 打包第二套次世代零件名冊 (m_part_handles)
    uint32_t part_edge_count = static_cast<uint32_t>(m_part_handles.size());
    stream.WriteBytes(reinterpret_cast<const uint8_t *>(&part_edge_count), sizeof(part_edge_count));

    for (const auto &[slot_name, handle_ptr] : m_part_handles)
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
   * @brief 次世代 handles 反序列化：先還原父類私有名冊，再還原次世代零件名冊
   */
  void DeserializeHandles(OuroStream &stream) override
  {
    // 1. 父類還原第一套私有名冊
    OuroObject::DeserializeHandles(stream);

    // 2. 還原第二套次世代零件名冊 (若串流還有剩餘位元組)
    if (!stream.HasRemainingBytes())
    {
      return;
    }

    constexpr size_t kMaxEdgeCount = 100000;
    constexpr size_t kMaxTargetsPerSlot = 100000;
    constexpr size_t kMaxSlotNameLength = 1024;

    size_t remaining = stream.GetRemainingBytes();
    if (remaining < sizeof(uint32_t))
    {
      throw OuroCorruptedStreamException("OuroKore Unpack Part Error: Truncated stream before part_edge_count.");
    }

    uint32_t part_edge_count = 0;
    stream.ReadBytes(reinterpret_cast<uint8_t *>(&part_edge_count), sizeof(part_edge_count));

    if (part_edge_count > kMaxEdgeCount || static_cast<size_t>(part_edge_count) * 8 > stream.GetRemainingBytes())
    {
      throw OuroCorruptedStreamException("OuroKore Unpack Part Error: part_edge_count exceeds stream bounds.");
    }

    std::vector<std::pair<std::string, std::vector<HandleID>>> parsed_edges;
    parsed_edges.reserve(part_edge_count);
    std::unordered_set<std::string> seen_slots;

    for (uint32_t i = 0; i < part_edge_count; ++i)
    {
      if (!stream.HasRemainingBytes())
      {
        throw OuroCorruptedStreamException("OuroKore Unpack Part Error: Stream truncated while reading part roster.");
      }

      std::string slot_name = stream.ReadStringRaw();
      if (slot_name.empty() || slot_name.size() > kMaxSlotNameLength)
      {
        throw OuroCorruptedStreamException("OuroKore Unpack Part Error: Invalid or oversized slot name.");
      }

      if (!seen_slots.insert(slot_name).second)
      {
        throw OuroDuplicateKeyException("OuroKore Unpack Part Error: Duplicate slot name in part roster: " + slot_name);
      }

      if (stream.GetRemainingBytes() < sizeof(uint32_t))
      {
        throw OuroCorruptedStreamException("OuroKore Unpack Part Error: Stream truncated before target_count in part slot: " + slot_name);
      }

      uint32_t target_count = 0;
      stream.ReadBytes(reinterpret_cast<uint8_t *>(&target_count), sizeof(target_count));

      if (target_count > kMaxTargetsPerSlot || static_cast<size_t>(target_count) * sizeof(HandleID) > stream.GetRemainingBytes())
      {
        throw OuroCorruptedStreamException("OuroKore Unpack Part Error: target_count exceeds stream bounds in part slot: " + slot_name);
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

    // 第二階段：原子套用至 m_part_handles
    for (const auto &[slot_name, tids] : parsed_edges)
    {
      auto it = m_part_handles.find(slot_name);
      if (it != m_part_handles.end())
      {
        it->second->ReleaseAll();
        for (HandleID tid : tids)
        {
          it->second->AddTarget(tid);
        }
      }
    }
  }

protected:
  OuroPart() = default;

private:
  std::unordered_map<std::string, OwningContainerHandle *> m_part_handles;
};

} // namespace ork

