@echo off

setlocal enabledelayedexpansion

set "PLATFORM=%1"
if "%PLATFORM%"=="" (
    set "PLATFORM=windows"
)

if "%PLATFORM%"=="windows" (
    call :build_windows %2
) else if "%PLATFORM%"=="android" (
    call :build_android
) else (
    echo Error: Unsupported platform '%PLATFORM%'
    echo Supported platforms: windows, android
    goto :error
)

echo Build completed successfully!
exit /b

:build_windows
set "BUILD_CONFIG=%1"
if "%BUILD_CONFIG%"=="" (
    set "BUILD_CONFIG=RelWithDebInfo"
)

for %%X in (cmake.exe) do (set FOUND=%%~$PATH:X)
if defined FOUND (
    set CMAKE=cmake
) ELSE (
    set CMAKE="%CD%\build\vcpkg.windows\downloads\tools\cmake-3.30.1-windows\cmake-3.30.1-windows-i386\bin\cmake.exe"
)

for %%X in (msbuild.exe) do (set FOUND=%%~$PATH:X)
if defined FOUND (
    set MSBUILD=msbuild
) ELSE (
    if exist "%programfiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\msbuild.exe" (
        set MSBUILD="%programfiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\msbuild.exe"
    ) ELSE if exist "%programfiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\msbuild.exe" (
        set MSBUILD="%programfiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\msbuild.exe"
    ) ELSE (
        echo Error: msbuild.exe not found. Please ensure Visual Studio is installed and msbuild.exe is in PATH.
        goto :error
    )
)

cd build || goto :error
mkdir windows 2>nul
cd windows || goto :error

call :SETTIMER

%CMAKE% -D WITH_OIDN=0 -D WITH_AVIF=0 -D WITH_SUPERLUMINAL=0 -D VCPKG_TARGET_TRIPLET=x64-windows-static -D CMAKE_TOOLCHAIN_FILE=../vcpkg.windows/scripts/buildsystems/vcpkg.cmake -G "Visual Studio 17 2022" -A "x64" ../.. || goto :error
%CMAKE% --build . --config %BUILD_CONFIG% -j12 || goto :error

call :STOPTIMER "Build"

cd ..
cd ..
exit /b

:build_android
cd android || goto :error
./gradlew.bat build || goto :error

cd ..
exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%

goto :EOF

:SETTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set StartTime=%%a
goto :EOF

:STOPTIMER
for /f %%a in ('powershell -command "Get-Date -Format HH:mm:ss.fff"') do set EndTime=%%a
for /f %%a in ('powershell -command "((Get-Date '%EndTime%') - (Get-Date '%StartTime%')).TotalSeconds"') do set Duration=%%a
echo %~1 cost %Duration% s.

goto :EOF