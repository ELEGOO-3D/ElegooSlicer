# Build and Test

## Build Commands

Windows Build
```bash
# Full build
build_release_windows.bat

# Debug symbols build
build_release_windows.bat debug

# Build with debug info
build_release_windows.bat debuginfo

# Build dependencies only
build_release_windows.bat onlydeps

# Build slicer only (after dependencies built)
build_release_windows.bat slicer

# Build slicer and package installer
build_release_windows.bat packinstall

# Package installer only (no build)
build_release_windows.bat onlypack

# Package dependencies
build_release_windows.bat pack

# Download web dependencies
build_release_windows.bat dlweb

# Test environment
build_release_windows.bat test
```

macOS Build
```bash
# Full build (dependencies and slicer)
./build_release_macos.sh

# Build dependencies only
./build_release_macos.sh -d

# Build slicer only (after dependencies built)
./build_release_macos.sh -s

# Use Ninja generator (faster build)
./build_release_macos.sh -x

# Specify architecture build
./build_release_macos.sh -a arm64    # or x86_64 or universal

# Specify macOS target version
./build_release_macos.sh -t 11.3

# Build configuration (Debug or Release)
./build_release_macos.sh -c Debug
```

Linux Build
```bash
# Full build (includes dependency updates, requires sudo)
sudo ./BuildLinux.sh -u

# Build dependencies only
./BuildLinux.sh -d

# Build slicer only
./BuildLinux.sh -s

# Generate AppImage
./BuildLinux.sh -i

# Skip memory/disk check
./BuildLinux.sh -r

# Combined command example: build dependencies, slicer and generate AppImage
./BuildLinux.sh -dsi
```

## Development Environment Configuration

clangd Configuration (Windows)
```bash
# Generate compile_commands.json and configure clangd
generate_clangd_config.bat

# Generate Debug mode configuration
generate_clangd_config.bat debug

# Generate RelWithDebInfo mode configuration
generate_clangd_config.bat debuginfo

# With internal test macro definitions
generate_clangd_config.bat test
```

## Testing Guidelines

- Test on target platform before committing
- Run application to verify changes
- Use debug build to check for memory leaks
- Cross-platform testing required if changes affect core logic

## Build Output

- Windows: `build/` or `build-dbginfo/`
- macOS: `build/`
- Linux: `build/`

Executable location varies by platform - check build script output.

## Common Build Options

Windows
- `debug` - Debug mode
- `debuginfo` - RelWithDebInfo mode (with debug symbols)
- `onlydeps` - Build dependencies only
- `slicer` - Build slicer only
- `pack` - Package dependencies
- `packinstall` - Build and package installer
- `onlypack` - Package installer only
- `dlweb` - Download web dependencies
- `test` - Internal test version
- `sign` - Sign binary files

macOS
- `-d` - Build dependencies only
- `-s` - Build slicer only
- `-x` - Use Ninja (recommended)
- `-a <arch>` - Specify architecture
- `-t <version>` - macOS target version
- `-c <config>` - Debug or Release
- `-p` - Package dependencies
- `-e` - Test environment
- `-w` - Download web dependencies
- `-b` - Build only (skip CMake configuration)
- `-1` - Single-core build
- `-n` - Nightly build

Linux
- `-u` - Update dependencies (requires sudo)
- `-d` - Build dependencies only
- `-s` - Build slicer only
- `-i` - Generate AppImage
- `-r` - Skip memory/disk check
- `-c` - Force clean build
- `-1` - Single-core build
- `-b` - Debug mode build
