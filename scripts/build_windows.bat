@echo off
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else (
        echo [build] cl.exe not found. Run this from a Developer Command Prompt.
        exit /b 1
    )
)

if not exist build\obj mkdir build\obj
if not exist build\bin\plugins mkdir build\bin\plugins

set COMMON=/nologo /std:c++20 /EHsc /MD /O2 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS
set QJSFLAGS=/nologo /c /O2 /MD /W4 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /Ithird_party\quickjs
if not exist build\obj\qjs mkdir build\obj\qjs

echo [build] QuickJS
for %%f in (quickjs dtoa libregexp libunicode) do (
    cl %QJSFLAGS% third_party\quickjs\%%f.c /Fo:build\obj\qjs\%%f.obj
    if errorlevel 1 exit /b 1
)

echo [build] yoyo_runtime.dll
cl %COMMON% /DYOYO_PLATFORM_WINDOWS=1 /DYOYO_RUNTIME_BUILD=1 /Iinclude /Isrc\runtime /Ithird_party\quickjs /LD ^
    src\runtime\entry.cpp ^
    src\runtime\runtime.cpp ^
    src\runtime\log.cpp ^
    src\runtime\module.cpp ^
    src\runtime\native_string.cpp ^
    src\runtime\version.cpp ^
    src\runtime\signatures.cpp ^
    src\runtime\capabilities.cpp ^
    src\runtime\hook_engine.cpp ^
    src\runtime\hooks.cpp ^
    src\runtime\live.cpp ^
    src\runtime\packet_events.cpp ^
    src\runtime\worlds.cpp ^
    src\runtime\events.cpp ^
    src\runtime\plugin_loader.cpp ^
    src\runtime\players.cpp ^
    src\runtime\nbt\nbt.cpp ^
    src\runtime\js\js_engine.cpp ^
    src\runtime\js\js_nbt.cpp ^
    src\runtime\js\js_forms.cpp ^
    src\runtime\js\js_timers.cpp ^
    src\runtime\js\js_script.cpp ^
    src\runtime\js\js_compat.cpp ^
    src\runtime\js\js_commands.cpp ^
    build\obj\qjs\quickjs.obj build\obj\qjs\dtoa.obj build\obj\qjs\libregexp.obj build\obj\qjs\libunicode.obj ^
    ws2_32.lib /Fo:build\obj\ /Fe:build\bin\yoyo_runtime.dll
if errorlevel 1 exit /b 1

echo [build] yoyo.exe
cl %COMMON% /DYOYO_PLATFORM_WINDOWS=1 /Iinclude /Isrc\launcher ^
    src\launcher\main.cpp ^
    src\launcher\win_inject.cpp ^
    /Fo:build\obj\ /Fe:build\bin\yoyo.exe
if errorlevel 1 exit /b 1

echo [build] sample_cpp.dll
cl %COMMON% /Iinclude /LD plugins\sample_cpp\plugin.cpp ^
    /Fo:build\obj\ /Fe:build\bin\plugins\sample_cpp.dll
if errorlevel 1 exit /b 1

echo [build] done
