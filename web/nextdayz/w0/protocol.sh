#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo"
includes=${1:?Usage: protocol.sh /path/to/vcpkg-triplet/include [native architecture]}
output="$repo/out/web/nextdayz/w0/protocol"
mkdir -p "$output"
game="src/Application/Game/NextDayz"
sources=(
    src/Application/Web/NextDayzWebCore/ProtocolProbe.cpp
    "$game/Net/NetProtocol.cpp" "$game/Inventory/Inventory.cpp" "$game/Data/ItemDefs.cpp"
)
flags=(-std=c++20 -O2 -DNDEBUG -DGK_MODULE_NEXTNET=1 -DFMT_HEADER_ONLY -Isrc -isystem "$includes")
arch=${2:-$(uname -m)}
native_flags=()
if [[ -n ${2:-} ]]; then native_flags=(-arch "$arch"); fi

"${CXX:-c++}" "${flags[@]}" ${native_flags[@]+"${native_flags[@]}"} "${sources[@]}" -o "$output/native-$arch"
"$output/native-$arch" > "$output/native-$arch.txt"
em++ "${flags[@]}" "${sources[@]}" -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -o "$output/protocol.cjs"
node "$output/protocol.cjs" > "$output/wasm32.txt"
diff -u "$output/native-$arch.txt" "$output/wasm32.txt"
cat "$output/wasm32.txt"
wc -c "$output/protocol.cjs" "$output/protocol.wasm"
