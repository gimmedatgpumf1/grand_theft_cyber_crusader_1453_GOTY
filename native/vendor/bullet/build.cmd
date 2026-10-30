@echo off
setlocal EnableExtensions
cd /d "%~dp0..\..\.."
if not exist "build\obj\bullet" mkdir "build\obj\bullet"
(for /f "usebackq delims=" %%S in ("native\vendor\bullet\sources.txt") do @echo "native\vendor\bullet\src\%%S") > "build\obj\bullet\sources.rsp"
cl.exe /nologo /MP /std:c++17 /EHsc /O2 /DNDEBUG /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"native\vendor\bullet\src" /c /Fo"build\obj\bullet\\" @"build\obj\bullet\sources.rsp"
if errorlevel 1 exit /b 1
(for /f "usebackq delims=" %%S in ("native\vendor\bullet\sources.txt") do @echo "build\obj\bullet\%%~nS.obj") > "build\obj\bullet\objects.rsp"
lib.exe /nologo /OUT:"build\obj\bullet\bullet.lib" @"build\obj\bullet\objects.rsp"
if errorlevel 1 exit /b 1
exit /b 0
