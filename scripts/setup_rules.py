#!/usr/bin/env python3
"""Install AI rule files from doc/rules/ templates to agent target locations."""

import sys
import shutil
from pathlib import Path

ALL_AGENTS = ['claude', 'cursor', 'copilot', 'codex']

def copy_file(src, dst, label=''):
    """Copy a single file, print result."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    try:
        shutil.copy2(src, dst)
        print(f'  [OK] {label or src.name} → {dst}')
        return True
    except Exception as e:
        print(f'  [WARN] {src.name}: {e}')
        return False

def install_agents(workspace, agents, lang):
    """Install rules for the given agents and language."""
    rules_dir = workspace / 'doc' / 'rules' / lang
    if not rules_dir.exists():
        print(f'[ERROR] rules directory not found: {rules_dir}')
        return False

    lang_label = '中文' if lang == 'zh-CN' else 'English'
    print(f'\n=== 安装规则 [{lang_label}] ===\n')

    ok = True

    # 1. Always install AGENTS.md (shared by all agents)
    agents_src = rules_dir / 'AGENTS.md'
    if agents_src.exists():
        if not copy_file(agents_src, workspace / 'AGENTS.md', 'AGENTS.md'):
            ok = False
    else:
        print(f'  [WARN] AGENTS.md not found in {lang}')

    # 2. Always install .ai_rules/ (on-demand rules)
    ai_src = rules_dir / '.ai_rules'
    if ai_src.exists():
        ai_dst = workspace / '.ai_rules'
        for f in ai_src.glob('*'):
            if not copy_file(f, ai_dst / f.name, f'.ai_rules/{f.name}'):
                ok = False

    # 3. Install per-agent entry points
    for agent in agents:
        if agent not in ALL_AGENTS:
            print(f'  [ERROR] unknown agent: {agent}')
            ok = False
            continue

        if agent == 'claude':
            src = rules_dir / 'claude' / 'CLAUDE.md'
            if src.exists():
                if not copy_file(src, workspace / 'CLAUDE.md', 'CLAUDE.md'):
                    ok = False

        elif agent == 'cursor':
            src = rules_dir / 'cursor' / 'cursor.mdc'
            if src.exists():
                if not copy_file(src, workspace / '.cursor' / 'rules' / 'cursor.mdc', '.cursor/rules/cursor.mdc'):
                    ok = False

        elif agent == 'copilot':
            src = rules_dir / 'copilot' / 'copilot-instructions.md'
            if src.exists():
                if not copy_file(src, workspace / '.github' / 'copilot-instructions.md', '.github/copilot-instructions.md'):
                    ok = False

    print(f'\n=== 安装完成 ===')
    return ok

def main():
    if len(sys.argv) < 2:
        print('用法: setup_rules.py <agent...> [lang]')
        print('')
        print('  agent:  claude | cursor | copilot | codex | all（可多个，空格分隔）')
        print('  lang:   en（默认）| cn')
        print('')
        print('示例:')
        print('  setup_rules.py claude cn          # Claude + 中文')
        print('  setup_rules.py cursor copilot     # Cursor + Copilot + English')
        print('  setup_rules.py all                # 全部 agent + English')
        print('  setup_rules.py all cn             # 全部 agent + 中文')
        sys.exit(1)

    args = sys.argv[1:]
    lang = 'en'
    agents = []

    for a in args:
        if a.lower() in ['en', 'cn']:
            lang = 'zh-CN' if a.lower() == 'cn' else 'en'
        elif a.lower() == 'all':
            agents = list(ALL_AGENTS)
        elif a.lower() in ALL_AGENTS:
            agents.append(a.lower())
        else:
            print(f'[ERROR] 未知参数: {a}')
            sys.exit(1)

    if not agents:
        print('[ERROR] 请指定至少一个 agent（claude/cursor/copilot/all）')
        sys.exit(1)

    # Deduplicate while preserving order
    seen = set()
    agents = [x for x in agents if not (x in seen or seen.add(x))]

    workspace = Path.cwd()
    if not (workspace / 'doc' / 'rules').exists():
        print('[ERROR] 请在项目根目录运行此脚本')
        sys.exit(1)

    success = install_agents(workspace, agents, lang)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
