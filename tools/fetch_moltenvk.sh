#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_ROOT="$PROJECT_ROOT/external/MoltenVK"
VERSION="v1.4.0"
BASE_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/${VERSION}"

ensure_tools() {
    for tool in curl tar; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "缺少依赖: $tool" >&2
            exit 1
        fi
    done
}

fetch_ios() {
    local archive="MoltenVK-ios.tar"
    local url="${BASE_URL}/${archive}"
    local temp_dir
    temp_dir="$(mktemp -d)"
    trap 'rm -rf "$temp_dir"' RETURN

    echo "下载 MoltenVK iOS 包: $url"
    curl -L "$url" -o "$temp_dir/$archive"

    echo "解压归档..."
    tar -xf "$temp_dir/$archive" -C "$temp_dir"

    local extracted="$temp_dir/MoltenVK/MoltenVK"
    local ios_source="$extracted/static/MoltenVK.xcframework/ios-arm64"
    local dest_root="$OUTPUT_ROOT/ios"

    rm -rf "$dest_root"
    mkdir -p "$dest_root/lib" "$dest_root/include"
    cp "$ios_source/libMoltenVK.a" "$dest_root/lib/"
    cp -R "$extracted/include/." "$dest_root/include/"

    echo "MoltenVK iOS 库已安装到 $dest_root"
}

case "${1:-ios}" in
    ios)
        ensure_tools
        fetch_ios
        ;;
    *)
        echo "未知平台: ${1}" >&2
        exit 1
        ;;
 esac
