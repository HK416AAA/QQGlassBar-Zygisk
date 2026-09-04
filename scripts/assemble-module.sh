#!/bin/bash
# Assemble QQGlassBar-Zygisk module. Requires built:
#   payload APK (glassbar.apk), zygisk/out/*.so, native/out/*/libmq-core.so,
#   core/out/classes.dex
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TPL="$ROOT_DIR/module-template"; ZYGOUT="$ROOT_DIR/zygisk/out"; NATOUT="$ROOT_DIR/native/out"; CORE="$ROOT_DIR/core/out"
STAGE="$ROOT_DIR/out/stage"; DIST="$ROOT_DIR/out"
GLASS_APK="${GLASS_APK:?}"
VER="${GB_VERSION:-v0.1.0}"; VC="${GB_VERCODE:-1}"

rm -rf "$DIST"; mkdir -p "$DIST" "$STAGE/zygisk" "$STAGE/libmq-core"
sed -e "s/^version=.*/version=$VER/" -e "s/^versionCode=.*/versionCode=$VC/" "$TPL/module.prop" > "$STAGE/module.prop"
cp "$TPL/customize.sh" "$STAGE/customize.sh"
for abi in arm64-v8a armeabi-v7a x86_64 x86; do
    [ -f "$ZYGOUT/$abi.so" ] && cp "$ZYGOUT/$abi.so" "$STAGE/zygisk/$abi.so"
    if [ -f "$NATOUT/$abi/libmq-core.so" ]; then
        mkdir -p "$STAGE/libmq-core/$abi"
        cp "$NATOUT/$abi/libmq-core.so" "$STAGE/libmq-core/$abi/libmq-core.so"
    fi
done
cp "$CORE/classes.dex" "$STAGE/core.dex"
cp "$GLASS_APK" "$STAGE/glassbar.apk"
ZIP="$DIST/qqglassbar-zygisk-$VER.zip"
(cd "$STAGE" && zip -r -9 "$ZIP" . >/dev/null)
rm -rf "$STAGE"
echo "module zip: $ZIP"; du -h "$ZIP"
