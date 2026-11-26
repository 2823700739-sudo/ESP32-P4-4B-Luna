@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0pc-agent\setup-luna.ps1" -WeatherOnly
set "LUNA_EXIT=%ERRORLEVEL%"
echo.
pause
exit /b %LUNA_EXIT%
