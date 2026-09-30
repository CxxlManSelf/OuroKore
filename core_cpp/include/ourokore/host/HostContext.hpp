#pragma once

#include <memory>
#include <stdexcept>
#include <utility>

#include "ourokore/base/ThreadPool.hpp"
#include "ourokore/c_api/host_api.h"
#include "ourokore/component/IAutoDehydrator.hpp"
#include "ourokore/component/IStorageDriver.hpp"
#include "ourokore/component/Types.hpp"
#include "ourokore/host/HostRuntimeAPI.hpp"
#include "ourokore/host/IObjectModuleBinder.hpp"

namespace ork
{

namespace detail
{
class ObjectModuleBinderImpl : public IObjectModuleBinder
{
public:
  void SetObjectModuleLoader(HandleID id, const ork::DynamicLibrary &loader) override
  {
    if (!detail::SetRuntimeObjectModuleLoader(id, loader))
    {
      throw std::runtime_error("OuroKore Host Error: Failed to bind DynamicLibrary loader to target object.");
    }
  }

  ork::DynamicLibrary GetObjectModuleLoader(HandleID id) const override
  {
    return detail::GetRuntimeObjectModuleLoader(id);
  }
};
}  // namespace detail

/**
 * @brief OuroKore 宿主控制物件（HostContext）
 *
 * 【特權管理與第三方插件隔離設計】
 * 唯有主程式（Host Application）透過 ork::Init() 成功初始化核心時，才能獲取有效之 HostContext。
 * 所有可能破壞系統穩定度、篡改進程組態或關閉核心的特權管理功能（如生命週期終止、磁碟排空、
 * 脫水策略設置、循環回收、延遲銷毀模式設定等）皆統一收斂於此物件中。
 *
 * 第三方外掛插件無法在全域命名空間中接觸到這些功能；且若外掛嘗試呼叫 ork::Init()，
 * 核心將拒絕請求並回傳無效之 HostContext，徹底防止越權行為。
 *
 * 本物件具備 Move-only 語意與 RAII 生命週期管理，解構時會自動安全執行優雅終止（Shutdown）。
 */
class HostContext : public IObjectModuleBinder
{
public:
  // 僅供 ork::Init() 內部建構，外部不可隨意自行偽造主控權限
  explicit HostContext(bool is_owner = false) noexcept : m_is_owner(is_owner) {}

  ~HostContext() noexcept
  {
    if (m_is_owner)
    {
      try
      {
        Shutdown();
      }
      catch (...)
      {
        // 吸收所有例外，防範解構期間例外逃逸引發 std::terminate()
      }
    }
  }

  // 唯一主控權：禁止複製
  HostContext(const HostContext &) = delete;
  HostContext &operator=(const HostContext &) = delete;

  // 支援移動語意（轉移主程式管理與安全關閉責任）
  HostContext(HostContext &&other) noexcept : m_is_owner(other.m_is_owner)
  {
    other.m_is_owner = false;
  }

  HostContext &operator=(HostContext &&other) noexcept
  {
    if (this != &other)
    {
      if (m_is_owner)
      {
        try
        {
          Shutdown();
        }
        catch (...)
        {
          // 吸收所有例外，防範移動賦值期間例外逃逸違反 noexcept 保證
        }
      }
      m_is_owner = other.m_is_owner;
      other.m_is_owner = false;
    }
    return *this;
  }

  /**
   * @brief 檢查當前 HostContext 是否為合法且成功初始化的主程式擁有者
   */
  bool IsValid() const noexcept
  {
    return m_is_owner;
  }

  explicit operator bool() const noexcept
  {
    return m_is_owner;
  }

  /**
   * @brief 提供指針風格存取語法糖 (host->FlushStorage())
   */
  HostContext *operator->() noexcept
  {
    return this;
  }

  const HostContext *operator->() const noexcept
  {
    return this;
  }

  // =========================================================================
  // --- 主程式專屬特權管理方法 (Host-Only Privileged Management Methods) ---
  // =========================================================================

  /**
   * @brief 優雅終止核心執行緒池與背景任務，確保退出時無死鎖與資料遺失
   */
  void Shutdown()
  {
    CheckOwner();
    ork_shutdown_runtime();
    m_is_owner = false;
  }

  /**
   * @brief 等待所有排隊中的背景儲存與銷毀任務完全落盤排空 (Flush)
   */
  void FlushStorage()
  {
    CheckOwner();
    ork_flush_storage();
  }

  /**
   * @brief 同步排空並等待目前背景佇列中的所有延遲銷毀任務完成
   */
  void FlushDeferredDeletions()
  {
    CheckOwner();
    ork_flush_deferred_deletions();
  }

  /**
   * @brief 設定延遲銷毀模式（sync: 同步即時執行；async: 背景執行緒池）
   */
  void SetDeferredDeleteMode(bool sync)
  {
    CheckOwner();
    ork_set_deferred_delete_mode(sync ? ORK_DEFERRED_DELETE_SYNC : ORK_DEFERRED_DELETE_ASYNC);
  }

  /**
   * @brief 觸發同步執行一輪循環參照收集判定
   */
  void CollectCycles()
  {
    CheckOwner();
    ork_collect_cycles();
  }

  /**
   * @brief 取得當前循環收集器排隊中等待檢查的嫌疑犯數量
   */
  size_t GetCycleSuspectCount() const
  {
    CheckOwner();
    return static_cast<size_t>(ork_get_cycle_suspect_count());
  }

  /**
   * @brief 設定當前註冊的自動脫水外掛模組
   */
  void SetAutoDehydrator(std::shared_ptr<IAutoDehydrator> dehydrator)
  {
    CheckOwner();
    detail::SetRuntimeAutoDehydrator(std::move(dehydrator));
  }

  /**
   * @brief 取得當前註冊的自動脫水模組
   */
  std::shared_ptr<IAutoDehydrator> GetAutoDehydrator() const
  {
    CheckOwner();
    return detail::GetRuntimeAutoDehydrator();
  }

  /**
   * @brief 設定底層持久化儲存驅動
   */
  void SetStorageDriver(std::shared_ptr<IStorageDriver> driver)
  {
    CheckOwner();
    detail::SetRuntimeStorageDriver(std::move(driver));
  }

  /**
   * @brief 取得底層持久化儲存驅動
   */
  std::shared_ptr<IStorageDriver> GetStorageDriver() const
  {
    CheckOwner();
    return detail::GetRuntimeStorageDriver();
  }

  /**
   * @brief 取得核心執行緒池
   */
  std::shared_ptr<ork::base::FixedThreadPool> GetThreadPool() const
  {
    CheckOwner();
    return detail::GetRuntimeThreadPool();
  }

  /**
   * @brief 設定核心全域物件銷毀監聽回呼（主程式常駐監控，杜絕插件懸空回呼）
   */
  void SetObjectDestroyedCallback(void (*callback)(HandleID id))
  {
    CheckOwner();
    ork_set_object_destroyed_callback(callback);
  }

  /**
   * @brief 取得目前排隊等待物理銷毀之任務數量（Host 監控與效能調度）
   */
  uint64_t GetDeferredDeletePendingCount() const
  {
    CheckOwner();
    return ork_get_deferred_delete_pending_count();
  }

  /**
   * @brief 復位核心初始化狀態（支援軟重啟與測試套件切換）
   */
  void Reset()
  {
    CheckOwner();
    try
    {
      Shutdown();
    }
    catch (...)
    {
    }
    m_is_owner = false;
  }

  /**
   * @brief 觸發全域緊急脫水救援以釋放記憶體（Host 專用記憶體調度）
   * @param bytes_needed 欲騰出的記憶體字節數
   * @return 實際釋放的字節數
   */
  size_t TriggerDehydrationRescue(size_t bytes_needed)
  {
    CheckOwner();
    size_t freed = 0;
    int32_t has_more = 0;
    ork_trigger_dehydration_rescue(bytes_needed, &freed, &has_more);
    return freed;
  }

  /**
   * @brief 綁定模組載入器（DynamicLibrary）至受管物件之控制區塊（宿主特權）
   *
   * 【生命週期反向錨定鐵律】
   * 只要該物件之實體存活或處於脫水 Dehydrated 狀態，該動態庫模組即保證不被卸載，
   * 確保未來透明復水時之 RehydrateCallback 與銷毀時之 DestroyFn 代碼段絕對有效。
   * 當物件實體 (Payload) 於 DeferredDeleteQueue 完成物理銷毀後，核心會立即釋放該 DynamicLibrary 引用
   * （若引用歸零則底層自動安全觸發 FreeLibrary/dlclose），即使 ControlBlock 作為墓碑長存（供弱引用查詢），
   * 亦絕不阻礙動態模組的正常卸載。
   *
   * @param id 目標受管物件 HandleID
   * @param loader 動態庫載入器實例
   */
  void SetObjectModuleLoader(HandleID id, const ork::DynamicLibrary &loader) override
  {
    CheckOwner();
    if (!detail::SetRuntimeObjectModuleLoader(id, loader))
    {
      throw std::runtime_error("OuroKore Host Error: Failed to bind DynamicLibrary loader to target object.");
    }
  }

  /**
   * @brief 取得目標物件綁定之模組載入器（宿主特權）
   * @param id 目標受管物件 HandleID
   * @return 若有綁定傳回 DynamicLibrary 實例；若未綁定或物件不存在傳回空實例
   */
  ork::DynamicLibrary GetObjectModuleLoader(HandleID id) const override
  {
    CheckOwner();
    return detail::GetRuntimeObjectModuleLoader(id);
  }

  /**
   * @brief 取得專用模組綁定介面（最小特權原則，Interface Segregation Principle）
   *
   * 允許宿主將此專用介面安全轉交給負責動態庫載入的專職單元（如 PluginManager、ComponentFactory），
   * 賦予其為受管物件綁定動態庫以錨定生命週期的能力，同時杜絕整個 HostContext 外洩所帶來的停機、GC 等破壞性特權風險。
   *
   * @return 具備模組載入器綁定能力之介面指標 std::shared_ptr<IObjectModuleBinder>
   */
  std::shared_ptr<IObjectModuleBinder> GetModuleBinder() const
  {
    CheckOwner();
    return std::make_shared<detail::ObjectModuleBinderImpl>();
  }

  // =========================================================================
  // --- 白盒測試與故障模擬專用方法 (White-Box Testing & Fault Simulation) ---
  // =========================================================================

  /**
   * @brief 白盒測試專用：手動設置特定物件之儲存狀態 (StorageState)
   */
  void SetStorageState(HandleID id, StorageState state)
  {
    CheckOwner();
    ork_set_storage_state_for_testing(id, static_cast<uint8_t>(state));
  }

  /**
   * @brief 白盒測試專用：清空特定物件之記憶體 Payload（模擬脫水後記憶體卸載狀態）
   */
  void ClearObjectPayloadForTesting(HandleID id)
  {
    CheckOwner();
    ork_clear_object_payload_for_testing(id);
  }

private:
  void CheckOwner() const
  {
    if (!m_is_owner)
    {
      throw std::runtime_error("OuroKore Host Error: Unauthorized or invalid HostContext operation. "
                               "Privileged core management functions are strictly reserved for the primary host.");
    }
  }

  bool m_is_owner{false};
};

/**
 * @brief Initialize OuroKore Core with persistent storage driver, optional auto-dehydrator plugin and optional thread pool.
 * @note One-Way Immutable: Only the primary host application's first call takes effect.
 * Subsequent calls from plugins or other modules are safely rejected and return an invalid HostContext.
 * @return HostContext privileged host control object if successfully initialized, or invalid HostContext if core has already been initialized.
 */
inline HostContext Init(std::shared_ptr<IStorageDriver> driver = nullptr,
                        std::shared_ptr<IAutoDehydrator> auto_dehydrator = nullptr,
                        std::shared_ptr<ork::base::FixedThreadPool> thread_pool = nullptr)
{
  bool success = detail::InitializeRuntime(std::move(driver), std::move(auto_dehydrator), std::move(thread_pool));
  return HostContext(success);
}

// 向後相容別名：使原本引用 OuroCoreScope 的主程式代碼能平滑過渡
using OuroCoreScope = HostContext;

}  // namespace ork
