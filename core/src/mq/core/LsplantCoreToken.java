package mq.core;

import java.lang.reflect.Member;
import java.lang.reflect.Method;

/** LSPlant hook context token; native invokes {@link #callback(Object[])}. */
public final class LsplantCoreToken {
    private final Member member;
    Method backup; // package-private access from CoreEngineImpl

    LsplantCoreToken(Member member) {
        this.member = member;
    }

    void setBackup(Method m) {
        this.backup = m;
    }

    public Object callback(Object[] rawArgs) throws Throwable {
        if (backup == null) {
            throw new IllegalStateException("backup not set");
        }
        return CoreEngineImpl.INSTANCE.dispatch(member, backup, rawArgs);
    }
}
