#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_ROOT="$PROJECT_ROOT/external"
VERSION="2025.6.1"
ARCHIVE="slang-${VERSION}-linux-x86_64.zip"
DEST_DIR="$OUTPUT_ROOT/${ARCHIVE%.zip}"
URL="https://github.com/shader-slang/slang/releases/download/v${VERSION}/${ARCHIVE}"

for tool in curl unzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "缺少依赖: $tool" >&2
        exit 1
    fi
done

if [ -d "$DEST_DIR" ]; then
    echo "Slang 已存在: $DEST_DIR"
    exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

echo "下载 Slang: $URL"
curl -L "$URL" -o "$tmp_dir/$ARCHIVE"

echo "解压到 $DEST_DIR ..."
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"
unzip -q "$tmp_dir/$ARCHIVE" -d "$DEST_DIR"

if [ ! -x "$DEST_DIR/bin/slangc" ]; then
    echo "未找到 slangc，可疑的归档结构: $DEST_DIR" >&2
    exit 1
fi

echo "Slang 安装完成: $DEST_DIR"
