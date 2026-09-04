/*
 * QFun-Zygisk native glue: embeds LSPlant (ART hook) with Dobby as the inline
 * hooker, exposes the JNI surface for me.yxp.qfun.loader.hookapi.LsplantBridge,
 * and provides a self-contained libart.so symbol resolver (reads the on-disk
 * ELF and resolves against the in-memory load bias via dl_iterate_phdr).
 *
 * Stage 2b-B: real resolver (replaces the Stage 2b-A placeholder).
 */

#include <android/log.h>
#include <elf.h>
#include <jni.h>
#include <link.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

#include "dobby.h"
#include "lsplant.hpp"

#define LOG_TAG "MQCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

bool gLsplantReady = false;

/* ---------------- libart symbol resolver ---------------- */

#if defined(__aarch64__)
using QfEhdr = Elf64_Ehdr;
using QfShdr = Elf64_Shdr;
using QfSym = Elf64_Sym;
#define QF_ELF_CLASS ELFCLASS64
#elif defined(__arm__)
using QfEhdr = Elf32_Ehdr;
using QfShdr = Elf32_Shdr;
using QfSym = Elf32_Sym;
#define QF_ELF_CLASS ELFCLASS32
#else
#error "unsupported ABI"
#endif

struct ArtSymbolTables {
    const uint8_t *map = nullptr;
    size_t mapSize = 0;
    uintptr_t loadBias = 0;  // dlpi_addr
    // dynsym / symtab
    const QfSym *dynsym = nullptr;
    size_t dynsymCount = 0;
    const char *dynstr = nullptr;
    const QfSym *symtab = nullptr;
    size_t symtabCount = 0;
    const char *strtab = nullptr;
};

static ArtSymbolTables gArt;
static std::once_flag gArtOnce;
static std::unordered_map<std::string, void *> gCache;

static int findArtModule(struct dl_phdr_info *info, size_t /*size*/, void * /*data*/) {
    const char *name = info->dlpi_name;
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
    if (strstr(name, "libart.so") == nullptr) {
        return 0;
    }
    gArt.loadBias = static_cast<uintptr_t>(info->dlpi_addr);
    return 1;
}

static void mapArtTables() {
    dl_iterate_phdr(findArtModule, nullptr);
    if (gArt.loadBias == 0) {
        return;
    }
    // dlpi_name is also the on-disk path (e.g. /apex/com.android.art/lib64/libart.so)
    const char *path = nullptr;
    // We did not keep the name; re-run to capture it.
    struct NameCapture {
        const char *name = nullptr;
    } nc;
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *data) -> int {
        auto *n = static_cast<NameCapture *>(data);
        const char *nm = info->dlpi_name;
        if (nm != nullptr && nm[0] != '\0' && strstr(nm, "libart.so") != nullptr) {
            n->name = nm;
            return 1;
        }
        return 0;
    }, &nc);
    path = nc.name;
    if (path == nullptr) {
        return;
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("resolver: open %s failed errno=%d", path, errno);
        return;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return;
    }
    size_t size = static_cast<size_t>(st.st_size);
    void *m = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        LOGE("resolver: mmap %s failed", path);
        return;
    }
    auto *ehdr = static_cast<const QfEhdr *>(m);
    if (size < sizeof(QfEhdr) || memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != QF_ELF_CLASS) {
        munmap(m, size);
        LOGE("resolver: bad ELF %s", path);
        return;
    }
    gArt.map = static_cast<const uint8_t *>(m);
    gArt.mapSize = size;

    size_t shOff = static_cast<size_t>(ehdr->e_shoff);
    size_t shNum = ehdr->e_shnum;
    size_t shEntSize = ehdr->e_shentsize == 0 ? sizeof(QfShdr) : ehdr->e_shentsize;
    if (shOff == 0 || shEntSize < sizeof(QfShdr) ||
        shOff + shNum * shEntSize > size) {
        LOGE("resolver: bad section headers in %s", path);
        return;
    }
    for (size_t i = 0; i < shNum; i++) {
        const auto *sh = reinterpret_cast<const QfShdr *>(gArt.map + shOff + i * shEntSize);
        if (sh->sh_type == SHT_DYNSYM) {
            size_t off = static_cast<size_t>(sh->sh_offset);
            if (off + sh->sh_size <= size) {
                gArt.dynsym = reinterpret_cast<const QfSym *>(gArt.map + off);
                gArt.dynsymCount = sh->sh_entsize ? sh->sh_size / sh->sh_entsize : 0;
                if (sh->sh_link < shNum) {
                    const auto *ls = reinterpret_cast<const QfShdr *>(
                        gArt.map + shOff + sh->sh_link * shEntSize);
                    gArt.dynstr = reinterpret_cast<const char *>(gArt.map + ls->sh_offset);
                }
            }
        } else if (sh->sh_type == SHT_SYMTAB) {
            size_t off = static_cast<size_t>(sh->sh_offset);
            if (off + sh->sh_size <= size) {
                gArt.symtab = reinterpret_cast<const QfSym *>(gArt.map + off);
                gArt.symtabCount = sh->sh_entsize ? sh->sh_size / sh->sh_entsize : 0;
                if (sh->sh_link < shNum) {
                    const auto *ls = reinterpret_cast<const QfShdr *>(
                        gArt.map + shOff + sh->sh_link * shEntSize);
                    gArt.strtab = reinterpret_cast<const char *>(gArt.map + ls->sh_offset);
                }
            }
        }
    }
    LOGI("resolver: libart mapped, bias=%#zx dynsym=%zu symtab=%zu",
         (size_t)gArt.loadBias, gArt.dynsymCount, gArt.symtabCount);
}

static void ensureArtTables() {
    std::call_once(gArtOnce, mapArtTables);
}

static void *resolveInTable(const QfSym *tab, size_t count, const char *str,
                            std::string_view name, bool prefix) {
    if (tab == nullptr || str == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count; i++) {
        const QfSym &s = tab[i];
        if (s.st_name == 0) {
            continue;
        }
        const char *symName = str + s.st_name;
        bool hit = prefix ? strncmp(symName, name.data(), name.size()) == 0
                          : strcmp(symName, name.data()) == 0;
        if (!hit) {
            continue;
        }
        uintptr_t addr = static_cast<uintptr_t>(s.st_value);
        if (s.st_shndx != SHN_ABS) {
            addr += gArt.loadBias;
        }
        return reinterpret_cast<void *>(addr);
    }
    return nullptr;
}

void *ResolveArtSymbol(std::string_view name) {
    ensureArtTables();
    auto it = gCache.find(std::string(name));
    if (it != gCache.end()) {
        return it->second;
    }
    void *addr = resolveInTable(gArt.dynsym, gArt.dynsymCount, gArt.dynstr, name, false);
    if (addr == nullptr) {
        addr = resolveInTable(gArt.symtab, gArt.symtabCount, gArt.strtab, name, false);
    }
    if (addr != nullptr) {
        gCache[std::string(name)] = addr;
    }
    return addr;
}

void *ResolveArtSymbolPrefix(std::string_view prefix) {
    ensureArtTables();
    void *addr = resolveInTable(gArt.dynsym, gArt.dynsymCount, gArt.dynstr, prefix, true);
    if (addr == nullptr) {
        addr = resolveInTable(gArt.symtab, gArt.symtabCount, gArt.strtab, prefix, true);
    }
    return addr;
}

/* ---------------- Dobby inline hook ---------------- */

void *InlineHook(void *target, void *hooker) {
    if (target == nullptr) {
        return nullptr;
    }
    dobby_dummy_func_t origin = nullptr;
    int rc = DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(hooker), &origin);
    if (rc == 0 && origin != nullptr) {
        return reinterpret_cast<void *>(origin);
    }
    return nullptr;
}

bool InlineUnhook(void *func) {
    if (func == nullptr) {
        return false;
    }
    return DobbyDestroy(func) == 0;
}

bool InitializeLsplant(JNIEnv *env) {
    if (gLsplantReady) {
        return true;
    }
    ::lsplant::InitInfo info;
    info.inline_hooker = InlineHook;
    info.inline_unhooker = InlineUnhook;
    info.art_symbol_resolver = ResolveArtSymbol;
    info.art_symbol_prefix_resolver = ResolveArtSymbolPrefix;
    bool ok = ::lsplant::Init(env, info);
    gLsplantReady = ok;
    if (!ok) {
        LOGE("lsplant init failed");
    }
    return ok;
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_mq_core_LsplantCoreBridge_nativeInitialize(JNIEnv *env, jclass) {
    if (!InitializeLsplant(env)) {
        // Build a diagnostic message so failures can be read from boot_status
        // without logcat.
        char buf[1024];
        int n = 0;
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                      "lsplant init failed (mapped=%d bias=%#zx dynsym=%zu symtab=%zu ",
                      gArt.map != nullptr, (size_t)gArt.loadBias,
                      gArt.dynsymCount, gArt.symtabCount);
        // probe a few well-known ART symbols to tell whether parsing works at all
        const char *probes[] = {
                "_ZN3art7Runtime9instanceEv",
                "art_quick_generic_jni_trampoline",
        };
        for (const char *p : probes) {
            void *addr = ResolveArtSymbol(std::string_view(p));
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s=%p ",
                          p, addr);
        }
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, ")");
        LOGE("%s", buf);
        jclass ex = env->FindClass("java/lang/RuntimeException");
        if (ex != nullptr) {
            env->ThrowNew(ex, buf);
        }
        return;
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_mq_core_LsplantCoreBridge_nativeHookMethod(
        JNIEnv *env, jclass, jobject target, jobject callback, jobject context) {
    if (!gLsplantReady) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
                      "lsplant not initialized");
        return nullptr;
    }
    return ::lsplant::Hook(env, target, context, callback);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_mq_core_LsplantCoreBridge_nativeIsMethodHooked(JNIEnv *env, jclass,
                                                                   jobject target) {
    if (!gLsplantReady) {
        return JNI_FALSE;
    }
    return ::lsplant::IsHooked(env, target) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_mq_core_LsplantCoreBridge_nativeUnhookMethod(JNIEnv *env, jclass,
                                                                 jobject target) {
    if (!gLsplantReady) {
        return JNI_FALSE;
    }
    return ::lsplant::UnHook(env, target) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_mq_core_LsplantCoreBridge_nativeDeoptimizeMethod(JNIEnv *env, jclass,
                                                                     jobject method) {
    if (!gLsplantReady) {
        return JNI_FALSE;
    }
    return ::lsplant::Deoptimize(env, method) ? JNI_TRUE : JNI_FALSE;
}
