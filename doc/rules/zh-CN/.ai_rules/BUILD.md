# 构建与测试

## 构建命令

### Windows（主要平台）
```bash
build_release_windows.bat              # 完整 Release 构建
build_release_windows.bat debug        # Debug
build_release_windows.bat debuginfo    # RelWithDebInfo
build_release_windows.bat onlydeps     # 仅构建依赖项
build_release_windows.bat slicer       # 仅构建切片器
build_release_windows.bat packinstall  # 构建 + 打包安装程序
```

### macOS
```bash
./build_release_macos.sh          # 完整构建（依赖 + 切片器）
./build_release_macos.sh -d       # 仅构建依赖
./build_release_macos.sh -s       # 仅构建切片器
./build_release_macos.sh -x       # 使用 Ninja
```

### Linux
```bash
sudo ./BuildLinux.sh -u           # 更新系统依赖
./BuildLinux.sh -d                # 仅构建依赖
./BuildLinux.sh -s                # 仅构建切片器
./BuildLinux.sh -dsi              # 依赖 + 切片器 + AppImage
```

### clangd 配置（Windows）
```bash
generate_clangd_config.bat           # Release
generate_clangd_config.bat debug     # Debug
generate_clangd_config.bat debuginfo # RelWithDebInfo
```

## 构建输出
- Windows：`build/` 或 `build-dbginfo/`
- macOS / Linux：`build/`

## 构建选项速查

**Windows**：`debug`、`debuginfo`、`onlydeps`、`slicer`、`pack`、`packinstall`、`onlypack`、`dlweb`、`test`、`sign`

**macOS**：`-d`（依赖）、`-s`（切片器）、`-x`（Ninja）、`-a <arch>`（架构）、`-t <version>`（目标版本）、`-c <config>`（Debug/Release）、`-p`（打包）、`-e`（测试环境）、`-w`（Web 依赖）、`-b`（跳过 CMake）、`-1`（单核）、`-n`（每日构建）

**Linux**：`-u`（更新依赖，需 sudo）、`-d`（依赖）、`-s`（切片器）、`-i`（AppImage）、`-r`（跳过检查）、`-c`（清理）、`-1`（单核）、`-b`（Debug）

## 测试

```bash
cd build && ctest --output-on-failure
ctest --test-dir ./tests/libslic3r
ctest --test-dir ./tests/fff_print
```

## Sentry 调试符号

崩溃报告使用 Sentry Native SDK（Crashpad 后端）。构建时上传调试符号：

| 平台 | 标志 | 上传内容 |
|---|---|---|
| Windows | `uploadpdb` | `build\src\Release\` 和 `deps\...\usr\local\` 中的 `.pdb` |
| macOS | `-g` | `build/<arch>/src/<config>/` 中的 `.dSYM` |
| Linux | `-g` | `build/src/<config>/` 中的 ELF 调试信息 |

上传脚本（`scripts/upload_sentry_pdbs.py`）首次运行时自动下载 sentry-cli
（缓存在 `tools/sentry-cli/`，已 gitignore）。需要在 `.env` 中设置 `SENTRY_AUTH_TOKEN`。
