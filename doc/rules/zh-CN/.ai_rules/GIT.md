# Git 工作流

- **始终使用 `git pull --rebase`** — 禁止 `git pull` 或 `git merge`（保持线性历史）
- 分支：`main`（生产就绪，禁止直接提交），版本分支（如 `v1.5.3`）
- 不要在提交中添加 `Co-Authored-By` 行

## 提交信息格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

- **类型**：`feat`、`fix`、`docs`、`style`、`refactor`、`test`、`chore`
- **范围**（可选）：`gui`、`slicer`、`network`、`printer`、`config`、`build`、`deps`，或具体模块名
- **主题**：祈使语气，小写，无句号，最多 50 字符
- **正文**：72 字符换行，说明做了什么以及为什么
- **脚注**：引用 issue（如 `Closes #123`）

简单提交可省略正文和脚注：
```
feat(gui): add crash test menu for debugging
fix(network): resolve timeout in printer discovery
```
