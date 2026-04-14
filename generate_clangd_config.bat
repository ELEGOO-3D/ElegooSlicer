@echo off
setlocal ENABLEDELAYEDEXPANSION
set WP=%CD%

rem ===========================================================================
rem Generate compile_commands.json for clangd
rem ===========================================================================

if "%1" == "help" (
    echo.
    echo ============================================================================
    echo ElegooSlicer Build Script for Windows
    echo ============================================================================
    echo.
    echo Usage: generate_clangd_config.bat [parameters]
    echo.
    echo Parameters:
    echo   debug        - Build in Debug mode
    echo   debuginfo    - Build in RelWithDebInfo mode with debug symbols
    echo   test         - Build internal testing version with ELEGOO_INTERNAL_TESTING=1
    echo.
    echo Examples:
    echo   generate_clangd_config.bat test
    echo   generate_clangd_config.bat debug
    echo   generate_clangd_config.bat debuginfo
    echo.
    echo ============================================================================
    echo.
    exit /b 0
)
echo.
echo ============================================================================
echo   Generate compile_commands.json for clangd
echo ============================================================================
echo.

rem Parse arguments
set debug=OFF
set debuginfo=OFF
set ELEGOO_INTERNAL_TESTING=0

:loop
if "%1"=="" goto end
if /I "%1"=="debug" set debug=ON
if /I "%1"=="debuginfo" set debuginfo=ON
if /I "%1"=="test" set ELEGOO_INTERNAL_TESTING=1
shift
goto loop
:end

rem Determine build type (same logic as build_release_windows.bat)
if "%debug%"=="ON" (
    set BUILD_TYPE=Debug
    set BUILD_DIR=build-ninja-dbg
    set DEPS_BUILD_DIR=build-dbg
) else (
    if "%debuginfo%"=="ON" (
        set BUILD_TYPE=RelWithDebInfo
        set BUILD_DIR=build-ninja-dbginfo
        set DEPS_BUILD_DIR=build-dbginfo
    ) else (
        set BUILD_TYPE=RelWithDebInfo
        set BUILD_DIR=build-ninja-dbginfo
        set DEPS_BUILD_DIR=build-dbginfo
    )
)

rem Auto-detect deps directory if standard one doesn't exist
set "DEPS_PREFIX=%WP%\deps\%DEPS_BUILD_DIR%\ElegooSlicer_dep"
if not exist "%DEPS_PREFIX%\usr\local\" (
    echo [INFO] Standard deps directory not found, searching alternatives...
    for %%d in (build build-dbginfo build-dbg) do (
        if exist "%WP%\deps\%%d\ElegooSlicer_dep\usr\local\" (
            set "DEPS_BUILD_DIR=%%d"
            set "DEPS_PREFIX=%WP%\deps\%%d\ElegooSlicer_dep"
            echo [OK] Found dependencies in: %%d
            goto found_deps
        )
    )
    echo [ERROR] Dependencies not found in any standard location
    echo         Please build dependencies first using build_release_windows.bat
    exit /b 1
)
:found_deps

rem Check dependencies
if exist "%DEPS_PREFIX%\usr\local\" (
    echo [OK] Dependencies found
) else (
    echo [ERROR] Dependencies not found at "%DEPS_PREFIX%\usr\local"
    echo         Please build dependencies first using build_release_windows.bat
    exit /b 1
)

rem Check Ninja
where ninja >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Ninja not found in PATH. Please install Ninja.
    echo         https://github.com/ninja-build/ninja/releases
    exit /b 1
)

rem Find and setup Visual Studio
echo [INFO] Searching for Visual Studio installation...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_PATH="

if exist "%VSWHERE%" (
    echo [INFO] Using vswhere to locate Visual Studio...
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
    
    if defined VS_PATH (
        for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do (
            echo [OK] Found Visual Studio %%v
        )
    )
) else (
    echo [INFO] vswhere not found, searching standard locations...
    rem Try to find any VS version (2026, 2022, 2019, etc.)
    for %%y in (2026 2025 2024 2023 2022 2019) do (
        if not defined VS_PATH (
            for %%e in (Enterprise Professional Community) do (
                if not defined VS_PATH (
                    if exist "%ProgramFiles%\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                        set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\%%y\%%e"
                        echo [OK] Found Visual Studio %%y %%e
                    )
                )
            )
        )
    )
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio not found.
    echo.
    echo Please install Visual Studio 2019 or later with C++ development tools.
    echo Download: https://visualstudio.microsoft.com/
    exit /b 1
)

set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found at: %VCVARS%
    echo Please ensure C++ development tools are installed.
    exit /b 1
)

echo [INFO] Setting up MSVC environment from:
echo        %VS_PATH%
call "%VCVARS%" >nul 2>&1

where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] MSVC compiler setup failed.
    exit /b 1
)

echo [OK] MSVC compiler ready
echo.
echo ============================================================================
echo                      BUILD CONFIGURATION
echo ============================================================================
echo   Build Type:          %BUILD_TYPE%
echo   Build Directory:     %BUILD_DIR%
echo   Dependencies Dir:    %DEPS_BUILD_DIR%
echo   Internal Testing:    %ELEGOO_INTERNAL_TESTING%
echo ============================================================================
echo.

rem Run CMake
if "%BUILD_TYPE%"=="Debug" (
    set ENABLE_DEBUG_OUTPUT=1
) else if "%BUILD_TYPE%"=="RelWithDebInfo" (
    set ENABLE_DEBUG_OUTPUT=1
) else (
    set ENABLE_DEBUG_OUTPUT=0
)

rem Run CMake
cmake -S "%WP%" -B "%BUILD_DIR%" -G "Ninja" ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="%DEPS_PREFIX%\usr\local" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -DBBL_RELEASE_TO_PUBLIC=1 ^
    -DELEGOO_INTERNAL_TESTING=%ELEGOO_INTERNAL_TESTING% ^
    -DELEGOO_ENABLE_DEBUG_OUTPUT=%ENABLE_DEBUG_OUTPUT% ^
    -DCMAKE_INSTALL_PREFIX="%BUILD_DIR%\ElegooSlicer" ^
    -Wno-dev

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

rem Copy compile_commands.json
if exist "%BUILD_DIR%\compile_commands.json" (
    copy /Y "%BUILD_DIR%\compile_commands.json" "%WP%\compile_commands.json" >nul
    if errorlevel 1 (
        echo.
        echo [ERROR] Failed to copy compile_commands.json
        exit /b 1
    )
    echo.
    echo ============================================================================
    echo                     BUILD COMPLETED SUCCESSFULLY!
    echo ============================================================================
    echo [OK] compile_commands.json generated for clangd!
    echo [OK] Full path: %WP%\compile_commands.json
    echo ============================================================================
    echo.
    
    rem Setup clangd configuration using Python script
    echo [INFO] Setting up clangd configuration...
    python "%WP%\scripts\setup_clangd_config.py" "%WP%"
    if errorlevel 1 (
        echo [WARNING] Some configuration files may not be copied correctly
    )
    
    echo.
    echo ============================================================================
    echo                          SETUP COMPLETE!
    echo ============================================================================
    echo.
    echo Next steps:
    echo   1. Reload VSCode/Cursor window (Ctrl+Shift+P ^> "Reload Window")
    echo   2. Wait for clangd to index the project (check status bar)
    echo   3. Enjoy fast code navigation!
    echo.
    echo Note: If clangd extension is not installed, please install it from:
    echo       Extensions ^> Search "clangd" ^> Install "clangd" by LLVM
    echo.
    exit /b 0
) else (
    echo.
    echo [ERROR] compile_commands.json not generated
    exit /b 1
)

