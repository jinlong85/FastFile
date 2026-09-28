@echo off
rem FastFile installer entry point (IExpress runs this from the extraction folder).
rem Keep this file ASCII-only: cmd.exe reads it in the OEM code page.
setlocal
set "SCRIPT=%~dp0install.ps1"
if not exist "%SCRIPT%" (
  echo [ERROR] install.ps1 not found next to install.cmd
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" %*
exit /b %ERRORLEVEL%
