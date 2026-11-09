@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Prepare-DAW-ASIO.ps1"
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
