# ElegooSlicer — Project Guide for Claude Code

## Project Overview

ElegooSlicer is an open-source 3D slicing application, originally forked from OrcaSlicer (which derives from Bambu Studio → PrusaSlicer → Slic3r). Written in C++17, it uses wxWidgets for the GUI and CMake as its build system.

**Tech stack:** C++17 (+ some C++20), wxWidgets 3.2+, OpenGL + ImGui for 3D viewport, Intel TBB for parallelism, CMake ≥3.13.
**Dependencies:** Managed in `deps/` and built separately; the main project picks them up via `CMAKE_PREFIX_PATH`.

### Architecture

| Directory | Purpose |
|---|---|
| `libslic3r/` | Core slicing engine (platform-agnostic): geometry, G-code generation, infill, supports, file I/O, SLA |
| `src/slic3r/GUI/` | wxWidgets GUI application |
| `src/ElegooSlicer.cpp` | Application entry point |
| `libslic3r/Print.cpp` | Orchestrates the slicing pipeline |
| `PrintConfig.cpp` | All print/printer/material setting definitions |
| `resources/profiles/` | Printer & material JSON profiles by manufacturer |
| `resources/web/` | Embedded web resources |
| `localization/` | i18n translation files |

### Key Algorithms
- **Arachne** — variable-width perimeter generation via skeleton trapezoidation
- **Tree supports** — organic support generation
- **Lightning infill** — sparse internal infill optimization
- **Adaptive slicing** — geometry-based variable layer height

---

## AI Interaction Rules

### Identity & Language
You are an AI coding assistant in Claude Code, helping with ElegooSlicer development.
- **Conversation**: Chinese (中文)
- **Code, comments, commit messages**: English

### Code Changes
- Always read current file state before editing — never rely on stale conversation context.
- Minimize changes: modify only what is necessary.
- Prefer adding new functions/classes/overloads over rewriting existing code (avoid merge conflicts).
- Do NOT generate documentation or test cases unless explicitly asked.
- When referencing code locations, use the format `filepath:startLine-endLine`.

### Dangerous Operations — Confirm First
Before executing any of the following, ask for user confirmation:
- Deleting files or large blocks of code
- Modifying core architecture or critical logic
- Batch renaming or large-scale refactoring
- Changing build configuration or dependencies
- Full-file formatting (risk of merge conflicts)
- Any other operation that could impact project stability

### Interaction Style
- When raising questions or issues, present options/solutions for quick decision-making.
- For complex requirements (multi-file, multi-step, architecture changes), first confirm scope, create a task list noting affected files/modules, clarify priorities and dependencies.
- Edit code directly — don't just offer suggestions.
- When the user points out an error, fix it immediately and update relevant memory or rules.

### Rule Reference
- Git operations: see the Git Workflow section below
- Coding standards: see the Coding Standards section below
- Build & test: see the Build and Test section below
- Dev logging: see the Dev Log section below

---

## Coding Standards

> **Key principle**: This is a legacy codebase. When modifying existing files, match the file's prevailing style. New code follows the rules below.

### Naming & Structure
- Classes/structs/enums: `PascalCase`; functions/methods/variables: `camelCase` (functions start with a verb)
- Member variables: `m` + CamelCase (`mPrinterList`); globals: `g` + PascalCase; statics: `s` + PascalCase
- Constants/macros: `ALL_CAPS`; booleans: verb prefix (`isReady`, `hasError`, `canUpdate`)
- Headers use `#pragma once`; prefer forward declarations to reduce dependencies.
- Include order (blank line between groups): paired header → project headers → third-party → standard library
- Module dependencies follow one-way rule: low-level modules (Utils/Core) never depend on high-level modules (GUI/Business).
- Cross-module calls should use interfaces/callbacks, not direct concrete implementation dependencies.
- Use `std::filesystem` or `boost::nowide` for path handling (Unicode/Chinese path support).

### Functions & Classes
- Single responsibility per function/class. Names should accurately describe purpose.
- Boolean returns: `is/has/can` prefix; void: `execute/save/update`. Use early returns to reduce nesting.
- Pass scalars/small objects by value, large objects by `const T&`, mutable by `T&`. Consider `std::optional<T>`.
- Prefer returning values (struct/`tuple`/`optional`/`expected`) over output parameters. Name reference outputs `outXYZ`.
- Use `std::span<T>` for buffers, `std::string_view` for read-only strings.
- Raw pointers mean "nullable and no ownership". Ownership transfer uses `unique_ptr`/`shared_ptr`.
- Validate pointer/reference arguments at function entry. Use `const` on non-modifiable objects.
- Prefer composition over inheritance. Use `const` wherever possible; `constexpr` where applicable.
- Members should be private with getters/setters. Mark non-mutating member functions `const`.

### Memory & Error Handling
- Follow RAII. Avoid raw `new`/`delete` except for GUI controls and third-party library interfaces. Prefer `unique_ptr`/`shared_ptr`.
- wxWidgets controls have parent-child lifetime management. Custom controls need explicit ownership.
- Don't repeatedly allocate/deallocate in loops — reuse or pre-allocate.
- Expected failures: use `optional`/`expected`/error codes. Unrecoverable errors: use standard exceptions with context.
- Validate inputs at function boundaries. Error messages should include context/path/parameter values (no private data).

### Modern C++
- `auto` only for complex iterators/lambdas. Explicit types for primitives (`int`/`double`/`bool`).
- Prefer C++17 features (range-for, structured bindings), `enum class`, `nullptr`.
- Avoid C-style casts; use `static_cast`/`dynamic_cast`. Prefer `vector`/`map`/`unordered_map`.
- Use `std::move` to reduce copies; prefer stack-based objects over heap allocation.
- Use `<algorithm>` (e.g., `std::sort`, `std::for_each`) instead of raw loops.
- Avoid global variables; use singletons sparingly. Separate interface from implementation.

### Concurrency & Threading
- UI updates MUST happen on the main thread. Background threads communicate via `CallAfter`/event queue — never directly manipulate controls.
- Use `thread`/`mutex`/`lock_guard`/`atomic` for thread safety. Shared resources need clear ownership and lock strategy.
- Prefer value copy or move for cross-thread data transfer, not shared pointers. Use thread pools for long-running operations.

### Comments & Documentation
- All code and documentation in **English**. Explicitly declare types for variables/parameters/return values.
- Write clear comments for classes, methods, and key logic.
- Comments explain **why**, not what. Don't restate the code.
- Comments describe only objective information: **code intent / business constraints / resource ownership / threading constraints**. Never include collaboration background (e.g., "based on AI discussion", "per user request", chat logs).
- Bug-fix comments use traceable references: `// Fix: <brief reason> (ISSUE-ID)`
- Public classes/functions use Doxygen: `/** @brief ... @param ... @return ... */`
- TODO format: `// TODO(OWNER|ISSUE-ID): description`

### Avoid
- `using namespace std` in headers
- Implementing non-template functions in headers
- C-style strings (`char*`) and arrays — use `std::string`/`std::vector`
- Ignoring compiler warnings — fix all warnings

---

## Git Workflow

- Commit messages in **English**.
- **Always use `git pull --rebase`** — never `git pull` or `git merge` (keep linear history).
- Do NOT add `Co-Authored-By` lines to commits.

### Standard Workflow
```bash
git add <files>
git commit -m "type(scope): description"
git pull --rebase origin <branch>
# If conflicts: resolve → git add <files> → git rebase --continue
git push origin <branch>
```

### Branches
- `main` — production-ready, no direct commits. Version branches merge into it with a tag after release.
- Version branches (e.g., `v1.5.2`) — primary development target.

### Commit Message Format
```
<type>(<scope>): <subject>

<body>

<footer>
```

- **Types**: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`
- **Scope** (optional): `gui`, `slicer`, `network`, `printer`, `config`, `build`, `deps`, or specific module name
- **Subject**: imperative mood, lowercase, no period, max 50 chars
- **Body**: wrapped at 72 chars, explains what and why
- **Footer**: references issues (e.g., `Closes #123`)

Simple commits can omit body and footer:
```
feat(gui): add crash test menu for debugging
fix(network): resolve timeout in printer discovery
```

---

## Build and Test

### Windows Build (primary)
```bash
# Full release build
.\build_release_windows.bat

# Debug / RelWithDebInfo
.\build_release_windows.bat debug
.\build_release_windows.bat debuginfo

# Dependencies only
.\build_release_windows.bat onlydeps

# Slicer only
.\build_release_windows.bat slicer

# Build + package installer
.\build_release_windows.bat packinstall

# Build + upload PDB to Sentry
.\build_release_windows.bat slicer uploadpdb
.\build_release_windows.bat packinstall uploadpdb
```

### clangd Config (Windows)
```bash
.\generate_clangd_config.bat           # Release
.\generate_clangd_config.bat debug     # Debug
.\generate_clangd_config.bat debuginfo # RelWithDebInfo
```

### macOS Build
```bash
./build_release_macos.sh        # Full build
./build_release_macos.sh -d     # Deps only
./build_release_macos.sh -s     # Slicer only
./build_release_macos.sh -x     # Use Ninja
./build_release_macos.sh -s -g  # Build + upload dSYM to Sentry
```

### Linux Build
```bash
./build_linux.sh -u             # Install system deps (requires sudo)
./build_linux.sh -d             # Deps only
./build_linux.sh -s             # Slicer only
./build_linux.sh -dsi           # Deps + slicer + AppImage
./build_linux.sh -dsig          # Deps + slicer + AppImage + upload symbols
```

### Build Output
- Windows: `build/` or `build-dbginfo/`
- macOS / Linux: `build/`

### Sentry Debug Symbols Upload

Crash reports use Sentry Native SDK (Crashpad backend). To get symbolicated stack traces, upload debug symbols at build time:

| Platform | Flag | Uploads |
|----------|------|---------|
| Windows | `uploadpdb` | `.pdb` from `build\src\Release\` and `deps\...\usr\local\` |
| macOS | `-g` | `.dSYM` from `build/<arch>/src/<config>/` |
| Linux | `-g` | ELF debug info from `build/src/<config>/` |

The upload script (`scripts/upload_sentry_pdbs.py`) auto-downloads sentry-cli on first run (cached in `tools/sentry-cli/`, gitignored). Requires `SENTRY_AUTH_TOKEN` in `.env`.

### Testing
- Test on target platform before committing.
- Run the application to verify changes manually.
- Use debug builds to check for memory leaks.
- Cross-platform testing required if core logic is changed.

---

## Dev Log

When the user explicitly says "记录" (record), create or update a development log entry.

### Location & Naming
- Directory: `dev_log/`
- **Major features / refactors**: create `module-name.md`
- **Daily fixes / optimizations / small changes**: append to `YYYY-MM-DD.md`
- One topic = one file (don't create `test-checklist.md`, `final-status.md`, etc. for the same feature)

### Content Requirements
**Required**: date, requirement (1-2 sentences), implementation approach (3-5 bullet points), new/modified files, status
**Optional**: fixes, items to test, notes, completed TODOs, pending TODOs (for larger features)

### Forbidden
- Detailed code snippets, full architecture diagrams, detailed test steps
- Auto-recording without explicit user trigger ("记录")
