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

install_macos() {
    local triplet="$1"
    local vcpkg_dir="vcpkg.macos"

    if [ -d "$vcpkg_dir" ]; then
        cd "$vcpkg_dir"
        echo "Updating vcpkg..."
        git pull origin master
        ./bootstrap-vcpkg.sh
        echo "Updating installed packages..."
        ./vcpkg update
        ./vcpkg upgrade --no-dry-run
    else
        git clone https://github.com/Microsoft/vcpkg.git "$vcpkg_dir"
        cd "$vcpkg_dir"
        ./bootstrap-vcpkg.sh
    fi

    ./vcpkg --recurse install \
        cpptrace:"$triplet" \
        cxxopts:"$triplet" \
        sdl3[vulkan]:"$triplet" \
        glm:"$triplet" \
        imgui[core,freetype,sdl3-binding,vulkan-binding,docking-experimental]:"$triplet" \
        stb:"$triplet" \
        tinyobjloader:"$triplet" \
        curl:"$triplet" \
        tinygltf:"$triplet" \
        draco:"$triplet" \
        rapidjson:"$triplet" \
        fmt:"$triplet" \
        meshoptimizer:"$triplet" \
        ktx:"$triplet" \
        joltphysics:"$triplet" \
        xxhash:"$triplet" \
        spdlog:"$triplet" \
        cpp-base64:"$triplet"
}

install_linux() {
    local vcpkg_dir="vcpkg.linux"

    if [ -d "$vcpkg_dir" ]; then
        cd "$vcpkg_dir"
        echo "Updating vcpkg..."
        git pull origin master
        ./bootstrap-vcpkg.sh
        echo "Updating installed packages..."
        ./vcpkg update
        ./vcpkg upgrade --no-dry-run
    else
        git clone https://github.com/Microsoft/vcpkg.git "$vcpkg_dir"
        cd "$vcpkg_dir"
        ./bootstrap-vcpkg.sh
    fi

    ./vcpkg --recurse install \
        cpptrace:x64-linux \
        cxxopts:x64-linux \
        vulkan-loader[wayland,xcb,xlib]:x64-linux \
        sdl3:x64-linux \
        glm:x64-linux \
        imgui[core,freetype,sdl3-binding,vulkan-binding,docking-experimental]:x64-linux \
        stb:x64-linux \
        tinyobjloader:x64-linux \
        curl:x64-linux \
        tinygltf:x64-linux \
        draco:x64-linux \
        rapidjson:x64-linux \
        fmt:x64-linux \
        meshoptimizer:x64-linux \
        ktx:x64-linux \
        joltphysics:x64-linux \
        xxhash:x64-linux \
        spdlog:x64-linux \
        cpp-base64:x64-linux
}

install_android() {
    local vcpkg_dir="vcpkg.android"

    if [ -d "$vcpkg_dir" ]; then
        cd "$vcpkg_dir"
        echo "Updating vcpkg..."
        git pull origin master
        ./bootstrap-vcpkg.sh
        echo "Updating installed packages..."
        ./vcpkg update
        ./vcpkg upgrade --no-dry-run
    else
        git clone https://github.com/Microsoft/vcpkg.git "$vcpkg_dir"
        cd "$vcpkg_dir"
        ./bootstrap-vcpkg.sh
    fi

    cp -f ../../android/custom-triplets/arm64-android.cmake ./triplets/arm64-android.cmake

    ./vcpkg --recurse install \
        cxxopts:arm64-android \
        glm:arm64-android \
        imgui[core,freetype,vulkan-binding,docking-experimental]:arm64-android \
        stb:arm64-android \
        tinyobjloader:arm64-android \
        curl:arm64-android \
        tinygltf:arm64-android \
        draco:arm64-android \
        fmt:arm64-android \
        meshoptimizer:arm64-android \
        ktx:arm64-android \
        joltphysics:arm64-android \
        xxhash:arm64-android \
        spdlog:arm64-android \
        cpp-base64:arm64-android
}

install_mingw() {
    local vcpkg_dir="vcpkg.mingw"

    if [ -d "$vcpkg_dir" ]; then
        cd "$vcpkg_dir"
        echo "Updating vcpkg..."
        git pull origin master
        ./bootstrap-vcpkg.sh
        echo "Updating installed packages..."
        ./vcpkg update
        ./vcpkg upgrade --no-dry-run
    else
        git clone https://github.com/Microsoft/vcpkg.git "$vcpkg_dir"
        cd "$vcpkg_dir"
        ./bootstrap-vcpkg.sh
    fi

    ./vcpkg --recurse install \
        cpptrace:x64-mingw-static \
        cxxopts:x64-mingw-static \
        sdl3:x64-mingw-static \
        glm:x64-mingw-static \
        imgui[core,freetype,sdl3-binding,vulkan-binding,docking-experimental]:x64-mingw-static \
        stb:x64-mingw-static \
        tinyobjloader:x64-mingw-static \
        curl:x64-mingw-static \
        tinygltf:x64-mingw-static \
        draco:x64-mingw-static \
        rapidjson:x64-mingw-static \
        fmt:x64-mingw-static \
        meshoptimizer:x64-mingw-static \
        ktx:x64-mingw-static \
        joltphysics:x64-mingw-static \
        xxhash:x64-mingw-static \
        spdlog:x64-mingw-static \
        cpp-base64:x64-mingw-static
}

install_ios() {
    local vcpkg_dir="vcpkg.ios"

    if [ -d "$vcpkg_dir" ]; then
        cd "$vcpkg_dir"
        echo "Updating vcpkg..."
        git pull origin master
        ./bootstrap-vcpkg.sh
        echo "Updating installed packages..."
        ./vcpkg update
        ./vcpkg upgrade --no-dry-run
    else
        git clone https://github.com/Microsoft/vcpkg.git "$vcpkg_dir"
        cd "$vcpkg_dir"
        ./bootstrap-vcpkg.sh
    fi

    cp -f ../../android/custom-triplets/community/arm64-ios.cmake ./triplets/arm64-ios.cmake

    ./vcpkg --recurse install \
        cxxopts:arm64-ios \
        glm:arm64-ios \
        sdl3[vulkan]:arm64-ios \
        imgui[core,freetype,sdl3-binding,vulkan-binding,docking-experimental]:arm64-ios \
        stb:arm64-ios \
        tinyobjloader:arm64-ios \
        curl:arm64-ios \
        tinygltf:arm64-ios \
        draco:arm64-ios \
        fmt:arm64-ios \
        meshoptimizer:arm64-ios \
        ktx:arm64-ios \
        joltphysics:arm64-ios \
        xxhash:arm64-ios \
        spdlog:arm64-ios \
        cpp-base64:arm64-ios
}

main() {
    mkdir -p build
    cd build

    local platform="$1"
    if [ -z "$platform" ]; then
        platform=$(detect_platform)
    fi

    case "$platform" in
        macos)
            echo "Installing dependencies for macOS (ARM64)..."
            install_macos "arm64-osx"
            ;;
        macos_x64)
            echo "Installing dependencies for macOS (x64)..."
            install_macos "x64-osx"
            ;;
        linux)
            echo "Installing dependencies for Linux..."
            install_linux
            ;;
        android)
            echo "Installing dependencies for Android..."
            install_android
            ;;
        ios)
            echo "Installing dependencies for iOS..."
            install_ios
            ;;
        mingw)
            echo "Installing dependencies for MinGW..."
            install_mingw
            ;;
        *)
            echo "Error: Unsupported platform '$platform'"
            echo "Supported platforms: macos, macos_x64, linux, android, ios, mingw"
            exit 1
            ;;
    esac

    echo "Vcpkg installation completed successfully!"
}

main "$@"