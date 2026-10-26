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

rem cl.exe on PATH is not enough: vcvars also configures standard headers and libraries.
call :InitializeVisualStudio
if errorlevel 1 exit /b 1
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [build] Visual Studio 2022 C++ tools were not found.
  echo [build] Install the "Desktop development with C++" workload and retry.
  exit /b 1
)
if not defined INCLUDE (
  echo [build] vcvars64.bat did not configure INCLUDE. Reinstall the MSVC v143 toolset.
  exit /b 1
)
call :CheckStandardLibrary
if errorlevel 1 exit /b 1

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

:InitializeVisualStudio
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  set "VSROOT="
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
  if defined VSROOT if exist "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=!VSROOT!\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
  echo [build] Could not locate Visual Studio 2022 with MSVC v143 build tools.
  echo [build] In Visual Studio Installer, enable "Desktop development with C++".
  exit /b 1
)
echo [build] Initializing Visual Studio C++ environment...
call "!VCVARS!"
if errorlevel 1 (
  echo [build] vcvars64.bat failed to initialize the C++ environment.
  exit /b 1
)
exit /b 0

:CheckStandardLibrary
set "PROBE=%ROOT%\build\obj\msvc_environment.cpp"
> "%PROBE%" echo #include ^<cmath^>
>> "%PROBE%" echo int main() { return 0; }
cl.exe /nologo /std:c++17 /c "%PROBE%" /Fo"%ROOT%\build\obj\msvc_environment.obj"
if errorlevel 1 (
  echo [build] MSVC was found, but its C++ standard library is unavailable.
  echo [build] Open Visual Studio Installer and modify Visual Studio 2022.
  echo [build] Enable "Desktop development with C++", including:
  echo [build]   MSVC v143 C++ x64/x86 build tools
  echo [build]   A Windows 10 or Windows 11 SDK
  exit /b 1
)
exit /b 0
