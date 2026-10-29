#pragma once

#include "ourokore/component/Handles.hpp"
#include "ourokore/component/OuroObject.hpp"
#include "ourokore/component/OuroStream.hpp"

namespace ork
{

/**
 * @brief Pack an OuroObject's Payload and Edge Roster into any OuroStream.
 * Delegates pure payload serialization and handle roster packing directly to OuroObject.
 */
inline void PackBlueprint(const OuroObject &obj, OuroStream &stream)
{
  obj.SerializePayload(stream);
  obj.SerializeHandles(stream);
}

/**
 * @brief Unpack an OuroObject's Payload and Edge Roster from any OuroStream.
 * Delegates pure payload deserialization and handle roster unpacking directly to OuroObject.
 */
inline void UnpackBlueprint(OuroObject &obj, OuroStream &stream)
{
  if (!obj.DeserializePayload(stream))
  {
    throw OuroCorruptedStreamException("Payload deserialization failed for object");
  }
  obj.DeserializeHandles(stream);
}

}  // namespace ork
