@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0pc-agent\setup-luna.ps1"
set "LUNA_EXIT=%ERRORLEVEL%"
echo.
if not "%LUNA_EXIT%"=="0" echo Luna did not start. Read the message above before retrying.
pause
exit /b %LUNA_EXIT%
