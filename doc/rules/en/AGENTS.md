# ElegooSlicer

Open-source 3D slicer, forked from OrcaSlicer (Bambu Studio → PrusaSlicer → Slic3r).
C++17, wxWidgets GUI, CMake build system. Modular architecture with separate libraries
for core slicing, GUI, and platform-specific code.

## Tech Stack

- C++17 (+ selective C++20), Objective-C++ (macOS), Python/Batch/Shell (scripts)
- wxWidgets 3.2+, ImGui for 3D viewport, OpenGL rendering
- Intel TBB + std::thread for parallelism
- CMake >= 3.13 (Windows: <= 3.31.x recommended)
- Dependencies built separately in `deps/`, linked via `CMAKE_PREFIX_PATH`

## Directory Layout

| Directory | Purpose |
|---|---|
| `src/libslic3r/` | Core slicing engine (platform-agnostic): geometry, G-code, infill, supports, file I/O, SLA |
| `src/libslic3r/GCode/` | G-code generation, cooling, pressure advance, thumbnails |
| `src/libslic3r/Fill/` | Infill patterns (gyroid, honeycomb, lightning, etc.) |
| `src/libslic3r/Support/` | Tree supports and traditional support generation |
| `src/libslic3r/Geometry/` | Advanced geometry, Voronoi diagrams, medial axis |
| `src/libslic3r/Format/` | File I/O (3MF, AMF, STL, OBJ, STEP) |
| `src/libslic3r/Arachne/` | Variable-width perimeter via skeleton trapezoidation |
| `src/slic3r/GUI/` | wxWidgets GUI application |
| `src/ElegooSlicer.cpp` | Application entry point |
| `src/libslic3r/Print.cpp` | Slicing pipeline orchestration |
| `src/libslic3r/PrintConfig.cpp` | All print/printer/material setting definitions |
| `resources/profiles/` | Printer & material JSON profiles by manufacturer |
| `resources/web/` | Embedded web UI (home page, guides, printer management) |
| `resources/i18n/` | Runtime language resources |
| `localization/` | i18n translation files (.pot, .po) |
| `tests/` | Test suites (libslic3r, fff_print, sla_print) |

### Key Algorithms

- **Arachne**: variable-width perimeter via skeleton trapezoidation
- **Tree supports**: organic support generation
- **Lightning infill**: sparse internal infill optimization
- **Adaptive slicing**: geometry-based variable layer height
- **Multi-material**: multi-extruder and soluble support handling

### External Dependencies

Clipper2, libigl, TBB, wxWidgets, OpenGL, CGAL, OpenVDB, Eigen, nlohmann/json,
curl, OpenSSL, imgui, boost

## Build & Test

When the user mentions build, compile, test, CMake, Sentry, or related topics → read `.ai_rules/BUILD.md`

## Critical Constraints

- **Backward compatible**: .3mf files and printer profiles must load in older versions
- **Cross-platform**: all changes must work on Windows, macOS, and Linux
- **Profile/format changes** need version migration handling
- Match existing file style when editing legacy code — don't reformat or "improve" adjacent code

## Coding Standards

> This is a legacy codebase. When modifying existing files, match the file's prevailing style.
> New code follows these rules.

### Naming

- Classes/structs/enums: `PascalCase`; functions/methods/variables: `camelCase` (verbs for functions)
- Members: `m` + PascalCase (`mPrinterList`); globals: `g` + PascalCase; statics: `s` + PascalCase
- Constants/macros: `ALL_CAPS`; booleans: verb prefix (`isReady`, `hasError`, `canUpdate`)
- Headers: `#pragma once`; prefer forward declarations

### Includes

Order (blank line between groups): paired header → project headers → third-party → standard library.

### Functions & Classes

- Single responsibility; names describe purpose
- Pass scalars/small objects by value, large objects by `const T&`, mutable by `T&`
- Prefer returning values (struct/`tuple`/`optional`/`expected`) over output parameters
- Raw pointers = "nullable, no ownership"; ownership transfer uses `unique_ptr`/`shared_ptr`
- Prefer composition over inheritance; use `const` wherever possible; `constexpr` where applicable
- Members private with getters/setters; non-mutating methods marked `const`

### Memory & Error Handling

- RAII: avoid raw `new`/`delete` (except GUI controls and third-party interfaces)
- Don't repeatedly allocate/deallocate in loops — reuse or pre-allocate
- Expected failures: `optional`/`expected`/error codes; unrecoverable: standard exceptions with context
- Validate inputs at function boundaries

### Modern C++

- `auto` only for complex iterators/lambdas; explicit types for primitives
- Prefer C++17 features (range-for, structured bindings), `enum class`, `nullptr`
- Avoid C-style casts; use `static_cast`/`dynamic_cast`
- Use `<algorithm>` over raw loops; prefer stack objects over heap allocation
- Avoid globals; use singletons sparingly; separate interface from implementation

### Concurrency

- UI updates on main thread ONLY; background threads use `CallAfter`/event queue
- `thread`/`mutex`/`lock_guard`/`atomic` for thread safety
- Cross-thread data: prefer value copy/move over shared pointers

### Comments

- Code, comments, commit messages in **English**
- Comments explain **why**, not what — don't restate the code
- Comments describe only objective info: code intent, business constraints, resource ownership, threading constraints. Never include collaboration background ("per AI discussion", "user requested", chat logs)
- Bug fixes: `// Fix: <reason> (ISSUE-ID)`
- Public API: Doxygen (`/** @brief ... @param ... @return ... */`)
- TODO: `// TODO(owner|ISSUE-ID): description`

### Avoid

- `using namespace std` in headers
- Implementing non-template functions in headers
- C-style strings/arrays — use `std::string`/`std::vector`
- Ignoring compiler warnings — fix them

## Git

When the user mentions git, commit, push, merge, rebase, branch, or related topics → read `.ai_rules/GIT.md`

## AI Interaction Rules

- **Conversation**: respond in the same language the user uses
- **Code, comments, commit messages**: English

### Task Classification

Before starting, classify the task:

| Tier | Scope | Workflow |
|---|---|---|
| **Trivial** | Single location, obvious fix (typo, rename, add getter) | Do it → explain after |
| **Standard** | Single module, clear requirement | Analyze → implement → self-review |
| **Complex** | Multi-file, cross-module, architecture change | Full analysis → confirm with user → implement → self-review |

When in doubt, treat as the higher tier.

### Standard Workflow

**1. Analyze** — State before editing: what, where (verify with tools, don't guess), how.
Present as a concise plan with file:line references.

**2. Implement** — Read current file state first. Edit surgically, prefer adding over rewriting.
If implementation reveals the plan was wrong: stop and re-analyze.

**3. Self-Review** — Check: plan match, orphan code (unused includes/vars/funcs), style consistency,
build risk. Fix issues, then summarize.

### Complex Workflow (additions)

- **Before analyzing**: list all affected files/modules, identify cross-module risks, get user confirmation
- **Enhanced analysis**: trace full call chain, map data flow, identify side effects
- **Enhanced review**: per-file summary, then overall summary

### Trivial Workflow

Just do it. One-line summary after.

### Code Style Rules

- Minimize changes: modify only what's necessary
- Don't generate docs or tests unless explicitly asked
- Match existing file style even if you'd do it differently
- Don't "improve" adjacent code, comments, or formatting
- Don't refactor things that aren't broken
- If you notice dead code: mention it, don't delete it
- When your changes create orphans: remove only what YOUR changes made unused

### Simplicity

- No features beyond what was asked
- No abstractions for single-use code
- No unrequested flexibility or configurability
- No error handling for impossible scenarios
- If it can be shorter without losing clarity, make it shorter

### Dangerous Operations — Confirm First

- Deleting files or large code blocks
- Changing core architecture (class hierarchies, module boundaries)
- Batch renaming affecting 5+ locations
- Changing build configuration (CMakeLists.txt, build scripts)
- Changing third-party dependencies
- Full-file reformatting

### Interaction Style

- When raising questions or issues, present options for quick decision-making
- For complex requirements, create a task list with affected files/modules, priorities, and dependencies
- When the user points out an error, fix it immediately