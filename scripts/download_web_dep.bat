@echo off
setlocal enabledelayedexpansion

set GIT_TOKEN=
set LAN_WEB_URL=https://github.com/ELEGOO-3D/elegoo-fdm-web/releases/download/v2.0.0/lan_service_web.zip
set CLOUD_WEB_URL=https://github.com/ELEGOO-3D/elegoo-fdm-web/releases/download/v2.0.0/cloud_service_web.zip
set LAN_WEB_NAME=lan_service_web
set CLOUD_WEB_NAME=cloud_service_web

REM Check parameter and set env file
set ENV_FILE=.env
if /i "%1"=="test" (
    set ENV_FILE=.env.testing
    echo Using testing environment
) else (
    echo Using default environment
)

echo read env from %ENV_FILE%
if not exist "%ENV_FILE%" (
    echo ERROR: %ENV_FILE% file not found. Exiting.
    exit /b 1
)

REM Parse env file, skip empty lines and comments
for /f "usebackq tokens=1,* delims==" %%a in ("%ENV_FILE%") do (
    REM Skip lines starting with # comments
    set "line=%%a"
    if not "!line!"=="" (
        if not "!line:~0,1!"=="#" (
            REM Remove inline comments from value
            set "value=%%b"
            for /f "tokens=1 delims=#" %%c in ("!value!") do set "%%a=%%c"
        )
    )
)

echo download web dependencies
set LAN_WEB_PATH=resources\plugins\elegoolink\web\%LAN_WEB_NAME%
set CLOUD_WEB_PATH=resources\plugins\elegoolink\web\%CLOUD_WEB_NAME%

REM Check if LAN_WEB_URL is empty
if "%LAN_WEB_URL%"=="" (
    echo WARNING: LAN_WEB_URL is empty, skipping LAN web download.
    goto skip_lan_download
)

echo Download %LAN_WEB_URL%
echo.

curl -f -L -H "Authorization: Bearer %GIT_TOKEN%" -o "%LAN_WEB_NAME%.zip" "%LAN_WEB_URL%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Download LAN web failed. HTTP error or network issue.
    exit /b 1
)

REM Check if file size is reasonable (should be larger than 1KB)
for %%A in ("%LAN_WEB_NAME%.zip") do set FILE_SIZE=%%~zA
if !FILE_SIZE! lss 1024 (
    echo ERROR: Downloaded file is too small ^(!FILE_SIZE! bytes^). Download may have failed.
    del "%LAN_WEB_NAME%.zip" 2>nul
    exit /b 1
)

echo Download completed: !FILE_SIZE! bytes
echo Extract %LAN_WEB_NAME%.zip

if exist "%LAN_WEB_PATH%" (
    REM Delete the folder and all its contents
    rd /s /q "%LAN_WEB_PATH%"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to delete %LAN_WEB_PATH%. Exiting.
        exit /b !ERRORLEVEL!
    )
    echo Folder %LAN_WEB_PATH% and all its contents have been deleted.
) else (
    echo Folder %LAN_WEB_PATH% does not exist.
)

powershell -Command "$ErrorActionPreference='Stop'; try { Expand-Archive -Path '%LAN_WEB_NAME%.zip' -DestinationPath '%LAN_WEB_PATH%' -Force; exit 0 } catch { Write-Host $_.Exception.Message; exit 1 }"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to extract %LAN_WEB_NAME%.zip. The file may be corrupted.
    del "%LAN_WEB_NAME%.zip" 2>nul
    exit /b 1
)

del "%LAN_WEB_NAME%.zip"
if !ERRORLEVEL! neq 0 (
    echo WARNING: Failed to delete %LAN_WEB_NAME%.zip
)

:skip_lan_download

REM Check if CLOUD_WEB_URL is empty
if "%CLOUD_WEB_URL%"=="" (
    echo WARNING: CLOUD_WEB_URL is empty, skipping CLOUD web download.
    goto skip_cloud_download
)

echo Download %CLOUD_WEB_URL%
echo.

curl -f -L -H "Authorization: Bearer %GIT_TOKEN%" -o "%CLOUD_WEB_NAME%.zip" "%CLOUD_WEB_URL%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Download CLOUD web failed. HTTP error or network issue.
    exit /b 1
)

REM Check if file size is reasonable (should be larger than 1KB)
for %%A in ("%CLOUD_WEB_NAME%.zip") do set FILE_SIZE=%%~zA
if !FILE_SIZE! lss 1024 (
    echo ERROR: Downloaded file is too small ^(!FILE_SIZE! bytes^). Download may have failed.
    del "%CLOUD_WEB_NAME%.zip" 2>nul
    exit /b 1
)

echo Download completed: !FILE_SIZE! bytes
echo Extract %CLOUD_WEB_NAME%.zip
if exist "%CLOUD_WEB_PATH%" (
    REM Delete the folder and all its contents
    rd /s /q "%CLOUD_WEB_PATH%"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to delete %CLOUD_WEB_PATH%. Exiting.
        exit /b !ERRORLEVEL!
    )
    echo Folder %CLOUD_WEB_PATH% and all its contents have been deleted.
) else (
    echo Folder %CLOUD_WEB_PATH% does not exist.
)

powershell -Command "$ErrorActionPreference='Stop'; try { Expand-Archive -Path '%CLOUD_WEB_NAME%.zip' -DestinationPath '%CLOUD_WEB_PATH%' -Force; exit 0 } catch { Write-Host $_.Exception.Message; exit 1 }"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Failed to extract %CLOUD_WEB_NAME%.zip. The file may be corrupted.
    del "%CLOUD_WEB_NAME%.zip" 2>nul
    exit /b 1
)

del "%CLOUD_WEB_NAME%.zip"
if !ERRORLEVEL! neq 0 (
    echo WARNING: Failed to delete %CLOUD_WEB_NAME%.zip
)

:skip_cloud_download

echo All downloads and extractions completed successfully!
exit /b 0

