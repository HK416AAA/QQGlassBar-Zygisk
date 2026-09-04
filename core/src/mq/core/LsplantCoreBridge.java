package mq.core;

import java.lang.reflect.Member;
import java.lang.reflect.Method;

/** JNI bridge to the shared LSPlant native library (libmq-core.so). */
public final class LsplantCoreBridge {
    private LsplantCoreBridge() {
    }

    static {
        try {
            System.loadLibrary("mq-core");
        } catch (UnsatisfiedLinkError ignored) {
            // absolute-path fallback happens in CoreEngineImpl
        }
    }

    public static native void nativeInitialize();

    /** returns the backup method, or null on failure */
    public static native Method nativeHookMethod(Member target, Member callback, Object context);

    public static native boolean nativeUnhookMethod(Member target);

    public static native boolean nativeDeoptimizeMethod(Member method);
}
