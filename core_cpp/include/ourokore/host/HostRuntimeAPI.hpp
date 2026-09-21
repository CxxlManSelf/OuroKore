#pragma once

#include <memory>

#include "ourokore/base/ThreadPool.hpp"
#include "ourokore/c_api/core.h"
#include "ourokore/c_api/host_api.h"
#include "ourokore/component/IAutoDehydrator.hpp"
#include "ourokore/component/IStorageDriver.hpp"

namespace ork::detail
{

/**
 * @brief Initialize OuroKore Core Runtime with persistent storage driver, dehydrator, and thread pool.
 * Exported from ourokore_core.dll (Single Process-Wide Instance).
 */
ORK_API bool InitializeRuntime(std::shared_ptr<IStorageDriver> driver,
                               std::shared_ptr<IAutoDehydrator> auto_dehydrator = nullptr,
                               std::shared_ptr<ork::base::FixedThreadPool> thread_pool = nullptr);

/**
 * @brief Gracefully terminate runtime context, thread pools, and cyclic collector.
 */
inline void ShutdownRuntime()
{
  ork_shutdown_runtime();
}

/**
 * @brief Synchronously flush deferred deletions and storage worker queue.
 */
inline void FlushStorageRuntime()
{
  ork_flush_storage();
}

/**
 * @brief Set the process-wide auto-dehydrator instance.
 */
ORK_API void SetRuntimeAutoDehydrator(std::shared_ptr<IAutoDehydrator> dehydrator);

/**
 * @brief Set the process-wide storage driver instance.
 */
ORK_API void SetRuntimeStorageDriver(std::shared_ptr<IStorageDriver> driver);

/**
 * @brief Get the process-wide storage driver instance (Host-only access).
 */
ORK_API std::shared_ptr<IStorageDriver> GetRuntimeStorageDriver();

/**
 * @brief Get the process-wide auto-dehydrator instance (Host-only access).
 */
ORK_API std::shared_ptr<IAutoDehydrator> GetRuntimeAutoDehydrator();

/**
 * @brief Get the process-wide core thread pool instance (Host-only access).
 */
ORK_API std::shared_ptr<ork::base::FixedThreadPool> GetRuntimeThreadPool();

}  // namespace ork::detail
