#pragma once

#include <cstdint>

namespace ork
{

using HandleID = uint64_t;

/**
 * @brief Base class for all managed objects in OuroKore.
 * All concrete components must inherit from this class and implement GetTypeID().
 */
class OuroObject
{
public:
  virtual ~OuroObject() = default;

  /**
   * @brief Gets the unique runtime type identifier of this object.
   * Required for RTTI and serialization.
   */
  virtual uint64_t GetTypeID() const = 0;

  /**
   * @brief Gets the runtime instance identifier of this object.
   */
  HandleID GetObjectID() const { return m_object_id; }

protected:
  OuroObject() = default;

private:
  // Allow core registry/factory to assign the object ID on creation.
  friend class Registry;
  void SetObjectID(HandleID id) { m_object_id = id; }

  HandleID m_object_id = 0;
};

}  // namespace ork
