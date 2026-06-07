@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_s3_fast.ps1" %*
exit /b %ERRORLEVEL%
