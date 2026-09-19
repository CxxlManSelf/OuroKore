#pragma once

#include <functional>
#include <memory>

#include "ourokore/c_api/core.h"
#include "ourokore/component/OuroStream.hpp"

struct OuroObject;

namespace ork::detail
{

/**
 * @brief 在核心儲存驅動上建立寫入串流（不洩漏 IStorageDriver 實體給外掛）
 */
ORK_API std::unique_ptr<OuroStream> CreateRuntimeWriteStream(HandleID id);

/**
 * @brief 在核心儲存驅動上開啟讀取串流（不洩漏 IStorageDriver 實體給外掛）
 */
ORK_API std::unique_ptr<OuroStream> OpenRuntimeReadStream(HandleID id);

/**
 * @brief 將任務安全提交至核心背景執行緒池（不洩漏 FixedThreadPool 物件給外掛）
 */
ORK_API void SubmitRuntimeTask(std::function<void()> task);

/**
 * @brief 當 OOM 時由核心內部觸發緊急脫水救援（不洩漏 IAutoDehydrator 給外掛）
 */
ORK_API bool TriggerRuntimeRescue(size_t bytes_needed);

/**
 * @brief 通知核心自動脫水器登記新物件（不洩漏 IAutoDehydrator 給外掛）
 */
ORK_API void NotifyRuntimeObjectRegistered(HandleID id, size_t size_bytes);

/**
 * @brief 通知核心自動脫水器物件已脫水
 */
ORK_API void NotifyRuntimeObjectDehydrated(HandleID id);

/**
 * @brief 通知核心自動脫水器物件已復水
 */
ORK_API void NotifyRuntimeObjectRehydrated(HandleID id);

/**
 * @brief Generic dehydration by HandleID performed natively inside ourokore_core.dll.
 */
ORK_API bool DehydrateRuntime(HandleID id);

/**
 * @brief 在核心預留 HandleID 並建立初始 ControlBlock（物件兩階段建構）
 */
ORK_API HandleID ReserveRuntimeObjectID();

/**
 * @brief 將客戶端建構之物件與 deleter/rehydrator 安全綁定至 ControlBlock
 */
ORK_API bool BindRuntimeObjectPayload(HandleID id,
                                      ::OuroObject* payload,
                                      void (*destroy_fn)(::OuroObject*),
                                      ::OuroObject* (*rehydrate_fn)(HandleID));

/**
 * @brief 取消預留並復位 ControlBlock（建構失敗異常回滾專用）
 */
ORK_API void RollbackRuntimeObjectID(HandleID id);

/**
 * @brief 標記物件的 ControlBlock 儲存狀態為 Clean
 */
ORK_API void MarkRuntimeObjectClean(HandleID id);

}  // namespace ork::detail

