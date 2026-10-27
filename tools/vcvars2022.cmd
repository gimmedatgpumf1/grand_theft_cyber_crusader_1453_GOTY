@echo off
rem Called from build.cmd wrappers. Do not use setlocal: vcvars must update the caller.
set "GTC_VCVARS="
set "GTC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "GTC_VCVARS_FILE=%~1\build\obj\vcvars2022_path.txt"
if not exist "%~1\build\obj" mkdir "%~1\build\obj"
if exist "%GTC_VSWHERE%" (
  "%GTC_VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Auxiliary\Build\vcvars64.bat" > "%GTC_VCVARS_FILE%"
  if not errorlevel 1 set /p "GTC_VCVARS=" < "%GTC_VCVARS_FILE%"
)
if not defined GTC_VCVARS if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "GTC_VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if not defined GTC_VCVARS (
  echo [build] Could not locate Visual Studio 2022 with MSVC v143 build tools.
  echo [build] Enable Desktop development with C++ in Visual Studio Installer.
  exit /b 1
)
if not exist "%GTC_VCVARS%" (
  echo [build] Detected vcvars64.bat path does not exist: %GTC_VCVARS%
  exit /b 1
)
echo [build] Using: %GTC_VCVARS%
call "%GTC_VCVARS%"
if errorlevel 1 (
  echo [build] vcvars64.bat failed to initialize the C++ environment.
  exit /b 1
)
exit /b 0
