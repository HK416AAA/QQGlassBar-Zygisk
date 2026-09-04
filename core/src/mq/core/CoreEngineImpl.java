package mq.core;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Member;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;

/** Single shared LSPlant hook engine with a per-member, multi-module callback list. */
public final class CoreEngineImpl implements CoreApi.CoreEngine {

    public static final CoreEngineImpl INSTANCE = new CoreEngineImpl();

    /** Force LSPlant native init; throws with the underlying cause on failure. */
    public static void ensureInit() {
        INSTANCE.ensureNative();
    }

    private CoreEngineImpl() {
    }

    private enum Kind { BEFORE, AFTER, REPLACE }

    private static final class Entry {
        final Kind kind;
        final int priority;
        final Object cb; // CoreBefore / CoreAfter / CoreReplace
        Entry(Kind kind, int priority, Object cb) {
            this.kind = kind;
            this.priority = priority;
            this.cb = cb;
        }
    }

    private static final class Holder {
        final Member member;
        final List<Entry> entries = new ArrayList<>();
        LsplantCoreToken token;

        Holder(Member member) {
            this.member = member;
        }
    }

    private final ConcurrentHashMap<Member, Holder> registry = new ConcurrentHashMap<>();
    private final Object lock = new Object();
    private Method callbackMethod;
    private boolean nativeOk;

    private static final Object UNSET = new Object();

    private void ensureNative() {
        if (nativeOk) {
            return;
        }
        synchronized (this) {
            if (nativeOk) {
                return;
            }
            try {
                if (callbackMethod == null) {
                    callbackMethod = LsplantCoreToken.class.getDeclaredMethod("callback", Object[].class);
                    callbackMethod.setAccessible(true);
                }
                loadNative();
                LsplantCoreBridge.nativeInitialize();
                nativeOk = true;
            } catch (Throwable t) {
                throw new IllegalStateException("core engine init failed", t);
            }
        }
    }

    /** load libmq-core.so via the loader's search path, then an absolute path */
    private static void loadNative() {
        UnsatisfiedLinkError last = null;
        try {
            System.loadLibrary("mq-core");
            return;
        } catch (UnsatisfiedLinkError e) {
            last = e;
        }
        StringBuilder tried = new StringBuilder();
        try {
            Object at = Class.forName("android.app.ActivityThread")
                    .getMethod("currentActivityThread").invoke(null);
            Object app = at == null ? null
                    : at.getClass().getMethod("getApplication").invoke(at);
            if (app != null) {
                java.io.File dataDir = (java.io.File) app.getClass().getMethod("getDataDir").invoke(app);
                java.io.File[] roots = {
                        new java.io.File(new java.io.File(dataDir, "files"), "qfunqaux"),
                        new java.io.File(new java.io.File(new java.io.File(dataDir, "files"),
                                "qfun_zygisk"), "libs"),
                };
                for (java.io.File root : roots) {
                    java.io.File lib = new java.io.File(root, "libmq-core.so");
                    if (!lib.isFile()) {
                        tried.append("missing:").append(lib.getAbsolutePath()).append(" ");
                        continue;
                    }
                    try {
                        System.load(lib.getAbsolutePath());
                        return;
                    } catch (UnsatisfiedLinkError e) {
                        last = e;
                        tried.append("fail:").append(lib.getAbsolutePath()).append(" (")
                                .append(e.getMessage()).append(") ");
                    }
                }
            }
        } catch (Throwable t) {
            tried.append("ctx-err:").append(t).append(" ");
        }
        throw new UnsatisfiedLinkError("mq-core load failed [loadLibrary:"
                + (last == null ? "?" : last.getMessage()) + "] [" + tried + "]");
    }

    private void check(Member member) {
        if (!(member instanceof Method) && !(member instanceof Constructor)) {
            throw new IllegalArgumentException("unsupported member " + member);
        }
    }

    private Holder holderOf(Member member) {
        return registry.computeIfAbsent(member, Holder::new);
    }

    private CoreApi.CoreHook add(Member member, Kind kind, int priority, Object cb) {
        check(member);
        ensureNative();
        final Holder h = holderOf(member);
        synchronized (h.entries) {
            if (h.token == null) {
                LsplantCoreToken tok = new LsplantCoreToken(member);
                Method backup = LsplantCoreBridge.nativeHookMethod(member, callbackMethod, tok);
                if (backup == null) {
                    throw new IllegalStateException("LSPlant hook failed: " + member);
                }
                backup.setAccessible(true);
                tok.setBackup(backup);
                h.token = tok;
            }
            Entry e = new Entry(kind, priority, cb);
            h.entries.add(e);
            h.entries.sort((a, b) -> Integer.compare(b.priority, a.priority));
            return new CoreHookImpl(h, e);
        }
    }

    private static final class CoreHookImpl implements CoreApi.CoreHook {
        final Holder holder;
        final Entry entry;

        CoreHookImpl(Holder h, Entry e) {
            holder = h;
            entry = e;
        }

        @Override
        public Member getMember() {
            return holder.member;
        }

        @Override
        public boolean isActive() {
            return holder.entries.contains(entry);
        }

        @Override
        public void unhook() {
            synchronized (holder.entries) {
                holder.entries.remove(entry);
                if (holder.entries.isEmpty()) {
                    try {
                        LsplantCoreBridge.nativeUnhookMethod(holder.member);
                    } catch (Throwable ignored) {
                    }
                    holder.token = null;
                }
            }
        }
    }

    @Override
    public CoreApi.CoreHook hookBefore(Member m, int p, CoreApi.CoreBefore cb) {
        return add(m, Kind.BEFORE, p, cb);
    }

    @Override
    public CoreApi.CoreHook hookAfter(Member m, int p, CoreApi.CoreAfter cb) {
        return add(m, Kind.AFTER, p, cb);
    }

    @Override
    public CoreApi.CoreHook hookReplace(Member m, int p, CoreApi.CoreReplace cb) {
        return add(m, Kind.REPLACE, p, cb);
    }

    @Override
    public Object invokeOriginal(Member member, Object thisObject, Object[] args) throws Throwable {
        check(member);
        Holder h = registry.get(member);
        if (h != null && h.token != null && h.token.backup != null) {
            return h.token.backup.invoke(thisObject, args);
        }
        if (member instanceof Method) {
            ((Method) member).setAccessible(true);
            return ((Method) member).invoke(thisObject, args);
        }
        ((Constructor<?>) member).setAccessible(true);
        try {
            return ((Constructor<?>) member).newInstance(args);
        } catch (InvocationTargetException ite) {
            throw ite.getTargetException();
        }
    }

    @Override
    public boolean deoptimize(Member member) {
        check(member);
        return LsplantCoreBridge.nativeDeoptimizeMethod(member);
    }

    @Override
    public void log(String msg) {
        android.util.Log.i("MQCore", msg);
    }

    /* ---------------- dispatch ---------------- */

    Object dispatch(Member member, Method backup, Object[] rawArgs) throws Throwable {
        boolean hasThis = member instanceof Constructor || (member.getModifiers() & Modifier.STATIC) == 0;
        Object receiver = hasThis ? (rawArgs.length > 0 ? rawArgs[0] : null) : null;
        Object[] args = hasThis
                ? (rawArgs.length <= 1 ? new Object[0] : java.util.Arrays.copyOfRange(rawArgs, 1, rawArgs.length))
                : rawArgs;

        Holder h = registry.get(member);
        List<Entry> entries = h == null ? java.util.Collections.emptyList()
                : synchronizedCopy(h.entries);

        List<Entry> replaces = filter(entries, Kind.REPLACE);
        if (!replaces.isEmpty()) {
            ParamImpl chain = new ParamImpl(member, receiver, args, backup);
            Object result = null;
            boolean proceeded = false;
            for (Entry e : replaces) {
                CoreApi.CoreChain c = chain.asChain();
                // first replace that doesn't call proceed wins
                Object r;
                try {
                    r = ((CoreApi.CoreReplace) e.cb).replace(c);
                } catch (Throwable t) {
                    throw unwrap(t);
                }
                result = r;
                if (!chain.proceeded) {
                    break;
                }
            }
            return cast(member, result);
        }

        ParamImpl param = new ParamImpl(member, receiver, args, backup);
        for (Entry e : filter(entries, Kind.BEFORE)) {
            try {
                ((CoreApi.CoreBefore) e.cb).before(param);
            } catch (Throwable t) {
                android.util.Log.e("MQCore", "before cb error", t);
            }
            // QAux-style: keep running every before callback (each callback
            // pairs its own before/after); just remember to skip the origin if
            // any before set a result/throwable.
        }
        if (!param.skip) {
            try {
                param.result = backup.invoke(receiver, param.args);
            } catch (Throwable t) {
                param.throwable = unwrap(t);
            }
        }
        List<Entry> after = filter(entries, Kind.AFTER);
        for (int i = after.size() - 1; i >= 0; i--) {
            try {
                ((CoreApi.CoreAfter) after.get(i).cb).after(param);
            } catch (Throwable t) {
                android.util.Log.e("MQCore", "after cb error", t);
            }
        }
        if (param.throwable != null) {
            throw param.throwable;
        }
        return cast(member, param.result);
    }

    private static List<Entry> synchronizedCopy(List<Entry> list) {
        synchronized (list) {
            return new ArrayList<>(list);
        }
    }

    private static List<Entry> filter(List<Entry> list, Kind kind) {
        List<Entry> out = new ArrayList<>();
        for (Entry e : list) {
            if (e.kind == kind) {
                out.add(e);
            }
        }
        return out;
    }

    private static Throwable unwrap(Throwable t) {
        return (t instanceof InvocationTargetException && t.getCause() != null)
                ? t.getCause() : t;
    }

    private static Object cast(Member member, Object result) {
        if (member instanceof Constructor) {
            return null;
        }
        Class<?> rt = ((Method) member).getReturnType();
        if (rt == Void.TYPE) {
            return null;
        }
        if (!rt.isPrimitive()) {
            return result;
        }
        return result;
    }

    private static final class ParamImpl implements CoreApi.CoreParam, CoreApi.CoreChain {
        private final Member member;
        private final Object receiver;
        private final Method backup;
        Object[] args;
        Object result = UNSET;
        Throwable throwable;
        boolean skip;
        boolean proceeded;
        Object extra;

        ParamImpl(Member member, Object receiver, Object[] args, Method backup) {
            this.member = member;
            this.receiver = receiver;
            this.args = args;
            this.backup = backup;
        }

        CoreApi.CoreChain asChain() {
            return this;
        }

        @Override
        public Member getMember() {
            return member;
        }

        @Override
        public Object getThisObject() {
            return receiver;
        }

        @Override
        public Object[] getArgs() {
            return args;
        }

        @Override
        public void setArgs(Object[] args) {
            this.args = args;
        }

        @Override
        public Object getResult() {
            return result == UNSET ? null : result;
        }

        @Override
        public void setResult(Object r) {
            this.result = r;
            this.skip = true;
        }

        @Override
        public Throwable getThrowable() {
            return throwable;
        }

        @Override
        public Object getExtra() {
            return extra;
        }

        @Override
        public void setExtra(Object extra) {
            this.extra = extra;
        }

        @Override
        public void setThrowable(Throwable t) {
            this.throwable = t;
            this.skip = true;
        }

        @Override
        public Object proceed(Object[] args) throws Throwable {
            if (proceeded) {
                throw new IllegalStateException("proceed() twice");
            }
            proceeded = true;
            this.args = args;
            try {
                Object r = backup.invoke(receiver, args);
                result = r;
                return r;
            } catch (Throwable t) {
                throw unwrap(t);
            }
        }
    }
}
