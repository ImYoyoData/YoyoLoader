@echo off
rem Package a ready-to-drop-in YoyoLoader release for Windows.
rem
rem The zip layout mirrors a BDS install, so users extract it straight into
rem their server root:
rem   yoyo.exe
rem   yoyo_runtime.dll
rem   plugins\...
rem   README.txt
rem
rem Usage: scripts\package_windows.bat [version]
setlocal EnableDelayedExpansion

if not "%~1"=="" set VERSION=%~1
if "%VERSION%"=="" set VERSION=dev

set ROOT=%~dp0..
set OUT=%ROOT%\dist
set STAGE=%OUT%\stage-win64
set ZIP=%OUT%\YoyoLoader-%VERSION%-win64.zip

where powershell >nul 2>nul
if errorlevel 1 (
    echo [package] powershell is required to create the zip
    exit /b 1
)

if not exist "%ROOT%\build\bin\yoyo.exe" (
    echo [package] build\bin\yoyo.exe missing; run scripts\build_windows.bat first
    exit /b 1
)
if not exist "%ROOT%\build\bin\yoyo_runtime.dll" (
    echo [package] build\bin\yoyo_runtime.dll missing; run scripts\build_windows.bat first
    exit /b 1
)

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\plugins"
if errorlevel 1 exit /b 1

echo [package] copying binaries
copy /y "%ROOT%\build\bin\yoyo.exe" "%STAGE%\yoyo.exe" >nul || exit /b 1
copy /y "%ROOT%\build\bin\yoyo_runtime.dll" "%STAGE%\yoyo_runtime.dll" >nul || exit /b 1

echo [package] copying sample plugins
if exist "%ROOT%\build\bin\plugins\sample_cpp.dll" (
    mkdir "%STAGE%\plugins\sample_cpp" 2>nul
    copy /y "%ROOT%\build\bin\plugins\sample_cpp.dll" "%STAGE%\plugins\sample_cpp\sample_cpp.dll" >nul
    > "%STAGE%\plugins\sample_cpp\manifest.json" echo {"name":"sample_cpp","version":"1.0.0","engine":"native","entry":"sample_cpp.dll","description":"YoyoLoader C++ ABI sample"}
)

rem Ship the three example plugins in manifest form so users have working
rem examples on first run.
for %%P in (home title tpa) do (
    mkdir "%STAGE%\plugins\%%P" 2>nul
    copy /y "%ROOT%\plugins\%%P.js" "%STAGE%\plugins\%%P\main.js" >nul 2>nul
    > "%STAGE%\plugins\%%P\manifest.json" echo {"name":"%%P","version":"1.0.0","engine":"javascript","entry":"main.js"}
)

echo [package] writing README.txt
> "%STAGE%\README.txt" echo YoyoLoader %VERSION% (Windows x64^)
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo HOW TO USE
>> "%STAGE%\README.txt" echo   1. Extract this zip into your BDS server root, next to bedrock_server.exe.
>> "%STAGE%\README.txt" echo   2. Run yoyo.exe instead of bedrock_server.exe.
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo   The launcher finds bedrock_server.exe in the current directory.
>> "%STAGE%\README.txt" echo   Plugins live in plugins\^<name^>\manifest.json.
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo WHAT IS INSIDE
>> "%STAGE%\README.txt" echo   yoyo.exe             launcher / injector
>> "%STAGE%\README.txt" echo   yoyo_runtime.dll     injected runtime (events, JS, plugins^)
>> "%STAGE%\README.txt" echo   plugins\home         example: /home waypoints
>> "%STAGE%\README.txt" echo   plugins\title        example: /title name tags
>> "%STAGE%\README.txt" echo   plugins\tpa          example: /tpa teleport requests
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo   Version support and limitations: see the project README / wiki.
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo SUPPORTED BDS BUILDS
>> "%STAGE%\README.txt" echo   1.26.52.3   full support (Windows / Linux^)
>> "%STAGE%\README.txt" echo   1.26.70.21  partial (see yoyo.log at startup^)
>> "%STAGE%\README.txt" echo   Other builds are probed automatically; unsupported features are
>> "%STAGE%\README.txt" echo   reported in yoyo.log and refused rather than crashing.

if exist "%ZIP%" del /q "%ZIP%"
echo [package] creating %ZIP%
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%' -CompressionLevel Optimal"
if errorlevel 1 exit /b 1

rem Keep a checksum next to the archive for release verification.
powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%ZIP%').Hash.ToLower()" > "%ZIP%.sha256"

echo [package] done: %ZIP%
endlocal
