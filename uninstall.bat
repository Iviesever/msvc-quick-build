@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "MQB_SHOULD_PAUSE=1"
if /I "%MQB_NO_PAUSE%"=="1" set "MQB_SHOULD_PAUSE=0"

where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo [MQB] Windows PowerShell was not found.
    if "%MQB_SHOULD_PAUSE%"=="1" pause
    exit /b 1
)

if exist "%SCRIPT_DIR%uninstall.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%uninstall.ps1" %*
) else if exist "%SCRIPT_DIR%uninstall-mqb.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%uninstall-mqb.ps1" %*
) else if exist "%SCRIPT_DIR%install.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install.ps1" -Action Uninstall %*
) else if exist "%SCRIPT_DIR%mqb-install.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%mqb-install.ps1" -Action Uninstall %*
) else (
    echo [MQB] No MQB uninstall engine was found next to uninstall.bat.
    if "%MQB_SHOULD_PAUSE%"=="1" pause
    exit /b 1
)

set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo [MQB] Uninstallation failed with exit code %RC%.
    if "%MQB_SHOULD_PAUSE%"=="1" pause
    exit /b %RC%
)

echo [MQB] Uninstallation succeeded.
if "%MQB_SHOULD_PAUSE%"=="1" pause
exit /b 0
