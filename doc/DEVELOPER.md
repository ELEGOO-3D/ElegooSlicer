# Developer Tools

## clangd

C/C++ IntelliSense is very slow on Windows for this project. clangd provides much faster
navigation (go-to-definition, find references, etc.).

### Setup

First build the project, then run:

```bash
generate_clangd_config.bat            # Release
generate_clangd_config.bat debug      # Debug
generate_clangd_config.bat debuginfo  # RelWithDebInfo
```

This automatically:
1. Generates `compile_commands.json`
2. Copies `.clangd` and VS Code settings to the project root
3. Merges with any existing config

Reload the window (`Ctrl+Shift+P` > "Reload Window") to take effect.

### Config Files

| File | Purpose |
|---|---|
| `doc/vscode_settings/clangd` | clangd configuration |
| `doc/vscode_settings/settings.json` | Disables C/C++ IntelliSense, enables clangd |
| `doc/vscode_settings/extensions.json` | Recommends clangd extension |

## CMakePresets.json

Copy to project root to enable CMake Tools extension auto-detection:

```bash
copy doc\vscode_settings\CMakePresets.json CMakePresets.json
```

**Note**: build dependencies first via `build_release_windows.bat` — the preset expects
`deps/build-*/ElegooSlicer_dep/` to exist.

## AI Assistant Rules

Rule templates are under `doc/rules/`, organized by language (`en`/`zh-CN`) and agent.

Install to project root:

```bash
python scripts/setup_rules.py <agent...> [lang]
```

| Agent | What it installs |
|---|---|
| `codex` | AGENTS.md + .ai_rules/ |
| `claude` | AGENTS.md + .ai_rules/ + CLAUDE.md |
| `cursor` | AGENTS.md + .ai_rules/ + .cursor/rules/cursor.mdc |
| `copilot` | AGENTS.md + .ai_rules/ + .github/copilot-instructions.md |
| `all` | All of the above |

Lang: `cn` (Chinese), `en` (English, default).

```bash
python scripts/setup_rules.py all cn     # All agents + Chinese
python scripts/setup_rules.py codex      # Codex only + English
```

### Rule Structure

```
doc/rules/
├── en/                          ← English
│   ├── AGENTS.md                ← Core rules (always loaded)
│   ├── .ai_rules/               ← On-demand rules (build/git)
│   │   ├── BUILD.md
│   │   └── GIT.md
│   ├── claude/CLAUDE.md         ← Claude Code entry
│   ├── cursor/cursor.mdc        ← Cursor entry
│   └── copilot/copilot-instructions.md ← Copilot entry
└── zh-CN/                       ← Chinese (same structure)
```

> **Codex** reads `AGENTS.md` directly — no separate entry file needed.

Edit templates under `doc/rules/{lang}/`, then run the install script to deploy.
