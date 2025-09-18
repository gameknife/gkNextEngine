@echo off

setlocal enabledelayedexpansion

set "MODE=%1"
set "PARAM=%2"

if "%MODE%"=="" (
    call :show_help
    exit /b 1
)

if "%MODE%"=="help" (
    call :show_help
    exit /b 0
)

if "%MODE%"=="local" (
    call :package_local %PARAM%
) else if "%MODE%"=="magicalego" (
    call :package_magicalego %PARAM%
) else (
    echo Error: Unknown mode '%MODE%'
    echo.
    call :show_help
    exit /b 1
)

exit /b 0

:package_local
set "CLEAN_PARAM=%1"

if "%CLEAN_PARAM%"=="clean" (
    echo Cleaning build directory...
    rmdir /S /Q build\windows 2>nul
)

echo Building gkNextRenderer...
call ./build.bat windows

pushd %CD%
cd build/windows

echo Copying package scripts...
copy /Y ..\..\package\*.bat %CD% >nul 2>&1

echo Creating gkNextRenderer-windows package...
tar -a -cf gkNextRenderer-windows.zip ./bin ./assets/locale ./assets/shaders ./assets/textures ./assets/fonts ./assets/models ./*.bat

move /Y gkNextRenderer-windows.zip ..\..\ >nul 2>&1
popd

echo Local package completed: gkNextRenderer-windows.zip
exit /b

:package_magicalego
set "VERSION=%1"

if "%VERSION%"=="" (
    echo Error: Version parameter is required for magicalego mode
    echo Usage: %0 magicalego [version]
    exit /b 1
)

echo Building MagicaLego...
call ./build.bat windows

pushd %CD%
cd build/windows/bin

echo Creating lego asset package...
Packager --out ../assets/paks/lego.pak --src assets --regex ".*.hdr|.*.png|.*.spv"

popd

pushd %CD%
cd build/windows

echo Creating MagicaLego package...
zip -r "MagicaLego_win64_%VERSION%.zip" ./bin/MagicaLego.exe ./bin/ffmpeg.exe ./bin/vulkan-1.dll ./assets/legos ./assets/sfx ./assets/paks ./assets/locale ./assets/fonts ./assets/models/legobricks.glb

move /Y "MagicaLego_win64_%VERSION%.zip" ..\..\ >nul 2>&1
popd

echo MagicaLego package completed: MagicaLego_win64_%VERSION%.zip
exit /b

:show_help
echo Usage: %0 [mode] [options]
echo.
echo Modes:
echo   local [clean]         Package gkNextRenderer for local deployment
echo   magicalego [version]   Package MagicaLego game with version number
echo   help                  Show this help message
echo.
echo Examples:
echo   %0 local              # Package gkNextRenderer
echo   %0 local clean        # Clean build and package gkNextRenderer
echo   %0 magicalego v1.0.0  # Package MagicaLego with version v1.0.0
echo.
echo Notes:
echo   - local mode creates gkNextRenderer-windows.zip
echo   - magicalego mode creates MagicaLego_win64_[version].zip
echo   - Both modes require build.bat to work properly
exit /b