#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
DEFAULT_VCPKG_ROOT="$PROJECT_ROOT/.vcpkg"
VCPKG_ROOT="${VCPKG_ROOT:-$DEFAULT_VCPKG_ROOT}"
VCPKG_EXE="$VCPKG_ROOT/vcpkg"

log()  { printf '[vcpkg] %s\n' "$*"; }
warn() { printf '[vcpkg] Warning: %s\n' "$*" >&2; }

usage() {
    cat <<USAGE
Usage: ./vcpkg.sh <platform> [manifest-features]

Platforms:
  macos        (arm64-osx)
  macos_x64    (x64-osx)
  linux        (x64-linux)
  android      (arm64-android)
  ios          (arm64-ios)
  mingw        (x64-mingw-static)
  windows      (x64-windows-static)

Examples:
  ./vcpkg.sh macos
  ./vcpkg.sh linux avif
USAGE
}

select_triplet() {
    case "$1" in
        macos) echo "arm64-osx" ;;
        macos_x64) echo "x64-osx" ;;
        linux) echo "x64-linux" ;;
        android) echo "arm64-android" ;;
        ios) echo "arm64-ios" ;;
        mingw) echo "x64-mingw-static" ;;
        windows) echo "x64-windows-static" ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
}

ensure_repo() {
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        log "Cloning vcpkg into $VCPKG_ROOT..."
        git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
    else
        log "Updating vcpkg in $VCPKG_ROOT..."
        if ! git -C "$VCPKG_ROOT" pull --ff-only; then
            warn "无法访问远程仓库，继续使用现有 vcpkg 副本。"
        fi
    fi

    if [ ! -x "$VCPKG_EXE" ]; then
        log "Bootstrapping vcpkg..."
        (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
    fi
}

install_manifest() {
    local triplet="$1"
    log "Installing manifest dependencies for triplet '$triplet'..."
    pushd "$PROJECT_ROOT" >/dev/null
    "$VCPKG_EXE" install --triplet "$triplet" --feature-flags=manifests
    popd >/dev/null
}

main() {
    local platform="${1:-}"
    if [ -z "$platform" ]; then
        usage
        exit 1
    fi

    local features="${2:-}"
    if [ -n "$features" ]; then
        export VCPKG_MANIFEST_FEATURES="$features"
    else
        unset VCPKG_MANIFEST_FEATURES || true
    fi

    ensure_repo
    local triplet
    triplet=$(select_triplet "$platform")
    install_manifest "$triplet"

    log "Done. 如果使用自定义路径，记得复用 VCPKG_ROOT=${VCPKG_ROOT}."
}

main "$@"
