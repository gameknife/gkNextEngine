#!/usr/bin/env bash
set -euo pipefail

app=$(cd "$(dirname "$0")" && pwd)
cd "$app/../../.."
includes=${1:?Usage: build.sh /path/to/vcpkg-triplet/include}
out=${ND_W1_OUTPUT:-"$PWD/out/web/nextdayz/w1"}
game="src/Application/Game/NextDayz"
mkdir -p "$out"
em++ -std=c++20 -O2 -DNDEBUG -DFMT_HEADER_ONLY -DGK_MODULE_NEXTNET=1 -Isrc -isystem "$includes" \
    "$app/ReviewProtocol.cpp" "$game/Net/NetProtocol.cpp" "$game/Inventory/Inventory.cpp" "$game/Data/ItemDefs.cpp" \
    --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString \
    -o "$out/protocol.mjs"
cp "$app/index.html" "$app/review.mjs" "$app/transport.mjs" "$out/"
