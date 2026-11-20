@echo off
rem Initialize the installed Visual Studio environment when available.
rem Do not use setlocal: vcvars must update the caller.
set "GTC_VCVARS="
set "GTC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "GTC_VCVARS_FILE=%~1\build\obj\vcvars_stable_path.txt"
if not exist "%~1\build\obj" mkdir "%~1\build\obj"
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "GTC_VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if not defined GTC_VCVARS if exist "%GTC_VSWHERE%" (
  "%GTC_VSWHERE%" -latest -products * -find "VC\Auxiliary\Build\vcvars64.bat" > "%GTC_VCVARS_FILE%"
  set /p "GTC_VCVARS=" < "%GTC_VCVARS_FILE%"
)
if defined GTC_VCVARS call "%GTC_VCVARS%"
exit /b 0
