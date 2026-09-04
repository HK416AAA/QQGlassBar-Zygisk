#!/system/bin/sh
MODPATH=${0%/*}
case "$ARCH" in
    arm64) ABI=arm64-v8a ;;
    arm) ABI=armeabi-v7a ;;
    x64) ABI=x86_64 ;;
    x86) ABI=x86 ;;
    *) ui_print "Unsupported arch $ARCH"; abort ;;
esac
if [ -f "$MODPATH/libmq-core/$ABI/libmq-core.so" ]; then
    cp -f "$MODPATH/libmq-core/$ABI/libmq-core.so" "$MODPATH/libmq-core.so"
    chmod 644 "$MODPATH/libmq-core.so"
fi
rm -rf "$MODPATH/libmq-core"
ui_print "QQGlassBar-Zygisk installed"
