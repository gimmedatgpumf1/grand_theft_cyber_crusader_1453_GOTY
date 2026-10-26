@echo off
setlocal
cd /d "%~dp0"
call "%~dp0build.cmd" --run
exit /b %ERRORLEVEL%
