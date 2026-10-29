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
call "%ROOT%\tools\vcvars2022.cmd" "%ROOT%"
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
    "!CCACHE_BIN!" cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  ) else (
    cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  )
  if errorlevel 1 exit /b 1
)
cl.exe /nologo !OBJECTS! /Fe"%ROOT%\build\bin\gtc-native-fit.exe"
if errorlevel 1 exit /b 1
if not exist "%ROOT%\build\obj\native-client" mkdir "%ROOT%\build\obj\native-client"
set "CLIENT_OBJECTS="
for %%S in (native\client\client.cpp native\client\raster.cpp native\client\hud.cpp native\client\human_renderer.cpp native\client\human_materials.cpp native\client\blockbody.cpp native\client\skeleton_body.cpp native\client\skeleton_room.cpp native\client\world_renderer.cpp native\client\fps_counter.cpp native\client\crosshair.cpp native\client\cheat_menu.cpp native\client\mode_bar.cpp native\client\overlay_panel.cpp native\client\interactive.cpp native\client\opengl_shared.cpp native\client\diagnostics.cpp native\client\window_win32.cpp native\client\main.cpp native\client\benchmark.cpp native\client\smoke.cpp src\net\platform.cpp src\net\websocket.cpp src\net\sha1.cpp native\vulkan\vk_device.cpp native\vulkan\vk_renderer.cpp native\vulkan\vk_batches.cpp native\vulkan\vk_targets.cpp native\vulkan\vk_swapchain.cpp) do (
  set "OBJECT=%ROOT%\build\obj\native-client\%%~nS.obj"
  set "CLIENT_OBJECTS=!CLIENT_OBJECTS! "!OBJECT!""
  if defined CCACHE_BIN (
    "!CCACHE_BIN!" cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  ) else (
    cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\src" /I"%ROOT%\native" /c "%%S" /Fo"!OBJECT!"
  )
  if errorlevel 1 exit /b 1
)
set "CLIENT_COMMON=%ROOT%\build\obj\native-client\client.obj %ROOT%\build\obj\native-client\raster.obj %ROOT%\build\obj\native-client\hud.obj %ROOT%\build\obj\native-client\human_renderer.obj %ROOT%\build\obj\native-client\blockbody.obj %ROOT%\build\obj\native-client\skeleton_body.obj %ROOT%\build\obj\native-client\skeleton_room.obj %ROOT%\build\obj\native-client\human_materials.obj %ROOT%\build\obj\native-client\world_renderer.obj %ROOT%\build\obj\native-client\fps_counter.obj %ROOT%\build\obj\native-client\crosshair.obj %ROOT%\build\obj\native-client\cheat_menu.obj %ROOT%\build\obj\native-client\mode_bar.obj %ROOT%\build\obj\native-client\overlay_panel.obj %ROOT%\build\obj\native\human.obj %ROOT%\build\obj\native\hair.obj %ROOT%\build\obj\native\json.obj %ROOT%\build\obj\native-client\platform.obj %ROOT%\build\obj\native-client\websocket.obj %ROOT%\build\obj\native-client\sha1.obj %ROOT%\build\obj\native-client\diagnostics.obj %ROOT%\build\obj\native-client\vk_device.obj %ROOT%\build\obj\native-client\vk_renderer.obj %ROOT%\build\obj\native-client\vk_batches.obj %ROOT%\build\obj\native-client\vk_targets.obj %ROOT%\build\obj\native-client\vk_swapchain.obj"
cl.exe /nologo "%ROOT%\build\obj\native-client\main.obj" "%ROOT%\build\obj\native-client\benchmark.obj" "%ROOT%\build\obj\native-client\interactive.obj" "%ROOT%\build\obj\native-client\window_win32.obj" "%ROOT%\build\obj\native-client\opengl_shared.obj" !CLIENT_COMMON! /Fe"%ROOT%\build\bin\gtc-native-client.exe" /link Ws2_32.lib User32.lib Gdi32.lib Opengl32.lib
if errorlevel 1 exit /b 1
cl.exe /nologo "%ROOT%\build\obj\native-client\smoke.obj" !CLIENT_COMMON! /Fe"%ROOT%\build\bin\gtc-native-smoke.exe" /link Ws2_32.lib
if errorlevel 1 exit /b 1
REM The counter is drawn on the host into pixels, so it is checked as pixels.
cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%\src" /I"%ROOT%\native" "tests\interface_check.cpp" !CLIENT_COMMON! /Fe"%ROOT%\build\bin\gtc-interface-check.exe" /link Ws2_32.lib
if errorlevel 1 exit /b 1
echo [native] Success: %ROOT%\build\bin\gtc-native-fit.exe
echo [native] Success: %ROOT%\build\bin\gtc-native-client.exe
echo [native] Success: %ROOT%\build\bin\gtc-native-smoke.exe
echo [native] Success: %ROOT%\build\bin\gtc-interface-check.exe
exit /b 0
