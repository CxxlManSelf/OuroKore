# 02. 二進位串流與藍圖打包協議 (Binary Protocols)

## 📜 1. 藍圖佈局與純 Payload 分離原則

OuroKore 藍圖打包嚴格禁止將物件內部私有資料與拓撲邊緣混雜在一起：
1. **純資料區 (Pure Payload)**：透過 `OuroStream::WriteProperty` 寫入具備型別校驗與 Key-Value 結構的單純數值。
2. **邊緣名冊區 (Edge Roster)**：框架自動遍歷物件的 `m_registered_handles`，依序寫入：
   * `Slot Name (String)` -> `Target Count (uint32_t)` -> `Target HandleIDs (uint64_t...)`。

---

## 🛡️ 2. 防禦性邊界檢查與兩階段反序列化 (Two-Phase Apply)

反序列化外部資料串流時，嚴防各類惡意攻擊與記憶體損毀：
1. **重複鍵阻斷 (Fail-Fast Duplicate Key Guard)**：若藍圖串流中存在重複屬性名稱，立即拋出 `OuroDuplicateKeyException`。
2. **記憶體爆炸防禦 (Sanity Upper Bounds)**：插槽數量與目標 Handle 數量嚴格受上限約束（如單一插槽上限 1,000,000），防止惡意構造的巨大數字耗盡記憶體。
3. **兩階段驗證套用**：反序列化時先在棧上或暫存區驗證整體資料完整性，無例外發生後才原子更新物件內部狀態。
