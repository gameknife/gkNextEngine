@echo off
setlocal enableextensions enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PROJECT_ROOT=%SCRIPT_DIR%"

if not defined VCPKG_ROOT set "VCPKG_ROOT=%PROJECT_ROOT%\.vcpkg"
set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"

if "%~1"=="" goto :usage
set "PLATFORM=%~1"
set "FEATURES=%~2"

if defined FEATURES (
    set "VCPKG_MANIFEST_FEATURES=%FEATURES%"
) else (
    set "VCPKG_MANIFEST_FEATURES="
)

call :ensure_repo || goto :error
call :select_triplet "%PLATFORM%" || goto :error
call :install_manifest "%TRIPLET%" || goto :error

echo [vcpkg] Done. 如果自定义路径，请复用 VCPKG_ROOT=%VCPKG_ROOT%
exit /b 0

:usage
echo Usage: vcpkg.bat ^<platform^> [manifest-features]
echo.
echo Platforms: windows^|android
exit /b 1

:ensure_repo
if not exist "%VCPKG_ROOT%\.git" (
    echo [vcpkg] Cloning vcpkg into %VCPKG_ROOT%...
    git clone https://github.com/microsoft/vcpkg "%VCPKG_ROOT%" || goto :error
) else (
    echo [vcpkg] Updating vcpkg in %VCPKG_ROOT%...
    pushd "%VCPKG_ROOT%" >nul || goto :error
    git pull --ff-only
    if errorlevel 1 (
        echo [vcpkg] Warning: 无法访问远程仓库，继续使用现有 vcpkg.>&2
    )
    popd >nul
)

if not exist "%VCPKG_EXE%" (
    echo [vcpkg] Bootstrapping vcpkg...
    pushd "%VCPKG_ROOT%" >nul || goto :error
    call bootstrap-vcpkg.bat -disableMetrics || goto :error
    popd >nul
)
exit /b 0

:select_triplet
set "TMP_PLATFORM=%~1"
if /i "%TMP_PLATFORM%"=="windows" (
    set "TRIPLET=x64-windows-static"
) else if /i "%TMP_PLATFORM%"=="android" (
    set "TRIPLET=arm64-android"
) else (
    echo Usage: vcpkg.bat windows^|android 1>&2
    exit /b 1
)
exit /b 0

:install_manifest
echo [vcpkg] Installing manifest dependencies for triplet %~1...
pushd "%PROJECT_ROOT%" >nul || goto :error
"%VCPKG_EXE%" install --triplet %~1 --feature-flags=manifests || goto :error
popd >nul
exit /b 0

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
