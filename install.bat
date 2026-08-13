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

if not exist "%SCRIPT_DIR%install.ps1" (
    echo [MQB] install.ps1 was not found next to install.bat.
    if "%MQB_SHOULD_PAUSE%"=="1" pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install.ps1" -Action Install %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo [MQB] Installation failed with exit code %RC%.
    if "%MQB_SHOULD_PAUSE%"=="1" pause
    exit /b %RC%
)

echo [MQB] Installation succeeded.
if "%MQB_SHOULD_PAUSE%"=="1" pause
exit /b 0
