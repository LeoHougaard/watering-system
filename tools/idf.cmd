@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0idf.ps1" %*
exit /b %ERRORLEVEL%
