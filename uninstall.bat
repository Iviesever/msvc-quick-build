@echo off
set "SCRIPT_DIR=%~dp0"

where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo [MQB] Windows PowerShell was not found.
    pause
    exit /b 1
)

if exist "%SCRIPT_DIR%uninstall.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%uninstall.ps1" %*
) else if exist "%SCRIPT_DIR%install.ps1" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install.ps1" -Action Uninstall %*
) else (
    echo [MQB] Neither uninstall.ps1 nor install.ps1 was found.
    pause
    exit /b 1
)

set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo [MQB] Uninstallation failed with exit code %RC%.
    pause
    exit /b %RC%
)

echo [MQB] Uninstallation succeeded.
pause
exit /b 0
