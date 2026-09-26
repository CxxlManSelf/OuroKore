# -*- coding: utf-8 -*-
"""
OuroKore 規格手冊與程式碼一致性自動自檢腳本 (verify_specs.py)
用於驗證 C API 導出符號、核心組件類別是否 100% 完整收錄於 specs/ 與 AI Skill 中。
"""
import re
import sys
from pathlib import Path

def get_exported_c_apis(include_dir: Path):
    c_apis = set()
    pattern = re.compile(r"ORK_API\s+[^\(\)]+?\s+ORK_CALL\s+(ork_\w+)\s*\(")
    
    c_api_dir = include_dir / "ourokore" / "c_api"
    for header in c_api_dir.glob("*.h"):
        content = header.read_text(encoding="utf-8")
        matches = pattern.findall(content)
        for fn in matches:
            c_apis.add(fn)
    return c_apis

def check_c_abi_specs(specs_dir: Path, expected_apis: set):
    target_file = specs_dir / "technical" / "03_c_abi_and_memory.md"
    if not target_file.exists():
        return False, [f"Missing file: {target_file}"]
    
    content = target_file.read_text(encoding="utf-8")
    missing = []
    for fn in expected_apis:
        if fn not in content:
            missing.append(fn)
    return len(missing) == 0, missing

def check_manual_api_coverage(specs_dir: Path):
    target_file = specs_dir / "manual" / "07_api_reference.md"
    if not target_file.exists():
        return False, [f"Missing file: {target_file}"]
    
    content = target_file.read_text(encoding="utf-8")
    essential_types = [
        "HostContext",
        "OuroPtr",
        "OwningHandle",
        "UnboundHandle",
        "OuroObject",
        "OuroStream",
        "OuroWriteLock"
    ]
    missing = [t for t in essential_types if t not in content]
    return len(missing) == 0, missing

def check_ai_skill_integrity(core_dir: Path):
    skill_file = core_dir / ".agents" / "skills" / "ourokore" / "SKILL.md"
    if not skill_file.exists():
        return False, ["Skill file missing: .agents/skills/ourokore/SKILL.md"]
    
    content = skill_file.read_text(encoding="utf-8")
    keywords = ["name: ourokore", "心智模型", "Dehydration", "Blueprint", "HostContext", "Mandatory Sync Protocol"]
    missing = [k for k in keywords if k not in content]
    return len(missing) == 0, missing

def check_app_skill_integrity(root_dir: Path):
    skill_file = root_dir / "skills" / "ourokore-app" / "SKILL.md"
    if not skill_file.exists():
        return False, ["App skill file missing: skills/ourokore-app/SKILL.md"]
    
    content = skill_file.read_text(encoding="utf-8")
    keywords = [
        "name: ourokore-app",
        "OuroObject",
        "OuroPtr",
        "OwningHandle",
        "UnboundHandle",
        "SerializePayload",
        "DeserializePayload",
        "OuroWriteLock"
    ]
    missing = [k for k in keywords if k not in content]
    return len(missing) == 0, missing

def main():
    script_dir = Path(__file__).resolve().parent
    core_dir = script_dir.parent
    root_dir = core_dir.parent
    specs_dir = root_dir / "specs"
    include_dir = core_dir / "include"

    print("=" * 60)
    print("🔍 開始執行 OuroKore 規格與程式碼一致性檢驗...")
    print("=" * 60)

    has_error = False

    # 1. 檢驗 C API 導出涵蓋度
    exported_apis = get_exported_c_apis(include_dir)
    print(f"📦 掃描到核心 C API 導出函式共 {len(exported_apis)} 個")
    ok, missing_apis = check_c_abi_specs(specs_dir, exported_apis)
    if ok:
        print("  ✅ [PASS] 所有 C API 導出符號均已完整收錄於 technical/03_c_abi_and_memory.md")
    else:
        print(f"  ❌ [FAIL] 發現未記錄於技術規範的 C API: {missing_apis}")
        has_error = True

    # 2. 檢驗使用說明書核心類別覆蓋率
    ok, missing_types = check_manual_api_coverage(specs_dir)
    if ok:
        print("  ✅ [PASS] 核心受管類別均已完整收錄於 manual/07_api_reference.md")
    else:
        print(f"  ❌ [FAIL] 說明書 API 手冊缺失關鍵類別: {missing_types}")
        has_error = True

    # 3. 檢驗 AI 核心維護技能完整性
    ok, missing_skill_parts = check_ai_skill_integrity(core_dir)
    if ok:
        print("  ✅ [PASS] AI 核心技能檔 .agents/skills/ourokore/SKILL.md 完整且包含同步協議")
    else:
        print(f"  ❌ [FAIL] AI 核心技能檔缺失關鍵段落: {missing_skill_parts}")
        has_error = True

    # 4. 檢驗應用端開發者 AI 技能完整性
    ok, missing_app_parts = check_app_skill_integrity(root_dir)
    if ok:
        print("  ✅ [PASS] 應用端 AI 技能檔 ../skills/ourokore-app/SKILL.md 結構完整且範本完備")
    else:
        print(f"  ❌ [FAIL] 應用端 AI 技能檔缺失關鍵段落: {missing_app_parts}")
        has_error = True

    print("=" * 60)
    if has_error:
        print("🚨 檢驗未通過！請依據上方缺失更新 specs/ 或 skill 文件。")
        sys.exit(1)
    else:
        print("🎉 恭喜！所有規格手冊、AI 技能與核心程式碼 100% 保持同步一致！")
        sys.exit(0)

if __name__ == "__main__":
    main()
