#pragma once

#include <cstdint>

namespace ork
{

using HandleID = uint64_t;

// Forward declarations
template <typename T>
class OwningHandle;
template <typename T>
class OuroPtr;

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

  /**
   * @brief Parent-driven child object factory.
   */
  template <typename T, typename... Args>
  OwningHandle<T> CreateChild(Args &&...args);

  /**
   * @brief Parent-driven adoption of an existing OuroPtr.
   */
  template <typename T>
  OwningHandle<T> AdoptChild(const OuroPtr<T> &child);

  /**
   * @brief Parent-driven adoption of an existing HandleID.
   */
  template <typename T = OuroObject>
  OwningHandle<T> AdoptChild(HandleID child_id);

protected:
  OuroObject() = default;

private:
  // Allow core registry/factory to assign the object ID on creation.
  friend class Registry;
  void SetObjectID(HandleID id) { m_object_id = id; }

  HandleID m_object_id = 0;
};

}  // namespace ork
