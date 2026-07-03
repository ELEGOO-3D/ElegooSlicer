# ElegooSlicer

开源 3D 切片软件，基于 OrcaSlicer 开发（OrcaSlicer 源自 Bambu Studio → PrusaSlicer → Slic3r）。
C++17，wxWidgets GUI，CMake 构建系统。模块化架构，核心切片、GUI 和平台特定代码使用独立库。

## 技术栈

- C++17（+ 部分 C++20），Objective-C++（macOS），Python/Batch/Shell（脚本）
- wxWidgets 3.2+，ImGui 用于 3D 视口，OpenGL 渲染
- Intel TBB + std::thread 并行计算
- CMake >= 3.13（Windows 建议 <= 3.31.x）
- 依赖项在 `deps/` 中单独构建，通过 `CMAKE_PREFIX_PATH` 引入

## 目录结构

| 目录 | 用途 |
|---|---|
| `src/libslic3r/` | 核心切片引擎（平台无关）：几何、G-code、填充、支撑、文件 I/O、SLA |
| `src/libslic3r/GCode/` | G-code 生成、冷却、压力提前、缩略图 |
| `src/libslic3r/Fill/` | 填充图案（gyroid、蜂窝、闪电填充等） |
| `src/libslic3r/Support/` | 树形支撑和传统支撑生成 |
| `src/libslic3r/Geometry/` | 高级几何、Voronoi 图、中轴 |
| `src/libslic3r/Format/` | 文件 I/O（3MF、AMF、STL、OBJ、STEP） |
| `src/libslic3r/Arachne/` | 基于骨架梯形化的可变宽度周长 |
| `src/slic3r/GUI/` | wxWidgets GUI 应用 |
| `src/ElegooSlicer.cpp` | 应用入口 |
| `src/libslic3r/Print.cpp` | 切片流程编排 |
| `src/libslic3r/PrintConfig.cpp` | 所有打印/打印机/材料设置定义 |
| `resources/profiles/` | 按制造商分类的打印机和材料 JSON 配置 |
| `resources/web/` | 嵌入式 Web UI（首页、指南、打印机管理） |
| `resources/i18n/` | 运行时语言资源 |
| `localization/` | i18n 翻译文件（.pot、.po） |
| `tests/` | 测试套件（libslic3r、fff_print、sla_print） |

### 关键算法

- **Arachne**：基于骨架梯形化的可变宽度周长生成
- **树形支撑**：有机支撑生成
- **闪电填充**：稀疏内部填充优化
- **自适应切片**：基于几何的可变层高
- **多材料**：多挤出机和可溶性支撑处理

### 外部依赖

Clipper2、libigl、TBB、wxWidgets、OpenGL、CGAL、OpenVDB、Eigen、nlohmann/json、
curl、OpenSSL、imgui、boost

## 构建与测试

当用户提到 build、compile、test、CMake、Sentry 或相关话题 → 读取 `.ai_rules/BUILD.md`

## 关键约束

- **向后兼容**：.3mf 文件和打印机配置必须在旧版本中可加载
- **跨平台**：所有改动必须在 Windows、macOS、Linux 上正常工作
- **配置/格式变更**需要版本迁移处理
- 修改旧代码时匹配文件现有风格 — 不要重新格式化或"改进"相邻代码

## 编码规范

> 这是旧项目。修改现有文件时，匹配该文件的主流风格。新代码遵循以下规则。

### 命名

- 类/结构/枚举：`PascalCase`；函数/方法/变量：`camelCase`（函数以动词开头）
- 成员变量：`m` + PascalCase（如 `mPrinterList`）；全局变量：`g` + PascalCase；静态变量：`s` + PascalCase
- 常量/宏：`ALL_CAPS`；布尔值：动词前缀（`isReady`、`hasError`、`canUpdate`）
- 头文件：`#pragma once`；优先使用前向声明

### Include 顺序

组间空行：配对头文件 → 项目头文件 → 第三方库 → 标准库。

### 函数与类

- 单一职责；名称准确描述用途
- 标量/小对象按值传递，大对象用 `const T&`，可变用 `T&`
- 优先返回值（结构体/`tuple`/`optional`/`expected`）而非输出参数
- 裸指针 = "可空，无所有权"；所有权转移使用 `unique_ptr`/`shared_ptr`
- 优先组合而非继承；能 `const` 则 `const`；能 `constexpr` 则 `constexpr`
- 成员私有并提供 getter/setter；非修改方法标记 `const`

### 内存与错误处理

- RAII：避免裸 `new`/`delete`（GUI 控件和第三方接口除外）
- 不要在循环中反复分配/释放 — 复用或预分配
- 预期失败：`optional`/`expected`/错误码；不可恢复：带上下文的标准异常
- 在函数边界验证输入

### 现代 C++

- `auto` 仅用于复杂迭代器/lambda；基础类型显式声明
- 优先 C++17 特性（range-for、结构化绑定）、`enum class`、`nullptr`
- 避免 C 风格转换；使用 `static_cast`/`dynamic_cast`
- 使用 `<algorithm>` 替代裸循环；优先栈对象而非堆分配
- 避免全局变量；谨慎使用单例；分离接口与实现

### 并发

- UI 更新必须在主线程；后台线程通过 `CallAfter`/事件队列通信
- 使用 `thread`/`mutex`/`lock_guard`/`atomic` 保证线程安全
- 跨线程数据传递：优先值拷贝或移动，避免共享指针

### 注释

- 代码、注释、提交信息使用**英文**
- 注释解释**为什么**，而非是什么 — 不要复述代码
- 注释只描述客观信息：代码意图、业务约束、资源所有权、线程约束。绝不包含协作背景（"根据 AI 讨论"、"按用户要求"、聊天记录等）
- Bug 修复：`// Fix: <原因> (ISSUE-ID)`
- 公共 API：Doxygen（`/** @brief ... @param ... @return ... */`）
- TODO：`// TODO(负责人|ISSUE-ID): 描述`

### 避免事项

- 头文件中 `using namespace std`
- 在头文件中实现非模板函数
- C 风格字符串/数组 — 使用 `std::string`/`std::vector`
- 忽略编译器警告 — 修复它们

## Git

当用户提到 git、commit、push、merge、rebase、branch 或相关话题 → 读取 `.ai_rules/GIT.md`

## AI 交互规则

- **对话**：使用用户所用的语言回复
- **代码、注释、提交信息**：英文

### 任务分类

开始前，将任务分为三个等级：

| 等级 | 范围 | 工作流程 |
|---|---|---|
| **简单** | 单一位置，明显修复（错别字、重命名、添加 getter） | 直接修改 → 事后说明 |
| **标准** | 单模块，需求明确 | 分析 → 实现 → 自审 |
| **复杂** | 多文件、跨模块、架构级 | 完整分析 → 确认 → 实现 → 自审 |

如有疑问，按更高等级处理。

### 标准工作流程

**1. 分析** — 编辑前说明：改什么、在哪里（用工具验证，不要猜）、怎么改。
以简洁计划呈现，含 file:line 引用。

**2. 实现** — 先读取当前文件状态。精准编辑，优先新增而非重写。
如果实现发现计划有误：停止，重新分析。

**3. 自审** — 检查：计划匹配、孤儿代码（未使用的 include/变量/函数）、风格一致性、
构建风险。修复问题，然后总结。

### 复杂工作流程（附加）

- **分析前**：列出所有受影响文件/模块，识别跨模块风险，获得用户确认
- **增强分析**：追踪完整调用链，映射数据流，识别副作用
- **增强审查**：逐文件总结，最后整体总结

### 简单工作流程

直接做。一行总结改了什么。

### 代码风格规则

- 最小化变更：只改必要的部分
- 除非明确要求，不要生成文档或测试
- 匹配现有文件风格，即使你会用不同方式
- 不要"改进"相邻代码、注释或格式
- 不要重构没有问题的东西
- 发现死代码：提一下，不要删
- 你的变更产生的孤儿代码：只删除你的变更导致未使用的部分

### 简洁原则

- 不添加超出要求的功能
- 不为单次使用做抽象
- 不添加未请求的灵活性或可配置性
- 不为不可能的场景添加错误处理
- 如果能更短且不失清晰，就缩短它

### 危险操作 — 先确认

- 删除文件或大段代码
- 修改核心架构（类层次、模块边界）
- 批量重命名影响 5+ 处
- 修改构建配置（CMakeLists.txt、构建脚本）
- 修改第三方依赖
- 全文件格式化

### 交互风格

- 提出问题或疑虑时，提供选项以便快速决策
- 对于复杂需求，创建任务列表注明受影响文件/模块、优先级和依赖关系
- 当用户指出错误时，立即修复
