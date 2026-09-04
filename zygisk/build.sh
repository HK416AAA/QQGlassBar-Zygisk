#!/bin/bash
# Build the QFun Zygisk native module for supported ABIs using the NDK.
# Output: zygisk/out/<abi>.so  (Magisk-expected name under module zygisk/)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/out"

NDK="${NDK:-$ANDROID_NDK_HOME}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "error: set NDK (or ANDROID_NDK_HOME)" >&2
    exit 1
fi
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[ -f "$TOOLCHAIN" ] || { echo "error: no android.toolchain.cmake" >&2; exit 1; }

VERCODE="${MQ_VERCODE:-26}"
ABIS="arm64-v8a armeabi-v7a x86_64 x86"

rm -rf "$OUT_DIR"
for abi in $ABIS; do
    echo "=== building qfun zygisk $abi ==="
    BUILD_DIR="$SCRIPT_DIR/.build-$abi"
    rm -rf "$BUILD_DIR"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_MAKE_PROGRAM="${NINJA:-/usr/bin/ninja}" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DMQ_VERCODE="$VERCODE"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
    mkdir -p "$OUT_DIR"
    cp "$BUILD_DIR/libzygisk.so" "$OUT_DIR/$abi.so"
done
echo "=== done: $OUT_DIR ==="
