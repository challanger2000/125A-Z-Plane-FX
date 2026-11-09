@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
if not exist "Tools\Run-Local-Tests.ps1" (
 echo [FEHLER] Testskript fehlt. ZIP zuerst komplett entpacken.
 pause
 exit /b 2
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Run-Local-Tests.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
echo 125A Z-Plane FX Test beendet. Exit-Code %RESULT%.
if /I not "%~1"=="-CI" pause
exit /b %RESULT%
