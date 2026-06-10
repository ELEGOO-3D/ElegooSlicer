# 构建和测试

## 构建命令

Windows 构建
```bash
# 完整构建
build_release_windows.bat

# 调试符号构建
build_release_windows.bat debug

# 带调试信息构建
build_release_windows.bat debuginfo

# 仅构建依赖项
build_release_windows.bat onlydeps

# 仅构建切片器（依赖项已构建后）
build_release_windows.bat slicer

# 构建切片器并打包安装程序
build_release_windows.bat packinstall

# 仅打包安装程序（不构建）
build_release_windows.bat onlypack

# 打包依赖项
build_release_windows.bat pack

# 下载 Web 依赖
build_release_windows.bat dlweb

# 测试环境
build_release_windows.bat test
```

macOS 构建
```bash
# 完整构建（依赖项和切片器）
./build_release_macos.sh

# 仅构建依赖项
./build_release_macos.sh -d

# 仅构建切片器（依赖项已构建后）
./build_release_macos.sh -s

# 使用 Ninja 生成器（更快的构建）
./build_release_macos.sh -x

# 指定架构构建
./build_release_macos.sh -a arm64    # 或 x86_64 或 universal

# 指定 macOS 目标版本
./build_release_macos.sh -t 11.3

# 构建配置（Debug 或 Release）
./build_release_macos.sh -c Debug
```

Linux 构建
```bash
# 完整构建（包含依赖项更新，需 sudo）
sudo ./BuildLinux.sh -u

# 仅构建依赖项
./BuildLinux.sh -d

# 仅构建切片器
./BuildLinux.sh -s

# 生成 AppImage
./BuildLinux.sh -i

# 跳过内存/磁盘检查
./BuildLinux.sh -r

# 组合命令示例：构建依赖项、切片器并生成 AppImage
./BuildLinux.sh -dsi
```

## 开发环境配置

clangd 配置（Windows）
```bash
# 生成 compile_commands.json 并配置 clangd
generate_clangd_config.bat

# 生成 Debug 模式的配置
generate_clangd_config.bat debug

# 生成 RelWithDebInfo 模式的配置
generate_clangd_config.bat debuginfo

# 带内部测试宏定义
generate_clangd_config.bat test
```

## 测试指南

- 提交前在目标平台上测试
- 运行应用程序验证改动
- 使用调试构建检查内存泄漏
- 如改动影响核心逻辑，需跨平台测试

## 构建输出

- Windows: `build/` 或 `build-dbginfo/`
- macOS: `build/`
- Linux: `build/`

可执行文件位置因平台而异 - 查看构建脚本输出。

## 常见构建选项

Windows
- `debug` - Debug 模式
- `debuginfo` - RelWithDebInfo 模式（带调试符号）
- `onlydeps` - 仅构建依赖项
- `slicer` - 仅构建切片器
- `pack` - 打包依赖项
- `packinstall` - 构建并打包安装程序
- `onlypack` - 仅打包安装程序
- `dlweb` - 下载 Web 依赖
- `test` - 内部测试版本
- `sign` - 对二进制文件进行签名

macOS
- `-d` - 仅构建依赖项
- `-s` - 仅构建切片器
- `-x` - 使用 Ninja（推荐）
- `-a <arch>` - 指定架构
- `-t <version>` - macOS 目标版本
- `-c <config>` - Debug 或 Release
- `-p` - 打包依赖项
- `-e` - 测试环境
- `-w` - 下载 Web 依赖
- `-b` - 仅构建（跳过 CMake 配置）
- `-1` - 单核构建
- `-n` - 夜间构建

Linux
- `-u` - 更新依赖项（需 sudo）
- `-d` - 仅构建依赖项
- `-s` - 仅构建切片器
- `-i` - 生成 AppImage
- `-r` - 跳过内存/磁盘检查
- `-c` - 强制清理构建
- `-1` - 单核构建
- `-b` - Debug 模式构建
