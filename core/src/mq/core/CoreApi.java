package mq.core;

import java.lang.reflect.Member;

/** Shared (module-agnostic) hook API used by the single-engine container. */
public final class CoreApi {

    private CoreApi() {
    }

    public interface CoreParam {
        Member getMember();
        Object getThisObject();
        Object[] getArgs();
        void setArgs(Object[] args);
        Object getResult();
        void setResult(Object result);
        Throwable getThrowable();
        void setThrowable(Throwable t);
        Object getExtra();
        void setExtra(Object extra);
    }

    public interface CoreChain extends CoreParam {
        /** invoke the original method; may only be called once */
        Object proceed(Object[] args) throws Throwable;
    }

    public interface CoreBefore {
        void before(CoreParam param) throws Throwable;
    }

    public interface CoreAfter {
        void after(CoreParam param) throws Throwable;
    }

    public interface CoreReplace {
        /** if proceed() is not called, the returned value is the result */
        Object replace(CoreChain chain) throws Throwable;
    }

    public interface CoreHook {
        Member getMember();
        boolean isActive();
        void unhook();
    }

    public interface CoreEngine {
        CoreHook hookBefore(Member member, int priority, CoreBefore cb);
        CoreHook hookAfter(Member member, int priority, CoreAfter cb);
        CoreHook hookReplace(Member member, int priority, CoreReplace cb);
        Object invokeOriginal(Member member, Object thisObject, Object[] args) throws Throwable;
        boolean deoptimize(Member member);
        void log(String msg);
    }
}
