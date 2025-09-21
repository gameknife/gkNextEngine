@echo off

setlocal enabledelayedexpansion

set "PROJROOT=%CD%"

mkdir build
cd build || goto :error

set "PLATFORM=%1"
if "%PLATFORM%"=="" (
    set "PLATFORM=windows"
)

if "%PLATFORM%"=="windows" (
    call :install_windows %2 %3
) else if "%PLATFORM%"=="android" (
    call :install_android
) else (
    echo Error: Unsupported platform '%PLATFORM%'
    echo Supported platforms: windows, android
    goto :error
)

cd ..
cd ..

echo Vcpkg installation completed successfully!
exit /b

:install_windows
set "VCPKG_DIR=vcpkg.windows"

IF EXIST %VCPKG_DIR% (
    echo "%VCPKG_DIR% already exists."
    cd %VCPKG_DIR% || goto :error

    echo "Updating vcpkg..."
    git pull origin master || goto :error

    echo "Updating installed packages..."
    rem .\vcpkg.exe update
    rem .\vcpkg.exe upgrade --no-dry-run

    echo "installing..."
    vcpkg.exe install --recurse ^
        cpptrace:x64-windows-static ^
        cxxopts:x64-windows-static ^
        sdl3[vulkan]:x64-windows-static ^
        glm:x64-windows-static ^
        imgui[core,freetype,sdl3-binding,vulkan-binding,docking-experimental]:x64-windows-static ^
        stb:x64-windows-static ^
        tinyobjloader:x64-windows-static ^
        curl:x64-windows-static ^
        tinygltf:x64-windows-static ^
        draco:x64-windows-static ^
        rapidjson:x64-windows-static ^
        fmt:x64-windows-static ^
        meshoptimizer:x64-windows-static ^
        ktx:x64-windows-static ^
        joltphysics:x64-windows-static ^
        xxhash:x64-windows-static ^
        spdlog:x64-windows-static ^
        cpp-base64:x64-windows-static || goto :error

) ELSE (
    git clone https://github.com/Microsoft/vcpkg.git %VCPKG_DIR% || goto :error
    cd %VCPKG_DIR% || goto :error
    call bootstrap-vcpkg.bat || goto :error
)

if "%1" == "avif" (
    vcpkg.exe install --recurse libavif[aom]:x64-windows-static || goto :error
)

cd ..
exit /b

:install_android
set "VCPKG_DIR=vcpkg.android"

IF EXIST %VCPKG_DIR% (
    echo "%VCPKG_DIR% already exists."
    cd %VCPKG_DIR% || goto :error

    echo "Updating vcpkg..."
    git pull origin master || goto :error
    call bootstrap-vcpkg.bat || goto :error

    echo "Updating installed packages..."
    .\vcpkg.exe update
    .\vcpkg.exe upgrade --no-dry-run

) ELSE (
    git clone https://github.com/Microsoft/vcpkg.git %VCPKG_DIR% || goto :error
    cd %VCPKG_DIR% || goto :error
    call bootstrap-vcpkg.bat || goto :error
)

copy /Y "%PROJROOT%\android\custom-triplets\arm64-android.cmake" "%CD%\triplets\arm64-android.cmake" || goto :error

.\vcpkg.exe --recurse install ^
    cxxopts:arm64-android ^
    glm:arm64-android ^
    hlslpp:arm64-android ^
    imgui[core,freetype,android-binding,vulkan-binding,docking-experimental]:arm64-android ^
    stb:arm64-android ^
    tinyobjloader:arm64-android ^
    tinygltf:arm64-android ^
    curl:arm64-android ^
    draco:arm64-android ^
    fmt:arm64-android ^
    meshoptimizer:arm64-android ^
    ktx:arm64-android ^
    joltphysics:arm64-android ^
    xxhash:arm64-android ^
    spdlog:arm64-android ^
    cpp-base64:arm64-android || goto :error

cd ..
exit /b

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%