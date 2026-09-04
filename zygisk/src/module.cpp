/*
 * QQGlassBar-Zygisk: single-payload Zygisk container for QQ.
 * Loads the shared LSPlant core (core.dex + libmq-core.so) and the glass-bar
 * payload (glassbar.apk), then boots me.glassbar.hook.GlassBarEntry.
 */
#include <android/log.h>
#include <cerrno>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

#define LOG_TAG "QQGlassBar"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef GB_VERCODE
#define GB_VERCODE 1
#endif

#if defined(__aarch64__)
#define GB_ABI "arm64-v8a"
#elif defined(__arm__)
#define GB_ABI "armeabi-v7a"
#elif defined(__x86_64__)
#define GB_ABI "x86_64"
#else
#define GB_ABI "x86"
#endif

static bool isQQTarget(const std::string &proc, std::string *pkg) {
    const char *pkgs[] = {"com.tencent.mobileqq", "com.tencent.tim"};
    for (auto p : pkgs) {
        std::string s(p);
        if (proc == s) { if (pkg) *pkg = s; return true; }
        if (proc.size() > s.size() + 1 && proc.compare(0, s.size() + 1, s + ":") == 0) {
            if (pkg) *pkg = s;
            return true;
        }
    }
    return false;
}

static bool stageFile(int fd, const char *dst) {
    struct stat a{}, b{};
    bool need = true;
    if (fstat(fd, &a) == 0 && stat(dst, &b) == 0 && a.st_size == b.st_size) {
        char x[512], y[512];
        size_t n = a.st_size > 512 ? 512 : a.st_size;
        lseek(fd, 0, SEEK_SET);
        if (read(fd, x, n) == (ssize_t)n) {
            int f2 = open(dst, O_RDONLY | O_CLOEXEC);
            if (f2 >= 0) {
                need = read(f2, y, n) != (ssize_t)n || memcmp(x, y, n) != 0;
                close(f2);
            }
        }
    }
    if (!need) return true;
    std::string tmp = std::string(dst) + ".t";
    lseek(fd, 0, SEEK_SET);
    int out = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) return false;
    char buf[65536]; ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) if (write(out, buf, r) != r) { close(out); return false; }
    close(out);
    if (r < 0) return false;
    rename(tmp.c_str(), dst);
    return true;
}

class GbModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *a, JNIEnv *e) override { api = a; env = e; if (e) e->GetJavaVM(&vm); }
    void preAppSpecialize(AppSpecializeArgs *args) override {
        inject = false;
        if (!args || !args->nice_name || !args->app_data_dir) return;
        std::string proc;
        { const char *n = env->GetStringUTFChars(args->nice_name, nullptr); if (n) { proc = n; env->ReleaseStringUTFChars(args->nice_name, n); } }
        std::string pkg;
        if (!isQQTarget(proc, &pkg)) return;
        if (proc != pkg) { std::string sfx = proc.substr(pkg.size()); if (sfx != ":MSF" && sfx != ":peak") return; }
        mfd = api->getModuleDir();
        if (mfd < 0) return;
        const char *dd = env->GetStringUTFChars(args->app_data_dir, nullptr);
        if (!dd) return;
        appData = dd; env->ReleaseStringUTFChars(args->app_data_dir, dd);
        coreFd = openat(mfd, "core.dex", O_RDONLY | O_CLOEXEC);
        if (coreFd >= 0) api->exemptFd(coreFd);
        { std::string lp = std::string("libmq-core/") + GB_ABI + "/libmq-core.so";
          libFd = openat(mfd, lp.c_str(), O_RDONLY | O_CLOEXEC);
          if (libFd < 0) libFd = openat(mfd, "libmq-core.so", O_RDONLY | O_CLOEXEC);
          if (libFd >= 0) api->exemptFd(libFd); }
        payloadFd = openat(mfd, "glassbar.apk", O_RDONLY | O_CLOEXEC);
        if (payloadFd >= 0) api->exemptFd(payloadFd);
        inject = true;
    }
    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!inject) { api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); return; }
        pthread_t th; pthread_attr_t at;
        pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, [](void *x) -> void * { static_cast<GbModule *>(x)->run(); return nullptr; }, this) != 0) LOGE("thread fail");
        pthread_attr_destroy(&at);
    }
    void preServerSpecialize(ServerSpecializeArgs *) override { inject = false; }
    void postServerSpecialize(const ServerSpecializeArgs *) override {}

private:
    Api *api = nullptr; JNIEnv *env = nullptr; static JavaVM *vm;
    bool inject = false; int mfd = -1, coreFd = -1, libFd = -1, payloadFd = -1;
    std::string appData;

    void run() {
        JavaVM *j = vm; if (!j) { closeAll(); return; }
        JNIEnv *e = nullptr;
        if (j->GetEnv((void **)&e, JNI_VERSION_1_6) != JNI_OK) {
            if (j->AttachCurrentThread(&e, nullptr) != JNI_OK) { closeAll(); return; }
        }
        if (stageCore(e)) {
            std::string dir = appData + "/files/glassbar";
            mkdir(dir.c_str(), 0700);
            std::string apk = dir + "/glassbar.apk";
            if (stageFile(payloadFd, apk.c_str())) loadAndStart(e, apk);
        }
        j->DetachCurrentThread();
        closeAll();
    }
    void closeAll() {
        if (coreFd >= 0) close(coreFd);
        if (libFd >= 0) close(libFd);
        if (payloadFd >= 0) close(payloadFd);
    }
    bool stageCore(JNIEnv *e) {
        if (coreFd < 0) return false;
        std::string dir = appData + "/files/qfunqaux"; mkdir(dir.c_str(), 0700);
        std::string libsDir = appData + "/files/qfun_zygisk/libs"; mkdir(libsDir.c_str(), 0700);
        if (!stageFile(coreFd, (dir + "/core.dex").c_str())) return false;
        if (libFd >= 0) stageFile(libFd, (libsDir + "/libmq-core.so").c_str());
        return true;
    }
    void loadAndStart(JNIEnv *e, const std::string &apk) {
        // core loader (librarySearchPath = qfun_zygisk/libs so loadLibrary works)
        std::string libsDir = appData + "/files/qfun_zygisk/libs";
        jobject core = makeLoader(e, appData + "/files/qfunqaux/core.dex", libsDir.c_str(), nullptr);
        jobject pay = makeLoader(e, apk.c_str(), libsDir.c_str(), core);
        if (!pay) { if (e->ExceptionCheck()) e->ExceptionClear(); return; }
        jclass clCL = e->FindClass("java/lang/ClassLoader");
        jmethodID load = e->GetMethodID(clCL, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring nm = e->NewStringUTF("me.glassbar.hook.GlassBarEntry");
        jclass cls = (jclass)e->CallObjectMethod(pay, load, nm);
        if (!cls) { if (e->ExceptionCheck()) e->ExceptionClear(); return; }
        jmethodID m = e->GetStaticMethodID(cls, "startWithCore", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
        if (!m) return;
        jstring ap = e->NewStringUTF(apk.c_str());
        jstring dd = e->NewStringUTF(appData.c_str());
        LOGI("invoke GlassBarEntry.startWithCore");
        e->CallStaticVoidMethod(cls, m, ap, dd, core);
        if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); }
    }
    jobject makeLoader(JNIEnv *e, const char *dex, const char *libdir, jobject parent) {
        jclass c = e->FindClass("dalvik/system/DexClassLoader");
        if (!c) return nullptr;
        jmethodID ctor = e->GetMethodID(c, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
        if (!ctor) return nullptr;
        jstring jd = e->NewStringUTF(dex);
        jstring jl = libdir ? e->NewStringUTF(libdir) : nullptr;
        return e->NewObject(c, ctor, jd, nullptr, jl, parent);
    }
};
JavaVM *GbModule::vm = nullptr;
REGISTER_ZYGISK_MODULE(GbModule)
