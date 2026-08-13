@echo off
setlocal
set "SCRIPT_DIR=%~dp0"

where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo [MQB] Windows PowerShell was not found.
    pause
    exit /b 1
)

if not exist "%SCRIPT_DIR%install.ps1" (
    echo [MQB] install.ps1 was not found next to install.bat.
    pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install.ps1" -Action Install %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo [MQB] Installation failed with exit code %RC%.
    pause
    exit /b %RC%
)

echo [MQB] Installation succeeded.
pause
exit /b 0
