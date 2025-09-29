@echo off
setlocal EnableDelayedExpansion

set "root_dir=%~dp0"
set "target=gkNextRenderer.exe"
set "platform=windows"
set "bin_dir="
set "list_only=0"
set "dry_run=0"
set "args="

:parse
if "%~1"=="" goto resolve
if "%~1"=="--" (
    shift
    goto passthrough
)
if "%~1"=="--list" set "list_only=1" & shift & goto parse
if "%~1"=="--dry-run" set "dry_run=1" & shift & goto parse
if "%~1"=="-h" goto help
if "%~1"=="--help" goto help

for %%O in (--target --platform --bin-dir --present-mode --scene) do (
    if /I "%%O"=="%~1" (
        if "%~2"=="" (
            echo Missing value for %~1 1>&2
            exit /b 1
        )
        if "%%O"=="--target" set "target=%~2"
        if "%%O"=="--platform" set "platform=%~2"
        if "%%O"=="--bin-dir" set "bin_dir=%~2"
        if "%%O"=="--present-mode" set "args=!args! --present-mode=%~2"
        if "%%O"=="--scene" set "args=!args! --load-scene=%~2"
        shift
        shift
        goto parse
    )
)

set "args=!args! %~1"
shift
goto parse

:passthrough
if "%~1"=="" goto resolve
set "args=!args! %~1"
shift
goto passthrough

:resolve
if /i "%platform%"=="android" (
    call :run_android
set "android_dir=%root_dir%android"
if not exist "%android_dir%" (
    echo Android project directory not found: %android_dir% 1>&2
    exit /b 1
)
if "%list_only%"=="1" (
    echo --list is not supported for android platform 1>&2
    exit /b 1
)
if not "%args%"=="" (
    echo Ignoring extra arguments for android platform: %args% 1>&2
)
set "cmd_install=gradlew.bat installAndLaunch"
set "cmd_push=gradlew.bat pushAssetsToDevice"
echo Working dir: %android_dir%
echo Command: %cmd_install%
echo Command: %cmd_push%
if "%dry_run%"=="1" exit /b 0
pushd "%android_dir%" >nul
call %cmd_install%
set "ec=%errorlevel%"
if not "%ec%"=="0" (
    popd >nul
    exit /b %ec%
)
call %cmd_push%
set "ec=%errorlevel%"
popd >nul
exit /b %ec%

:help
echo Usage: run.bat [options] [-- extra args]
echo   --target NAME         Executable to launch ^(default: gkNextRenderer.exe^)
echo   --platform NAME       build\^<platform^>\bin override ^(default: windows^)
echo   --bin-dir PATH        explicit bin directory
echo   --present-mode VALUE  append --present-mode=VALUE
echo   --scene PATH          append --load-scene=PATH
echo   --list                list entries in the bin directory
echo   --dry-run             print the command without running
echo   -h, --help            show this help
exit /b 0
