#!/bin/bash
set -e

detect_platform() {
    case "$(uname -s)" in
        Darwin*)
            if [ "$(uname -m)" = "arm64" ]; then
                echo "macos"
            else
                echo "macos_x64"
            fi
            ;;
        Linux*)
            echo "linux"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "mingw"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

build_macos() {
    local triplet="$1"
    local build_dir="build/macos"

    start_time=$(date +%s)

    mkdir -p "$build_dir"
    cd "$build_dir"
    cmake -Wno-dev -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D VCPKG_TARGET_TRIPLET="$triplet" \
        -D CMAKE_TOOLCHAIN_FILE="../vcpkg.macos/scripts/buildsystems/vcpkg.cmake" \
        ../..
    cmake --build . --config RelWithDebInfo

    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))

    echo "Build completed in $elapsed_time seconds."
}

build_ios() {
    local triplet="$1"
    local build_dir="build/ios"

    start_time=$(date +%s)

    mkdir -p "$build_dir"
    cd "$build_dir"
    cmake -Wno-dev -G Xcode \
        -D CMAKE_BUILD_TYPE=Release \
        -D CMAKE_SYSTEM_NAME=iOS \
        -D SDL3_DIR="/Users/gameknife/github/gkNextRenderer/lib/share/cmake/SDL3" \
        -D VCPKG_TARGET_TRIPLET="$triplet" \
        -D CMAKE_TOOLCHAIN_FILE="../vcpkg.ios/scripts/buildsystems/vcpkg.cmake" \
        ../..
    cmake --build . --config RelWithDebInfo

    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))

    echo "Build completed in $elapsed_time seconds."
}

build_linux() {
    local build_dir="build/linux"

    start_time=$(date +%s)

    mkdir --parents "$build_dir"
    cd "$build_dir"
    cmake -Wno-dev -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D VCPKG_TARGET_TRIPLET=x64-linux \
        -D CMAKE_TOOLCHAIN_FILE="../vcpkg.linux/scripts/buildsystems/vcpkg.cmake" \
        ../..
    cmake --build . --config Release

    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))

    echo "Build completed in $elapsed_time seconds."
}

build_android() {
    start_time=$(date +%s)

    cd android
    ./gradlew build

    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))

    echo "Build completed in $elapsed_time seconds."
}

build_mingw() {
    local build_dir="build/mingw"

    start_time=$(date +%s)

    mkdir --parents "$build_dir"
    cd "$build_dir"
    cmake -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D VCPKG_TARGET_TRIPLET=x64-mingw-static \
        -D CMAKE_TOOLCHAIN_FILE="../vcpkg.mingw/scripts/buildsystems/vcpkg.cmake" \
        ../..
    cmake --build . --config Release

    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))

    echo "Build completed in $elapsed_time seconds."
}

main() {
    local platform="$1"
    if [ -z "$platform" ]; then
        platform=$(detect_platform)
    fi

    case "$platform" in
        macos)
            echo "Building for macOS (ARM64)..."
            build_macos "arm64-osx"
            ;;
        macos_x64)
            echo "Building for macOS (x64)..."
            build_macos "x64-osx"
            ;;
        ios)
            echo "Building for iOS (ARM64)..."
            build_ios "arm64-ios"
            ;;
        linux)
            echo "Building for Linux..."
            build_linux
            ;;
        android)
            echo "Building for Android..."
            build_android
            ;;
        mingw)
            echo "Building for MinGW..."
            build_mingw
            ;;
        *)
            echo "Error: Unsupported platform '$platform'"
            echo "Supported platforms: macos, macos_x64, linux, android, mingw"
            echo "Usage: $0 [platform]"
            exit 1
            ;;
    esac
}

main "$@"