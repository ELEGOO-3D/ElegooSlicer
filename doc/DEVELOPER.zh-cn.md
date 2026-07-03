# 开发者工具

## clangd

VS Code 自带的 C/C++ 扩展在此项目中导航（跳转定义、查找引用等）非常慢。clangd 可大幅提升速度。

> **注意**：必须先完成构建，再执行 clangd 配置脚本 — 脚本需要读取构建产物来生成
> `compile_commands.json`（基于 Ninja）。例如：`build_release_windows.bat`、`build_release_windows.bat debug` 等。

构建完成后，运行：

```bash
generate_clangd_config.bat            # Release
generate_clangd_config.bat debug      # Debug
generate_clangd_config.bat debuginfo  # RelWithDebInfo
```

脚本会从构建产物生成 `compile_commands.json`，并拷贝 `.clangd` 和 VS Code 配置到项目根目录。
重新加载窗口（`Ctrl+Shift+P` > "Reload Window"）即可生效。

> **需要重新执行的情况**：新增/删除文件、修改宏定义、改动函数签名、或更换构建配置后。
> clangd 依赖最新的 `compile_commands.json` 才能保持准确。

### 配置文件

| 文件 | 用途 |
|---|---|
| `doc/vscode_settings/clangd` | clangd 配置 |
| `doc/vscode_settings/settings.json` | 禁用 C/C++ IntelliSense，启用 clangd |
| `doc/vscode_settings/extensions.json` | 推荐安装 clangd 扩展 |

## CMakePresets.json

拷贝到项目根目录，启用 CMake Tools 扩展自动检测构建预设：

```bash
copy doc\vscode_settings\CMakePresets.json CMakePresets.json
```

**注意**：需先通过 `build_release_windows.bat` 构建依赖项，预设依赖 `deps/build-*/ElegooSlicer_dep/` 目录。

## AI 辅助规则

规则模板存放在 `doc/rules/`，按语言（`en`/`zh-CN`）和 agent 类型组织。

安装到项目根目录：

```bash
python scripts/setup_rules.py <agent...> [lang]
```

| Agent | 安装内容 |
|---|---|
| `codex` | AGENTS.md + .ai_rules/ |
| `claude` | AGENTS.md + .ai_rules/ + CLAUDE.md |
| `cursor` | AGENTS.md + .ai_rules/ + .cursor/rules/cursor.mdc |
| `copilot` | AGENTS.md + .ai_rules/ + .github/copilot-instructions.md |
| `all` | 以上全部 |

语言：`cn`（中文）、`en`（英文，默认）。

```bash
python scripts/setup_rules.py all cn     # 全部 agent + 中文
python scripts/setup_rules.py codex      # 仅 Codex + 英文
```

### 规则结构

```
doc/rules/
├── en/                          ← 英文
│   ├── AGENTS.md                ← 核心规则（始终加载）
│   ├── .ai_rules/               ← 按需规则（构建/Git）
│   │   ├── BUILD.md
│   │   └── GIT.md
│   ├── claude/CLAUDE.md         ← Claude Code 入口
│   ├── cursor/cursor.mdc        ← Cursor 入口
│   └── copilot/copilot-instructions.md ← Copilot 入口
└── zh-CN/                       ← 中文（同上结构）
```

> **Codex** 直接读取 `AGENTS.md`，无需独立入口文件。

修改 `doc/rules/{lang}/` 下的模板后，运行安装脚本即可部署。
