# 02. 5 分鐘快速上手 (Quickstart)

本章節帶領您從零開始建立第一個 OuroKore 應用程式，體驗宿主初始化、自訂領域物件、屬性讀寫與序列化存檔。

---

## 🛠️ 第一步：主程式初始化核心 (Host Entry Point)

在應用程式進入點（如 `main()`）中，透過 `ork::Init()` 取得唯一的 **`HostContext`**：

```cpp
#include <iostream>
#include <ourokore/host/HostContext.hpp>
#include <ourokore/component/builtin/InMemoryStorage.hpp>

int main() {
    // 1. 配置儲存驅動（以記憶體驅動為例）
    auto storage = std::make_shared<ork::InMemoryStorage>();

    // 2. 初始化核心並獲取宿主特權控制物件（RAII 自動管理生命週期）
    ork::HostContext host = ork::Init(storage);
    if (!host.IsValid()) {
        std::cerr << "核心已被其他主程式初始化！" << std::endl;
        return 1;
    }

    std::cout << "OuroKore 核心啟動成功！" << std::endl;

    // 當離開 main() 時，host 解構會自動呼叫 host.Shutdown() 優雅退出
    return 0;
}
```

---

## 📦 第二步：定義自訂領域物件 (Define OuroObject)

所有託管物件必須繼承自 `ork::OuroObject`，禁止外部直接 `new`：

```cpp
#include <ourokore/component/OuroCore.hpp>
#include <string>

class Player : public ork::OuroObject {
public:
    Player() = default;

    // 執行緒安全之屬性存取介面
    std::string GetName() const {
        ork::OuroReadLock lock(*this); // 取得共享讀鎖
        return m_name;
    }

    void SetName(std::string name) {
        ork::OuroWriteLock lock(*this); // 取得獨占寫鎖；解構時自動原子標記 Dirty！
        m_name = std::move(name);
    }

    int32_t GetScore() const {
        ork::OuroReadLock lock(*this);
        return m_score;
    }

    void AddScore(int32_t delta) {
        ork::OuroWriteLock lock(*this);
        m_score += delta;
    }

    // 實作純 Payload 序列化協議
    void SerializePayload(ork::OuroStream &stream) const override {
        stream.WriteProperty("name", m_name);
        stream.WriteProperty("score", m_score);
    }

    void DeserializePayload(ork::OuroStream &stream) override {
        stream.ReadProperty("name", m_name);
        stream.ReadProperty("score", m_score);
    }

private:
    std::string m_name{"Knight"};
    int32_t     m_score{0};
};
```

---

## 🚀 第三步：建立物件與存檔

使用 `ork::CreateObject<T>()` 實例化領域物件，取得棧上保護的 `ork::OuroPtr<T>`：

```cpp
// 1. 建立玩家物件
ork::OuroPtr<Player> player = ork::CreateObject<Player>();
player->SetName("亞瑟王");
player->AddScore(100);

// 2. 存檔至儲存驅動
bool save_ok = ork::Save(player);
assert(save_ok == true);

// 3. 取得物件全域唯一識別碼
ork::HandleID pid = player.GetTargetID();
std::cout << "玩家建立成功，HandleID: " << pid << std::endl;
```
