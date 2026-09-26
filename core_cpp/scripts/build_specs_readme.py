# -*- coding: utf-8 -*-
"""
生成 specs/README.md 導覽總目錄
"""
from pathlib import Path

def generate_specs_readme(specs_dir: Path):
    readme_file = specs_dir / "README.md"
    readme_file.write_text('''# OuroKore 規格手冊體系 (Specifications & Manuals)

歡迎查閱 **OuroKore** 核心系統之官方手冊與規格文件庫。

---

## 📚 1. 使用者說明書 (Developer Manual)

專為使用 OuroKore 框架開發應用系統（如遊戲邏輯、大規模世界編輯器、高效能後端圖託管）的軟體工程師設計：

1. [01. 系統概述與架構哲學](manual/01_introduction.md) - 心智模型、四大基石與三層邊界隔離哲學
2. [02. 5 分鐘快速上手](manual/02_quickstart.md) - 宿主初始化、自訂領域物件、屬性存取與存檔
3. [03. 領域物件設計規範](manual/03_domain_object_design.md) - Getter/Setter、OuroReadLock/OuroWriteLock、原子標髒
4. [04. Handle 拓撲管理系統](manual/04_handles_and_topology.md) - OwningHandle、WeakHandle、OwningContainerHandle、OuroPtr
5. [05. 自動換頁脫水與儲存驅動](manual/05_dehydration_and_storage.md) - 記憶體脫水、透明按需復水、LRU 策略配置
6. [06. 宿主生命週期與特權管理](manual/06_host_lifecycle.md) - HostContext 獨佔特權、插件隔離防護、優雅退出
7. [07. 公開 C++ API 參照手冊](manual/07_api_reference.md) - 完整公開 API 清單與核心型別定義

---

## 🛠️ 2. 底層技術架構手冊 (Technical Specifications)

專為 OuroKore C++ 核心維護者、多語言 FFI 介面開發者與架構審查者設計：

1. [01. 系統架構設計規範](technical/01_system_architecture.md) - 三層隔離設計、圖論演算法、循環參照 GC 原理
2. [02. 二進位串流與藍圖打包協議](technical/02_binary_protocols.md) - 純 Payload 與拓撲分離、防記憶體爆炸與防重複鍵
3. [03. 純 C ABI 規格與記憶體佈局規範](technical/03_c_abi_and_memory.md) - 32 個純 C 導出函式規格、CRT 隔離、ControlBlock
4. [04. 併發模型、鎖階層規範與防死鎖設計](technical/04_concurrency_and_locks.md) - 單向鎖順序、執行緒池防自我死鎖機制

---

## 🔍 3. 規格一致性驗證

本手冊體系受 `core_cpp/scripts/verify_specs.py` 自動化自檢腳本約束，確保核心 C ABI、受管類別與手冊 100% 保持同步。
''', encoding="utf-8")
    print("✅ specs/README.md 導覽文件生成完畢！")

if __name__ == "__main__":
    import sys
    specs_dir = Path(__file__).resolve().parent.parent.parent / "specs"
    generate_specs_readme(specs_dir)
