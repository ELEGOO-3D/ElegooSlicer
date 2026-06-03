#!/usr/bin/env python3
import sys
import shutil
from pathlib import Path

def setup_rules(workspace, ide_type, lang='zh-CN'):
    """Setup IDE-specific rules"""
    source_dir = workspace / 'doc' / 'rules' / lang / ide_type
    
    if not source_dir.exists():
        print(f'[ERROR] source directory not found: {source_dir}')
        return False
    
    rule_files = list(source_dir.glob('*'))
    if not rule_files:
        print(f'[WARNING] no rule files found in {source_dir}')
        return True
    
    success = True
    copied_count = 0
    
    if ide_type == 'cursor':
        target_dir = workspace / '.cursor' / 'rules'
        target_dir.mkdir(parents=True, exist_ok=True)

        for source_file in rule_files:
            if source_file.is_file():
                target_file = target_dir / source_file.name
                try:
                    shutil.copy2(source_file, target_file)
                    copied_count += 1
                except Exception as e:
                    print(f'[WARNING] failed to copy {source_file.name}: {e}')
                    success = False

        if copied_count > 0:
            print(f'[OK] copied {copied_count} rule file(s) for Cursor')
            print(f'     target: {target_dir}')
    
    elif ide_type == 'vscode':
        github_dir = workspace / '.github'
        github_dir.mkdir(parents=True, exist_ok=True)
        
        copilot_file = source_dir / 'copilot-instructions.md'
        if copilot_file.exists():
            try:
                shutil.copy2(copilot_file, github_dir / 'copilot-instructions.md')
                copied_count += 1
                print(f'[OK] copied copilot-instructions.md to .github/')
            except Exception as e:
                print(f'[WARNING] failed to copy copilot-instructions.md: {e}')
                success = False
        
        rules_dir = workspace / '.vscode' / 'rules'
        rules_dir.mkdir(parents=True, exist_ok=True)
        
        for source_file in rule_files:
            if source_file.is_file() and source_file.name != 'copilot-instructions.md':
                target_file = rules_dir / source_file.name
                try:
                    shutil.copy2(source_file, target_file)
                    copied_count += 1
                except Exception as e:
                    print(f'[WARNING] failed to copy {source_file.name}: {e}')
                    success = False
        
        if copied_count > 1:
            print(f'[OK] copied {copied_count - 1} reference file(s) to .vscode/rules/')
    
    elif ide_type == 'claude':
        target_dir = workspace
        target_dir.mkdir(parents=True, exist_ok=True)

        for source_file in rule_files:
            if source_file.is_file():
                target_file = target_dir / source_file.name
                try:
                    shutil.copy2(source_file, target_file)
                    copied_count += 1
                except Exception as e:
                    print(f'[WARNING] failed to copy {source_file.name}: {e}')
                    success = False

        if copied_count > 0:
            print(f'[OK] copied {copied_count} rule file(s) for Claude')
            print(f'     target: {target_dir}')
    
    else:
        print(f'[ERROR] unknown IDE type: {ide_type}')
        return False
    
    return success

def main():
    if len(sys.argv) < 2 or len(sys.argv) > 4:
        print('usage: setup_rules.py <ide_type> [lang] [workspace_path]')
        print('       ide_type: vscode | cursor | claude')
        print('       lang: en | cn (default: en)')
        print('       workspace_path: optional, defaults to current directory')
        print('')
        print('examples:')
        print('  setup_rules.py cursor')
        print('  setup_rules.py cursor cn')
        print('  setup_rules.py vscode en')
        sys.exit(1)
    
    ide_type = sys.argv[1].lower()
    
    lang = 'en'
    workspace = Path.cwd()
    
    if len(sys.argv) >= 3:
        if sys.argv[2] in ['cn', 'en']:
            lang = 'zh-CN' if sys.argv[2] == 'cn' else 'en'
            if len(sys.argv) == 4:
                workspace = Path(sys.argv[3])
        else:
            workspace = Path(sys.argv[2])
    
    if ide_type not in ['vscode', 'cursor', 'claude']:
        print(f'[ERROR] invalid IDE type: {ide_type}')
        print('        supported: vscode, cursor, claude')
        sys.exit(1)
    
    if lang not in ['zh-CN', 'en']:
        print(f'[ERROR] invalid language: {lang}')
        print('        supported: en, cn')
        sys.exit(1)
    
    if not workspace.exists():
        print(f'[ERROR] workspace not found: {workspace}')
        sys.exit(1)
    
    success = setup_rules(workspace, ide_type, lang)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()

