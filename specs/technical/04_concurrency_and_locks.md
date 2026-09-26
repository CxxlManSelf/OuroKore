# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks RFC)

本文件定義 OuroKore 系統中多執行緒併發存取、全域單向鎖偏序與任務排程器的防死鎖規範。

---

## 🚦 1. 全域單向鎖偏序規範 (Lock Partial Ordering Invariants)

為杜絕跨執行緒併發存取引發的循環等待死鎖，任何語言實現之 OuroKore 核心必須嚴格遵守以下單向鎖順序：

```
[ 層級 1：全域註冊表讀寫鎖 (Registry Mutex) ]
                     │
                     ▼
  [ 層級 2：ControlBlock 物件互斥鎖 (Object Mutex) ]
                     │
                     ▼
   [ 層級 3：佇列與排程器互斥鎖 (Queue / Worker Mutex) ]
```

### 鐵律防線：
1. **嚴禁逆向取鎖**：禁止在持有層級 2（物件鎖）的情況下，嘗試索取層級 1（全域註冊表鎖）；禁止在持有子物件獨占鎖的情況下，逆向索取父物件的獨占鎖。
2. **領域物件終結器無鎖調用防線 (Finalizer Outside Locks)**：
   - 核心在調用領域物件的釋放/終結邏輯（Destructor / Finalizer / Dispose）或執行釋放回呼期間，**絕對不可持有任何全域註冊表鎖或佇列鎖**。
   - 目的：防範物件在終結邏輯中遞迴存取其他物件或觸發連鎖清理而造成自死鎖。

---

## 🧵 2. 執行緒池 Worker 辨識與防遞迴等待死鎖

在高效能非同步執行緒池（`FixedThreadPool` / `DynamicThreadPool`）中：
1. **工作執行緒識別標記**：每個被執行緒池管理的工作線程必須具備執行緒在地標記（Thread-Local Flag: `IsWorker = True`）。
2. **等待排空防呆保護 (Wait Idle Invariant)**：
   - 若某個 Worker 執行緒在其承擔的任務內部呼叫了等待整個執行緒池排空（`WaitIdle`）之操作，內部必須立即判定此為自我死鎖行為並主動 Fail-Fast 拋出異常，杜絕死鎖擴散至整個進程。
