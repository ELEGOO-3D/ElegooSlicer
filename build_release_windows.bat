@echo off
set WP=%CD%

set "VS_GENERATOR=Visual Studio 17 2022"
set "PACK_SUFFIX=vs2022"

if not defined WindowsSdkDir (
    call scripts/get_windows_sdk.bat
)

echo WindowsSdkDir=%WindowsSdkDir%
echo WindowsSDKVersion=%WindowsSDKVersion%

if "%1" == "help" (
    echo.
    echo ============================================================================
    echo ElegooSlicer Build Script for Windows
    echo ============================================================================
    echo.
    echo Usage: build_release_windows.bat [parameters]
    echo.
    echo Parameters:
    echo   slicer       - Only compile the slicer, skip dependencies build
    echo   onlydeps    - Only build dependencies, skip slicer build
    echo   packinstall  - Build slicer and create installer package
    echo   onlypack     - Only create installer package, skip build
    echo   sign         - Sign the executable with digital signature
    echo   test         - Build internal testing version with ELEGOO_INTERNAL_TESTING=1
    echo   debug        - Build in Debug mode
    echo   debuginfo    - Build in RelWithDebInfo mode with debug symbols
    echo   pack         - Pack dependencies into zip file
    echo   dlweb        - Download web dependencies default: skip download
    echo   vs2019       - Select Visual Studio vs2019 toolchain
    echo   vs2026       - Select Visual Studio vs2026 toolchain default is vs2022
    echo.
    echo Examples:
    echo   build_release_windows.bat slicer
    echo   build_release_windows.bat vs2019 packinstall sign
    echo   build_release_windows.bat test slicer
    echo   build_release_windows.bat onlydeps
    echo.
    echo ============================================================================
    echo.
    goto end
)

@REM Pack deps
if "%1"=="pack" (
    echo.
    echo ============================================================================
    echo                     PACKING DEPENDENCIES
    echo ============================================================================
    setlocal ENABLEDELAYEDEXPANSION 
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    set PACK_NAME=ElegooSlicer_dep_win64_!build_date!_%PACK_SUFFIX%.zip
    
    echo [INFO] Creating dependency package: !PACK_NAME!
    echo [INFO] Compressing with 7-Zip...
    echo.

    %WP%/tools/7z.exe a !PACK_NAME! ElegooSlicer_dep
    
    if !ERRORLEVEL! neq 0 (
        echo.
        echo [ERROR] Failed to pack dependencies
        goto error_end
    )
    
    echo.
    echo [OK] Dependencies packed successfully: !PACK_NAME!
    echo ============================================================================
    echo.
    goto end
)

set debug=OFF
set debuginfo=OFF
set only_slicer=OFF
set pack_install=OFF
set only_pack=OFF
set only_deps=OFF
set sign=OFF
set dlweb=OFF
set ELEGOO_INTERNAL_TESTING=0

set count=1
:loop
if "%1"=="" goto args_end
if /I "%1"=="vs2019" (
    set "VS_GENERATOR=Visual Studio 16 2019"
    set "PACK_SUFFIX=vs2019"
    shift
    goto loop
)
if /I "%1"=="vs2026" (
    set "VS_GENERATOR=Visual Studio 17 2022"
    set "PACK_SUFFIX=vs2026"
    shift
    goto loop
)
if "%1"=="debug" set debug=ON
if "%1"=="debuginfo" set debuginfo=ON
if "%1"=="slicer" set only_slicer=ON
if "%1"=="packinstall" set pack_install=ON
if "%1"=="onlypack" set only_pack=ON
if "%1"=="onlydeps" set only_deps=ON
if "%1"=="sign" set sign=ON
if "%1"=="dlweb" set dlweb=ON
if "%1"=="test" set ELEGOO_INTERNAL_TESTING=1
set /a count+=1
shift
goto loop
:args_end


echo.
echo ============================================================================
echo                      BUILD CONFIGURATION
echo ============================================================================
echo   VS Toolchain:        %VS_GENERATOR%
echo   Debug Mode:          %debug%
echo   Debug Info:          %debuginfo%
echo   Only Slicer:         %only_slicer%
echo   Pack Install:        %pack_install%
echo   Only Pack:           %only_pack%
echo   Only Deps:           %only_deps%
echo   Sign Binaries:       %sign%
echo   Download Web Deps:   %dlweb%
echo   Internal Testing:    %ELEGOO_INTERNAL_TESTING%
echo ============================================================================
echo.



@REM Determine build type
if "%debug%"=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
    set enable_debug_output=1
) else (
    if "%debuginfo%"=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
        set enable_debug_output=1
    ) else (
        set build_type=Release
        set build_dir=build
        set enable_debug_output=0
    )
)
echo [INFO] Build Type: %build_type%
echo [INFO] Build Directory: %build_dir%
echo [INFO] Enable Debug Output: %enable_debug_output%
echo.


@REM Clean previous build
set folderPath=".\%build_dir%\ElegooSlicer"
if exist "%folderPath%" (
    echo [INFO] Cleaning previous build folder: %folderPath%
    rd /s /q "%folderPath%"
    echo [OK] Previous build folder cleaned successfully
) else (
    echo [INFO] No previous build folder found, skipping cleanup
)


if "%dlweb%"=="ON" (
    setlocal enabledelayedexpansion
    echo ----------------------------------------------------------------------------
    echo                     Downloading Web Dependencies
    echo ----------------------------------------------------------------------------
    if "%ELEGOO_INTERNAL_TESTING%"=="1" (
        set TEST_PARAM=test
        echo [INFO] Downloading INTERNAL TESTING web dependencies...
    ) else (
        set TEST_PARAM=
        echo [INFO] Downloading RELEASE web dependencies...
    )
    echo.

    call scripts/download_web_dep.bat !TEST_PARAM!
    if !ERRORLEVEL! neq 0 (
        echo.
        echo [ERROR] Download web dependencies failed. Exiting.
        endlocal
        goto error_end
    )
    echo.
    echo [OK] Web dependencies downloaded successfully
    echo ----------------------------------------------------------------------------
    echo.
    endlocal
) else (
    echo.
    echo [INFO] Skipping web dependencies download use 'dlweb' parameter to enable
    echo.
)


setlocal DISABLEDELAYEDEXPANSION 
cd deps
mkdir %build_dir% 2>nul
cd %build_dir%
set DEPS=%CD%/ElegooSlicer_dep

@REM Check for skip conditions
if "%only_pack%"=="ON" (
    echo.
    echo [INFO] Only Pack mode - Skipping build, jumping to installer creation
    echo.
    GOTO :pack_install
)

if "%only_slicer%"=="ON" (
    echo.
    echo [INFO] Only Slicer mode - Skipping dependencies build
    echo.
    GOTO :slicer
)

if "%pack_install%"=="ON" (
    echo.
    echo [INFO] Pack Install mode - Will build dependencies, slicer, and create installer
    echo.
)

echo.
echo ============================================================================
echo                    STAGE 1: BUILDING DEPENDENCIES
echo ============================================================================
echo [INFO] Dependencies will be installed to: %DEPS%
echo [INFO] Configuring dependencies with CMake...
echo.

@echo on
cmake ../ -G "%VS_GENERATOR%" -A x64 -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo% -DELEGOO_INTERNAL_TESTING=%ELEGOO_INTERNAL_TESTING%
cmake --build . --config %build_type% --target deps -- -m
@echo off

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Dependencies build failed!
    goto error_end
)

echo.
echo [OK] Dependencies built successfully
echo.

if "%only_deps%"=="ON" (
    echo.
    echo [INFO] Only Deps mode - Build complete, exiting
    echo.
    goto end
)

:slicer
echo.
echo ============================================================================
echo                    STAGE 2: BUILDING ELEGOOSLICER
echo ============================================================================
echo [INFO] Configuring ElegooSlicer with CMake...
echo.

cd %WP%
mkdir %build_dir% 2>nul
cd %build_dir%

@echo on
cmake .. -G "%VS_GENERATOR%" -A x64 -DELEGOO_INTERNAL_TESTING=%ELEGOO_INTERNAL_TESTING%  -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./ElegooSlicer" -DCMAKE_BUILD_TYPE=%build_type% -DELEGOO_ENABLE_DEBUG_OUTPUT=%enable_debug_output% -DWIN10SDK_PATH="%WindowsSdkDir%Include\%WindowsSDKVersion%\"
cmake --build . --config %build_type% --target ALL_BUILD -- -m
@echo off

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] ElegooSlicer build failed!
    goto error_end
)

echo.
echo [OK] ElegooSlicer built successfully
echo [INFO] Processing localization files...
echo.

cd ..
call scripts/run_gettext.bat
cd %build_dir%

echo.
echo [INFO] Installing ElegooSlicer files...
echo.

cmake --build . --target install --config %build_type%

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] ElegooSlicer installation failed!
    goto error_end
)

echo.
echo [OK] ElegooSlicer installation completed
echo.

if "%only_slicer%"=="ON" (
    echo [INFO] Only Slicer mode - Build complete, exiting
    goto end
)

@REM Check if packaging is needed
if "%pack_install%"=="OFF"  (
    echo.
    echo [INFO] Packaging disabled, build complete
    echo.
    goto end
)

:pack_install
echo.
echo ============================================================================
echo                   STAGE 3: CREATING INSTALLER PACKAGE
echo ============================================================================

setlocal enabledelayedexpansion

cd %WP%

set PACK_INSTALL_FILE=.\%build_dir%\install.ini
echo [INFO] Reading version from: %PACK_INSTALL_FILE%

for /f "tokens=1,* delims==" %%a in ('findstr "^ELEGOOSLICER_VERSION" "%PACK_INSTALL_FILE%"') do (
    set ELEGOOSLICER_VERSION=%%b
)

if "!ELEGOOSLICER_VERSION!"=="" (
    echo.
    echo [ERROR] ELEGOOSLICER_VERSION is empty in install.ini. Exiting.
    goto error_end
)

echo [INFO] Version detected: !ELEGOOSLICER_VERSION!
echo.

set INSTALL_PATH=./%build_dir%/ElegooSlicer
set INSTALL_NAME=ElegooSlicer_Windows_Installer_V!ELEGOOSLICER_VERSION!.exe
set OUTFILE=./%build_dir%/ElegooSlicer_Windows_Installer_V!ELEGOOSLICER_VERSION!.exe

echo [INFO] Installer will be created at: %OUTFILE%
echo.

cd %WP%
setlocal enabledelayedexpansion

echo [INFO] Loading environment variables from .env file...
REM Parse env file, skip empty lines and comments
for /f "usebackq tokens=1,* delims==" %%a in (".env") do (
    set "line=%%a"
    if not "!line!"=="" (
        if not "!line:~0,1!"=="#" (
            REM Remove inline comments from value
            set "value=%%b"
            for /f "tokens=1 delims=#" %%c in ("!value!") do set "%%a=%%c"
        )
    )
)
echo.

if "%sign%"=="ON" (
    echo ----------------------------------------------------------------------------
    echo                     Signing Binaries
    echo ----------------------------------------------------------------------------
    
    if "%SIGNTOOL_PATH%"=="" (
        echo [ERROR] SIGNTOOL_PATH must be set in .env file. Exiting.
        goto error_end
    )

    if "%SIGN_CONFIG_PATH%"=="" (
        echo [ERROR] SIGN_CONFIG_PATH must be set in .env file. Exiting.
        goto error_end
    )

    echo [INFO] Signing tool: %SIGNTOOL_PATH%
    echo [INFO] Config path: %SIGN_CONFIG_PATH%
    echo.
    
    cd %WP%
    cd %build_dir%
    
    echo [INFO] Signing elegoo-slicer.exe...
    %SIGNTOOL_PATH% --config %SIGN_CONFIG_PATH% --cmd sign -i .\ElegooSlicer\elegoo-slicer.exe -m 3 -r elegoo
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Failed to sign elegoo-slicer.exe. Exiting.
        goto error_end
    )
    echo [OK] elegoo-slicer.exe signed successfully
    echo.
    
    echo [INFO] Signing ElegooSlicer.dll...
    %SIGNTOOL_PATH% --config %SIGN_CONFIG_PATH% --cmd sign -i .\ElegooSlicer\ElegooSlicer.dll -m 3 -r elegoo
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Failed to sign ElegooSlicer.dll. Exiting.
        goto error_end
    )
    echo [OK] ElegooSlicer.dll signed successfully
    echo.
    echo [OK] All binaries signed successfully
    echo ----------------------------------------------------------------------------
    echo.
)

cd %WP%
echo ----------------------------------------------------------------------------
echo                     Creating Installer with NSIS
echo ----------------------------------------------------------------------------
echo [INFO] Product Version: !ELEGOOSLICER_VERSION!
echo [INFO] Install Path: !INSTALL_PATH!
echo [INFO] Output File: %OUTFILE%
echo.
echo [INFO] Running NSIS packager, this may take a moment...
echo.

"%~dp0/tools/NSIS/makensis.exe" /DPRODUCT_VERSION=!ELEGOOSLICER_VERSION! /DINSTALL_PATH=!INSTALL_PATH! /DOUTFILE=!OUTFILE!  package.nsi

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] NSIS packaging failed! Please check if package.nsi is correct.
    goto error_end
)
echo.
echo [OK] Installer created successfully
echo ----------------------------------------------------------------------------
echo.

if "%sign%"=="ON" (
    echo ----------------------------------------------------------------------------
    echo                     Signing Installer
    echo ----------------------------------------------------------------------------
    cd %WP%
    cd %build_dir%
    
    echo [INFO] Signing installer: %INSTALL_NAME%
    echo.
    
    %SIGNTOOL_PATH% --config %SIGN_CONFIG_PATH% --cmd sign -i .\%INSTALL_NAME% -m 3 -r elegoo
    if !ERRORLEVEL! neq 0 (
        echo.
        echo [ERROR] Failed to sign installer. Exiting.
        goto error_end
    )
    
    echo.
    echo [OK] Installer signed successfully
    echo ----------------------------------------------------------------------------
    echo.
)

echo.
echo ============================================================================
echo                     BUILD COMPLETED SUCCESSFULLY!
echo ============================================================================
echo [OK] Installer package created: %INSTALL_NAME%
echo [OK] Full path: %WP%\%build_dir%\%INSTALL_NAME%
echo ============================================================================
echo.

:end
exit /b 0

:error_end
if not errorlevel 1 exit /b 1
exit /b %ERRORLEVEL%
