# 04. 併發模型、鎖階層規範與防死鎖設計 (Concurrency & Locks)

## 🚦 1. 單向鎖階層規範 (Lock Ordering Invariants)

在多執行緒併發環境下，OuroKore 嚴格遵循以下單向取鎖順序，違者視為架構級 Bug：

```
[ Registry 全域讀寫鎖 (m_registry_mutex) ]
                  │
                  ▼
   [ ControlBlock 物件獨占鎖 (m_mutex) ]
                  │
                  ▼
       [ 延遲銷毀隊列鎖 (m_queue_mutex) ]
```

### 鐵律防線：
1. **嚴禁反向鎖定**：禁止在持有子物件獨占鎖的情況下，逆向索取父物件的獨占鎖。
2. **生命週期解構無鎖調用**：在調用使用者定義的 `~OuroObject()` 解構函式期間，絕對不持有任何全域註冊表互斥鎖，防範巢狀解構引發死鎖。

---

## 🧵 2. FixedThreadPool 與 DynamicThreadPool 防自我死鎖

* **Worker 執行緒自我辨識**：工作執行緒內部標記 `thread_local bool is_worker_thread`。
* **`wait_idle()` 防呆保護**：若工作執行緒在自身任務中嘗試呼叫 `wait_idle()`，內部立即判定為潛在自我死鎖並 Fail-Fast，徹底保護系統穩定。
