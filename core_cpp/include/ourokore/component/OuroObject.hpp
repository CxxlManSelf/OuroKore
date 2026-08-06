#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>


namespace ork
{

using HandleID = uint64_t;

// Forward declarations
class OwningContainerHandle;
template <typename T>
class OwningHandle;
template <typename T>
class OuroPtr;

/**
 * @brief Abstract Stream interface for Phase 3 Blueprint Serialization.
 */
class OuroStream
{
public:
  virtual ~OuroStream() = default;
  virtual void WriteBytes(const void *data, size_t size) = 0;
  virtual void ReadBytes(void *dest, size_t size) = 0;
};

/**
 * @brief Base class for all managed objects in OuroKore.
 * All concrete components must inherit from this class.
 */
class OuroObject
{
public:
  virtual ~OuroObject() = default;

  /**
   * @brief Gets the runtime instance identifier of this object.
   */
  HandleID GetObjectID() const { return m_object_id; }

  /**
   * @brief Register an OwningContainerHandle into this object's handle roster.
   * Asserts uniqueness of slot names within the same OuroObject.
   */
  void RegisterHandle(OwningContainerHandle *handle);

  /**
   * @brief Retrieve registered handles roster.
   */
  const std::unordered_map<std::string, OwningContainerHandle *> &GetRegisteredHandles() const
  {
    return m_registered_handles;
  }

  /**
   * @brief Optional Phase 3 Blueprint Serialization interface.
   */
  virtual void Serialize(OuroStream & /*stream*/) const {}
  virtual void Deserialize(OuroStream & /*stream*/) {}

protected:
  OuroObject();

private:
  // Allow core registry/factory to assign the object ID on creation.
  friend class Registry;
  void SetObjectID(HandleID id) { m_object_id = id; }

  HandleID m_object_id = 0;
  std::unordered_map<std::string, OwningContainerHandle *> m_registered_handles;
};

}  // namespace ork
