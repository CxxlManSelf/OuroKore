#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "ourokore/base/ThreadPool.hpp"
#include "ourokore/component/IAutoDehydrator.hpp"
#include "ourokore/component/IStorageDriver.hpp"

namespace ork
{

/**
 * @brief 核心全域執行時上下文管理器（Singleton in ourokore_core.dll）
 *
 * 徹底解決跨 DLL / 模組靜態變數隔離陷阱，確保整個 Process 唯一持有單一
 * StorageDriver、AutoDehydrator 與 ThreadPool，並原子管理全生命週期。
 */
class RuntimeContext
{
public:
  static RuntimeContext &GetInstance();

  bool Initialize(std::shared_ptr<IStorageDriver> driver,
                  std::shared_ptr<IAutoDehydrator> auto_dehydrator = nullptr,
                  std::shared_ptr<ork::base::FixedThreadPool> thread_pool = nullptr);

  void Shutdown();

  void FlushStorage();

  bool Dehydrate(HandleID id);

  void OnObjectDestroyed(HandleID id);

  std::shared_ptr<IStorageDriver> GetStorageDriver() const;
  std::shared_ptr<IAutoDehydrator> GetAutoDehydrator() const;
  std::shared_ptr<ork::base::FixedThreadPool> GetThreadPool() const;

  void SetAutoDehydrator(std::shared_ptr<IAutoDehydrator> dehydrator);
  void SetStorageDriver(std::shared_ptr<IStorageDriver> driver);

  bool IsInitialized() const;
  bool IsShutdownRunning() const;

  void NotifyObjectRegistered(HandleID id, size_t size_bytes);
  void NotifyObjectDehydrated(HandleID id);
  void NotifyObjectRehydrated(HandleID id);
  DehydrationReport TriggerDehydrationRescue(size_t bytes_needed);

  void Reset();

private:
  RuntimeContext() = default;
  ~RuntimeContext() = default;

  RuntimeContext(const RuntimeContext &) = delete;
  RuntimeContext &operator=(const RuntimeContext &) = delete;
  RuntimeContext(RuntimeContext &&) = delete;
  RuntimeContext &operator=(RuntimeContext &&) = delete;

  mutable std::mutex m_mutex;
  std::shared_ptr<IStorageDriver> m_storage_driver{nullptr};
  std::shared_ptr<IAutoDehydrator> m_auto_dehydrator{nullptr};
  std::shared_ptr<ork::base::FixedThreadPool> m_thread_pool{nullptr};
  bool m_is_core_owned_pool{false};
  std::atomic<bool> m_is_shutdown_running{false};
  std::atomic<bool> m_is_initialized{false};
};

}  // namespace ork
