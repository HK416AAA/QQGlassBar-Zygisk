#!/bin/bash
# Build libmq-core.so (shared LSPlant engine) for Android ABIs.
# Output: native/out/<abi>/libmq-core.so
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/out"
NDK="${NDK:-$ANDROID_NDK_HOME}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then echo "error: set NDK" >&2; exit 1; fi
TC="$NDK/build/cmake/android.toolchain.cmake"
[ -f "$TC" ] || { echo "error: toolchain" >&2; exit 1; }
ABIS="arm64-v8a armeabi-v7a"
rm -rf "$OUT"
for abi in $ABIS; do
    echo "=== mq-core $abi ==="
    B="$HERE/.build-$abi"
    rm -rf "$B"
    cmake -S "$HERE" -B "$B" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TC" \
        -DCMAKE_MAKE_PROGRAM="${NINJA:-/usr/bin/ninja}" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
    cmake --build "$B" -j"$(nproc)"
    mkdir -p "$OUT/$abi"
    cp "$B/libmq-core.so" "$OUT/$abi/libmq-core.so"
done
echo "=== done: $OUT ==="
