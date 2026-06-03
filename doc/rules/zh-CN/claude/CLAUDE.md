# ElegooSlicer — Claude Code 项目指南

## 项目概述

ElegooSlicer 是一款开源 3D 切片应用程序，最初从 OrcaSlicer 分支而来（OrcaSlicer 源自 Bambu Studio → PrusaSlicer → Slic3r）。使用 C++17 编写，采用 wxWidgets 作为 GUI 框架，CMake 作为构建系统。

**技术栈：** C++17（+ 部分 C++20），wxWidgets 3.2+，OpenGL + ImGui 用于 3D 视口，Intel TBB 用于并行计算，CMake ≥3.13。
**依赖管理：** 依赖项在 `deps/` 中单独管理并构建；主项目通过 `CMAKE_PREFIX_PATH` 引用。

### 架构

| 目录 | 用途 |
|---|---|
| `libslic3r/` | 核心切片引擎（平台无关）：几何、G-code 生成、填充、支撑、文件 I/O、SLA |
| `src/slic3r/GUI/` | wxWidgets GUI 应用程序 |
| `src/ElegooSlicer.cpp` | 应用程序入口点 |
| `libslic3r/Print.cpp` | 编排切片流程 |
| `PrintConfig.cpp` | 所有打印/打印机/材料设置定义 |
| `resources/profiles/` | 按制造商分类的打印机和材料 JSON 配置文件 |
| `resources/web/` | 嵌入式 Web 资源 |
| `localization/` | i18n 翻译文件 |

### 关键算法
- **Arachne** — 通过骨架梯形化实现变宽度周长生成
- **树形支撑** — 有机支撑生成
- **闪电填充** — 稀疏内部填充优化
- **自适应切片** — 基于几何的可变层高

---

## AI 交互规则

### 身份与语言
你是 Claude Code 中的 AI 编程助手，协助 ElegooSlicer 开发。
- **对话语言**：中文
- **代码、注释、提交信息**：英文

### 任务分类

开始前，将任务分为三个等级。每个等级有不同的工作流程要求。

| 等级 | 范围 | 示例 | 工作流程 |
|------|------|------|----------|
| **简单** | 单一位置，明显修复 | 错别字、单行修复、重命名变量、添加 getter | 直接修改 → 事后说明 |
| **标准** | 单模块，需求明确 | 添加配置字段、修复空指针检查、添加 UI 按钮、修改单个函数 | 分析 → 实现 → 自审 |
| **复杂** | 多文件、跨模块、架构级 | 新功能、重构、跨模块变更、性能优化 | 完整分析 → 确认 → 实现 → 自审 |

如有疑问，按更高等级处理。用户可以说"直接改"来降级。

### 工作流程：标准等级（大多数任务）

#### 1. 分析
编辑前说明以下内容：
- **改什么**：需要改什么以及为什么
- **在哪里**：哪些文件/函数受影响（使用搜索工具验证，不要猜测）
- **怎么改**：简述修改方案

使用工具支持分析 — 不要依赖假设：
- `grep` / `find references` 定位所有调用者
- `read` 实际文件了解当前结构
- `go to definition` 追踪依赖

以简洁计划呈现：
```
计划：
- FileA.cpp:100 — 给 `onConnect()` 添加超时参数
- FileB.hpp:50 — 声明新参数并设置默认值

原因：当前连接没有超时，慢网络时会导致挂起。
```

#### 2. 实现
- 编辑前读取当前文件状态 — 不要依赖过时的上下文
- **编辑每个文件前**：简述你将对该文件做什么修改
- 精准编辑：只改计划要求的内容
- 优先添加新函数/类/重载，而非重写现有代码
- 如果实现发现计划有误：停止，重新分析，呈现更新的计划

#### 3. 自审
所有编辑完成后，检查：
- **计划匹配**：代码是否符合既定计划？
- **孤儿代码**：是否引入了未使用的 include/变量/函数？
- **一致性**：命名和风格是否与周围代码匹配？
- **构建风险**：是否有缺失声明、头文件问题、平台隐患？
- 修复发现的问题，然后总结变更内容。

### 工作流程：复杂等级

与标准等级相同，增加以下内容：

#### 步骤 1 之前：范围确认
- 列出所有受影响的文件/模块
- 识别跨模块依赖和风险
- 向用户呈现范围，等待确认后再继续

#### 步骤 1 增强：深度分析
- 追踪完整调用链：入口点 → 中间函数 → 目标
- 映射数据流：数据如何在链中转换
- 识别副作用：下游调用者、相关功能、平台差异
- 以结构化修改计划呈现，包含 file:line 引用

#### 步骤 3 增强：按文件审查
- 实现每个文件后，简述变更内容
- 所有文件完成后，呈现所有变更的总结供用户审查

### 工作流程：简单等级

直接做。修改后，用一行总结改了什么以及为什么。

### 危险操作 — 先确认

以下操作执行前需要用户明确确认：
- 删除文件或大段代码
- 修改核心架构（如改变类层次结构、模块边界）
- 批量重命名影响 5+ 处
- 修改构建配置（CMakeLists.txt、构建脚本）
- 修改第三方依赖
- 全文件格式化或重新格式化

### 代码风格规则

- 最小化变更：只改必要的部分
- 除非明确要求，不要生成文档或测试用例
- 引用代码位置时，使用格式 `filepath:startLine-endLine`
- 匹配现有文件风格，即使你会用不同方式
- 不要"改进"相邻代码、注释或格式
- 不要重构没有问题的东西
- 如果发现无关的死代码，提一下 — 不要删除
- 当你的变更产生孤儿代码：只删除你的变更导致未使用的部分

### 简洁原则

- 不添加超出要求的功能
- 不为单次使用的代码做抽象
- 不添加未请求的"灵活性"或"可配置性"
- 不为不可能的场景添加错误处理
- 如果 200 行能缩成 50 行，重写它

### 交互风格

- 提出问题或疑虑时，提供选项/方案以便快速决策
- 对于复杂需求，创建任务列表注明受影响的文件/模块，明确优先级和依赖关系
- 当用户指出错误时，立即修复并更新相关记忆或规则

### 规则参考

- Git 操作：参见下方 Git 工作流程章节
- 编码标准：参见下方编码标准章节
- 构建与测试：参见下方构建与测试章节
- 开发日志：参见下方开发日志章节

---

## 编码标准

> **核心原则**：这是一个遗留代码库。修改现有文件时，匹配该文件的主流风格。新代码遵循以下规则。

### 命名与结构
- 类/结构体/枚举：`PascalCase`；函数/方法/变量：`camelCase`（函数以动词开头）
- 成员变量：`m` + CamelCase（如 `mPrinterList`）；全局变量：`g` + PascalCase；静态变量：`s` + PascalCase
- 常量/宏：`ALL_CAPS`；布尔值：动词前缀（`isReady`、`hasError`、`canUpdate`）
- 头文件使用 `#pragma once`；优先使用前向声明减少依赖
- 包含顺序（组间空行）：配对头文件 → 项目头文件 → 第三方库 → 标准库
- 模块依赖遵循单向规则：底层模块（Utils/Core）绝不依赖高层模块（GUI/Business）
- 跨模块调用应使用接口/回调，而非直接依赖具体实现
- 路径处理使用 `std::filesystem` 或 `boost::nowide`（支持 Unicode/中文路径）

### 函数与类
- 函数/类单一职责，名称应准确描述用途
- 布尔返回值：`is/has/can` 前缀；void：`execute/save/update`。使用 early return 减少嵌套
- 标量/小对象按值传递，大对象用 `const T&`，可变引用用 `T&`。考虑使用 `std::optional<T>`
- 优先返回值（结构体/`tuple`/`optional`/`expected`）而非输出参数。引用输出命名为 `outXYZ`
- 缓冲区使用 `std::span<T>`，只读字符串使用 `std::string_view`
- 裸指针表示"可空且无所有权"。所有权转移使用 `unique_ptr`/`shared_ptr`
- 函数入口验证指针/引用参数。不可修改的对象使用 `const`
- 优先组合而非继承。尽可能使用 `const`；适用处使用 `constexpr`
- 成员应为私有并提供 getter/setter。非修改成员函数标记为 `const`

### 内存与错误处理
- 遵循 RAII。除 GUI 控件和第三方库接口外，避免裸 `new`/`delete`。优先使用 `unique_ptr`/`shared_ptr`
- wxWidgets 控件有父子生命周期管理。自定义控件需要明确的所有权
- 不要在循环中反复分配/释放 — 重用或预分配
- 可预期的失败：使用 `optional`/`expected`/错误码。不可恢复的错误：使用带上下文的标准异常
- 在函数边界验证输入。错误消息应包含上下文/路径/参数值（不含私有数据）

### 现代 C++
- `auto` 仅用于复杂迭代器/lambda。基本类型（`int`/`double`/`bool`）使用显式类型
- 优先使用 C++17 特性（range-for、结构化绑定）、`enum class`、`nullptr`
- 避免 C 风格转换；使用 `static_cast`/`dynamic_cast`。优先使用 `vector`/`map`/`unordered_map`
- 使用 `std::move` 减少拷贝；优先栈对象而非堆分配
- 使用 `<algorithm>`（如 `std::sort`、`std::for_each`）而非裸循环
- 避免全局变量；谨慎使用单例。分离接口与实现

### 并发与线程
- UI 更新必须在主线程。后台线程通过 `CallAfter`/事件队列通信 — 绝不直接操作控件
- 使用 `thread`/`mutex`/`lock_guard`/`atomic` 保证线程安全。共享资源需要明确的所有权和锁策略
- 跨线程数据传递优先值拷贝或移动，而非共享指针。长时间操作使用线程池

### 注释与文档
- 所有代码和文档使用**英文**。变量/参数/返回值显式声明类型
- 为类、方法和关键逻辑编写清晰注释
- 注释解释**为什么**，而非是什么。不要复述代码
- 注释只描述客观信息：**代码意图/业务约束/资源所有权/线程约束**。绝不包含协作背景（如"基于 AI 讨论"、"按用户要求"、聊天记录）
- Bug 修复注释使用可追溯引用：`// Fix: <简要原因> (ISSUE-ID)`
- 公共类/函数使用 Doxygen：`/** @brief ... @param ... @return ... */`
- TODO 格式：`// TODO(所有者|ISSUE-ID): 描述`

### 避免
- 头文件中使用 `using namespace std`
- 在头文件中实现非模板函数
- C 风格字符串（`char*`）和数组 — 使用 `std::string`/`std::vector`
- 忽略编译器警告 — 修复所有警告

---

## Git 工作流程

- 提交信息使用**英文**。
- **始终使用 `git pull --rebase`** — 绝不使用 `git pull` 或 `git merge`（保持线性历史）
- 不要在提交中添加 `Co-Authored-By` 行

### 标准工作流程
```bash
git add <files>
git commit -m "type(scope): description"
git pull --rebase origin <branch>
# 如有冲突：解决 → git add <files> → git rebase --continue
git push origin <branch>
```

### 分支
- `main` — 生产就绪，无直接提交。版本分支在发布后合并并打标签
- 版本分支（如 `v1.5.2`） — 主要开发目标

### 提交信息格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

- **类型**：`feat`、`fix`、`docs`、`style`、`refactor`、`test`、`chore`
- **范围**（可选）：`gui`、`slicer`、`network`、`printer`、`config`、`build`、`deps`，或特定模块名
- **主题**：祈使语气，小写，无句号，最多 50 字符
- **正文**：72 字符换行，说明做了什么以及为什么
- **脚注**：引用 issue（如 `Closes #123`）

简单提交可省略正文和脚注：
```
feat(gui): add crash test menu for debugging
fix(network): resolve timeout in printer discovery
```

---

## 构建与测试

### Windows 构建（主要平台）
```bash
# 完整 Release 构建
.\build_release_windows.bat

# Debug / RelWithDebInfo
.\build_release_windows.bat debug
.\build_release_windows.bat debuginfo

# 仅构建依赖
.\build_release_windows.bat onlydeps

# 仅构建 Slicer
.\build_release_windows.bat slicer

# 构建 + 打包安装程序
.\build_release_windows.bat packinstall

# 构建 + 上传 PDB 到 Sentry
.\build_release_windows.bat slicer uploadpdb
.\build_release_windows.bat packinstall uploadpdb
```

### clangd 配置（Windows）
```bash
.\generate_clangd_config.bat           # Release
.\generate_clangd_config.bat debug     # Debug
.\generate_clangd_config.bat debuginfo # RelWithDebInfo
```

### macOS 构建
```bash
./build_release_macos.sh        # 完整构建
./build_release_macos.sh -d     # 仅构建依赖
./build_release_macos.sh -s     # 仅构建 Slicer
./build_release_macos.sh -x     # 使用 Ninja
./build_release_macos.sh -s -g  # 构建 + 上传 dSYM 到 Sentry
```

### Linux 构建
```bash
./build_linux.sh -u             # 安装系统依赖（需要 sudo）
./build_linux.sh -d             # 仅构建依赖
./build_linux.sh -s             # 仅构建 Slicer
./build_linux.sh -dsi           # 依赖 + Slicer + AppImage
./build_linux.sh -dsig          # 依赖 + Slicer + AppImage + 上传符号
```

### 构建输出
- Windows：`build/` 或 `build-dbginfo/`
- macOS / Linux：`build/`

### Sentry 调试符号上传

崩溃报告使用 Sentry Native SDK（Crashpad 后端）。要获取符号化的堆栈跟踪，需在构建时上传调试符号：

| 平台 | 标志 | 上传内容 |
|------|------|----------|
| Windows | `uploadpdb` | `build\src\Release\` 和 `deps\...\usr\local\` 中的 `.pdb` |
| macOS | `-g` | `build/<arch>/src/<config>/` 中的 `.dSYM` |
| Linux | `-g` | `build/src/<config>/` 中的 ELF 调试信息 |

上传脚本（`scripts/upload_sentry_pdbs.py`）首次运行时自动下载 sentry-cli（缓存在 `tools/sentry-cli/`，已 gitignore）。需要在 `.env` 中设置 `SENTRY_AUTH_TOKEN`。

### 测试
- 提交前在目标平台上测试
- 运行应用程序手动验证变更
- 使用 debug 构建检查内存泄漏
- 如果修改了核心逻辑，需要跨平台测试

---

## 开发日志

当用户明确说"记录"时，创建或更新开发日志条目。

### 位置与命名
- 目录：`dev_log/`
- **主要功能/重构**：创建 `module-name.md`
- **日常修复/优化/小改动**：追加到 `YYYY-MM-DD.md`
- 一个主题 = 一个文件（不要为同一功能创建 `test-checklist.md`、`final-status.md` 等）

### 内容要求
**必填**：日期、需求（1-2 句话）、实现方案（3-5 个要点）、新增/修改的文件、状态
**选填**：修复内容、待测试项、备注、已完成的 TODO、待处理的 TODO（用于较大功能）

### 禁止
- 详细代码片段、完整架构图、详细测试步骤
- 未经用户明确触发（"记录"）自动记录
