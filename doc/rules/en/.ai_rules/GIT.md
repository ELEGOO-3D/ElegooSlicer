# Git Workflow

- **Always use `git pull --rebase`** — never `git pull` or `git merge` (keep linear history)
- Branches: `main` (production, no direct commits), version branches (e.g., `v1.5.3`)
- No `Co-Authored-By` lines in commits

## Commit Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

- **Types**: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`
- **Scope** (optional): `gui`, `slicer`, `network`, `printer`, `config`, `build`, `deps`, or specific module
- **Subject**: imperative, lowercase, no period, max 50 chars
- **Body**: wrapped at 72 chars, explains what and why
- **Footer**: references issues (`Closes #123`)

Simple commits can omit body and footer:
```
feat(gui): add crash test menu for debugging
fix(network): resolve timeout in printer discovery
```
