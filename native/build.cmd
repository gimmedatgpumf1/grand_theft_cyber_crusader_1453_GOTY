@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0.."
set "ROOT=%CD%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\nagger.ps1"
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\bin" mkdir "%ROOT%\build\bin"
if not exist "%ROOT%\build\obj\native" mkdir "%ROOT%\build\obj\native"
if not exist "%ROOT%\.cache\ccache" mkdir "%ROOT%\.cache\ccache"
set "CCACHE_DIR=%ROOT%\.cache\ccache"
set "CCACHE_CONFIGPATH=%ROOT%\tools\ccache.conf"
call :InitializeVisualStudio
if errorlevel 1 exit /b 1
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [native] Visual Studio 2022 C++ tools were not found.
  exit /b 1
)
set "CCACHE_BIN="
if exist "%ROOT%\tools\ccache\windows-x86_64\ccache.exe" set "CCACHE_BIN=%ROOT%\tools\ccache\windows-x86_64\ccache.exe"
if not defined CCACHE_BIN (
  where ccache.exe >nul 2>nul
  if not errorlevel 1 set "CCACHE_BIN=ccache.exe"
)
set "OBJECTS="
for %%S in (native\human.cpp native\io.cpp native\hair.cpp native\fit.cpp native\render.cpp native\main.cpp src\core\json.cpp) do (
  set "OBJECT=%ROOT%\build\obj\native\%%~nS.obj"
  set "OBJECTS=!OBJECTS! "!OBJECT!""
  if defined CCACHE_BIN (
    "!CCACHE_BIN!" cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  ) else (
    cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  )
  if errorlevel 1 exit /b 1
)
cl.exe /nologo !OBJECTS! /Fe"%ROOT%\build\bin\gtc-native-fit.exe"
if errorlevel 1 exit /b 1
echo [native] Success: %ROOT%\build\bin\gtc-native-fit.exe
exit /b 0

:InitializeVisualStudio
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  "%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Auxiliary\Build\vcvars64.bat" > "%ROOT%\build\obj\native\vcvars_path.txt"
  if not errorlevel 1 set /p "VCVARS=" < "%ROOT%\build\obj\native\vcvars_path.txt"
)
if not defined VCVARS if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
  echo [native] Could not locate Visual Studio 2022 with MSVC v143 build tools.
  echo [native] Enable Desktop development with C++ in Visual Studio Installer.
  exit /b 1
)
if not exist "!VCVARS!" exit /b 1
call "!VCVARS!"
exit /b %ERRORLEVEL%
