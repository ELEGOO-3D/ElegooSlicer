# Build and Test

## Build Commands

### Windows (primary)
```bash
build_release_windows.bat              # Full release
build_release_windows.bat debug        # Debug
build_release_windows.bat debuginfo    # RelWithDebInfo
build_release_windows.bat onlydeps     # Dependencies only
build_release_windows.bat slicer       # Slicer only
build_release_windows.bat packinstall  # Build + package installer
```

### macOS
```bash
./build_release_macos.sh          # Full build (deps + slicer)
./build_release_macos.sh -d       # Deps only
./build_release_macos.sh -s       # Slicer only
./build_release_macos.sh -x       # Use Ninja
```

### Linux
```bash
sudo ./BuildLinux.sh -u           # Update system deps
./BuildLinux.sh -d                # Deps only
./BuildLinux.sh -s                # Slicer only
./BuildLinux.sh -dsi              # Deps + slicer + AppImage
```

### clangd Config (Windows)
```bash
generate_clangd_config.bat           # Release
generate_clangd_config.bat debug     # Debug
generate_clangd_config.bat debuginfo # RelWithDebInfo
```

## Build Output
- Windows: `build/` or `build-dbginfo/`
- macOS / Linux: `build/`

## Build Options Reference

**Windows**: `debug`, `debuginfo`, `onlydeps`, `slicer`, `pack`, `packinstall`, `onlypack`, `dlweb`, `test`, `sign`

**macOS**: `-d` (deps), `-s` (slicer), `-x` (Ninja), `-a <arch>`, `-t <version>`, `-c <config>`, `-p` (pack), `-e` (test env), `-w` (web deps), `-b` (skip cmake), `-1` (single core), `-n` (nightly)

**Linux**: `-u` (update deps, sudo), `-d` (deps), `-s` (slicer), `-i` (AppImage), `-r` (skip checks), `-c` (clean), `-1` (single core), `-b` (debug)

## Testing

```bash
cd build && ctest --output-on-failure
ctest --test-dir ./tests/libslic3r
ctest --test-dir ./tests/fff_print
```

## Sentry Debug Symbols

Crash reports use Sentry Native SDK (Crashpad backend). Upload debug symbols at build time:

| Platform | Flag | Uploads |
|---|---|---|
| Windows | `uploadpdb` | `.pdb` from `build\src\Release\` and `deps\...\usr\local\` |
| macOS | `-g` | `.dSYM` from `build/<arch>/src/<config>/` |
| Linux | `-g` | ELF debug info from `build/src/<config>/` |

Upload script (`scripts/upload_sentry_pdbs.py`) auto-downloads sentry-cli on first run
(cached in `tools/sentry-cli/`, gitignored). Requires `SENTRY_AUTH_TOKEN` in `.env`.
