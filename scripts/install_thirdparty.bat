@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_thirdparty.ps1" %*
exit /b %errorlevel%
