@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%CD%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\nagger.ps1"
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\bin" mkdir "%ROOT%\build\bin"
if not exist "%ROOT%\build\obj" mkdir "%ROOT%\build\obj"
if not exist "%ROOT%\.cache\ccache" mkdir "%ROOT%\.cache\ccache"
set "CCACHE_DIR=%ROOT%\.cache\ccache"
set "CCACHE_CONFIGPATH=%ROOT%\tools\ccache.conf"

where cl.exe >nul 2>nul
if errorlevel 1 call :LocateVisualStudio
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [build] Visual Studio 2022 C++ tools were not found.
  echo [build] Install the "Desktop development with C++" workload and retry.
  exit /b 1
)

set "SOURCES="
for /r "%ROOT%\src" %%F in (*.cpp) do set "SOURCES=!SOURCES! "%%F""
if not defined SOURCES (
  echo [build] No sources found.
  exit /b 1
)
set "COMPILER=cl.exe"
where ccache.exe >nul 2>nul
if not errorlevel 1 (
  set "COMPILER=ccache.exe cl.exe"
  echo [build] ccache enabled: content hashes only, persistent local cache.
) else (
  echo [build] WARNING: ccache not found; compiling directly. Install ccache for faster rebuilds.
)

%COMPILER% /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX ^
  /I"%ROOT%\src" !SOURCES! /Fo"%ROOT%\build\obj\\" /Fe"%ROOT%\build\bin\gtc1453.exe" ^
  /link Ws2_32.lib
if errorlevel 1 exit /b 1
echo [build] Success: %ROOT%\build\bin\gtc1453.exe
if /i "%~1"=="--run" "%ROOT%\build\bin\gtc1453.exe"
exit /b %ERRORLEVEL%

:LocateVisualStudio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" (
  call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
  exit /b 0
)
if not exist "%VSWHERE%" exit /b 0
set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if defined VSROOT if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
exit /b 0
