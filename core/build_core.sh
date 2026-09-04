#!/bin/bash
# Compile the shared core engine into core.dex (DexClassLoader-loadable).
# Requires: JDK, Android build-tools d8, platform android.jar.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
CLS="$HERE/.classes"
OUT="$HERE/out"
SDK="${ANDROID_HOME:-/usr/local/lib/android/sdk}"
ANDROID_JAR=$(ls "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)
D8=$(ls "$SDK"/build-tools/*/d8 2>/dev/null | sort -V | tail -1)

rm -rf "$CLS"
mkdir -p "$CLS" "$OUT"
find "$SRC" -name '*.java' > "$HERE/.sources.txt"
javac --release 8 -cp "$ANDROID_JAR" -d "$CLS" @"$HERE/.sources.txt" 2>&1 | grep -v "bootstrap class path" || true
"$D8" --release --lib "$ANDROID_JAR" --output "$OUT" $(find "$CLS" -name '*.class')
rm -rf "$CLS" "$HERE/.sources.txt"
echo "core.dex: $OUT/core.dex"
ls -la "$OUT"
