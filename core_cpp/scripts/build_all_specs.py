# -*- coding: utf-8 -*-
"""
一鍵生成全套 specs/ 規範手冊 (build_all_specs.py)
"""
from pathlib import Path
from build_specs_manual import generate_manual_specs
from build_specs_technical import generate_technical_specs
from build_specs_readme import generate_specs_readme

def main():
    script_dir = Path(__file__).resolve().parent
    core_dir = script_dir.parent
    root_dir = core_dir.parent
    specs_dir = root_dir / "specs"

    print("🚀 開始生成 OuroKore 全套手冊與規範文件...")
    generate_manual_specs(specs_dir)
    generate_technical_specs(specs_dir)
    generate_specs_readme(specs_dir)
    print("🎉 全套 specs/ 文件生成成功！路徑:", specs_dir)

if __name__ == "__main__":
    main()
