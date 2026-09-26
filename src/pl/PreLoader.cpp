#include <jni.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <dlfcn.h>
#include <filesystem>
#include <fcntl.h>
#include <optional>
#include <unistd.h>
#include <dirent.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pl/Logger.hpp"
#include "pl/internal/ModManager.h"
#include "pl/memory/Hook.hpp"
#include "pl/runtime/GameHooks.h"
#include "pl/runtime/JavaRuntime.h"
#include "pl/runtime/ModMenuBridge.h"

namespace {

// Some external mods (e.g. BedrockTools) gate their installation on the
// launcher process name read from /proc/self/cmdline, only accepting
// org.levimc.launcher / org.levimc.launcher:minecraft / com.mojang.minecraftpe.
// When BreezeLauncher ships under a different applicationId (e.g. the
// com.wisebreeze.launcher disguise build), those mods silently no-op.
// To keep them working without touching mod code, rewrite argv[0] (which
// backs /proc/self/cmdline) to org.levimc.launcher at preloader load time.
//
// This only affects /proc/self/cmdline. Application.getProcessName() reads
// the ActivityThread binder cache, not this file, so Java-side process name
// checks (CrashReporter :crash guard, etc.) are unaffected. Process model,
// applicationId, package name, permissions and Firebase identity are all
// untouched.
bool g_compatModeEnabled = true;

void fakeLauncherIdentityIfNeeded() {
  if (!g_compatModeEnabled) return;
  char current[256] = {0};
  int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  ssize_t n = read(fd, current, sizeof(current) - 1);
  close(fd);
  if (n <= 0) return;

  // Already presenting as org.levimc.launcher* — nothing to do (default build).
  if (std::strncmp(current, "org.levimc.launcher", 19) == 0) return;

  // bionic libc exports __progname, which points directly at the argv[0]
  // buffer (no copy). Overwriting that buffer rewrites /proc/self/cmdline.
  char **prognameVar = static_cast<char **>(dlsym(RTLD_DEFAULT, "__progname"));
  if (!prognameVar || !*prognameVar) return;

  char *argv0 = *prognameVar;
  size_t currentLen = std::strlen(argv0);
  const char *fake = "org.levimc.launcher";
  size_t fakeLen = std::strlen(fake);
  // argv[0] buffer is sized for the current process name; only overwrite when
  // the fake name fits (e.g. com.wisebreeze.launcher[23] -> org.levimc.launcher[19]).
  if (fakeLen >= currentLen) return;

  std::memset(argv0, 0, currentLen);
  std::strncpy(argv0, fake, fakeLen);

  // Keep /proc/self/comm (15-char limit) consistent too.
  char comm[16] = {0};
  std::strncpy(comm, fake, 15);
  prctl(PR_SET_NAME, comm);
}

// ── Filesystem path redirection ─────────────────────────────────────────
//
// Third-party native mods (e.g. BlockPlacementAnimation) hard-code resource
// paths under /storage/emulated/0/Android/{data,media}/org.levimc.launcher/...
// BreezeLauncher ships under com.wisebreeze.launcher, so those paths do not
// exist and the mod silently scans 0 resource packs.
//
// When compat mode is enabled, inline-hook the libc filesystem entry points
// (open, openat, opendir, stat-family, access-family, __open_2, __openat_2)
// and rewrite any path argument whose prefix is
//   "/storage/emulated/0/Android/data/org.levimc.launcher/"
//   "/storage/emulated/0/Android/media/org.levimc.launcher/"
// to the com.wisebreeze.launcher equivalent. The suffix after the package
// name is preserved verbatim.
//
// The hooks are installed once (idempotent) right after the identity rewrite,
// before any external mod .so is dlopen-ed, so every filesystem call made by
// the mod or by libpreloader's own libc++ (which the mod links against) is
// intercepted.

constexpr const char *kLegacyDataPrefix =
    "/storage/emulated/0/Android/data/org.levimc.launcher/";
constexpr const char *kBreezeDataPrefix =
    "/storage/emulated/0/Android/data/com.wisebreeze.launcher/";
constexpr const char *kLegacyMediaPrefix =
    "/storage/emulated/0/Android/media/org.levimc.launcher/";
constexpr const char *kBreezeMediaPrefix =
    "/storage/emulated/0/Android/media/com.wisebreeze.launcher/";

// Thread-local buffer for the rewritten path. Sized to PATH_MAX (4096).
thread_local char g_redirectBuf[4096];

// If `path` starts with one of the legacy org.levimc.launcher prefixes,
// copy the rewritten com.wisebreeze.launcher path into g_redirectBuf and
// return a pointer to it. Otherwise return `path` unchanged.
const char *RedirectPathIfNeeded(const char *path) {
  if (!path) return path;
  // Fast reject: only consider absolute paths under /storage.
  if (path[0] != '/' || path[1] != 's') return path;

  const char *legacyPrefix = nullptr;
  const char *breezePrefix = nullptr;
  size_t prefixLen = 0;

  constexpr size_t dataLegacyLen = 47;  // strlen(kLegacyDataPrefix)
  constexpr size_t mediaLegacyLen = 48; // strlen(kLegacyMediaPrefix)

  if (std::strncmp(path, kLegacyDataPrefix, dataLegacyLen) == 0) {
    legacyPrefix = kLegacyDataPrefix;
    breezePrefix = kBreezeDataPrefix;
    prefixLen = dataLegacyLen;
  } else if (std::strncmp(path, kLegacyMediaPrefix, mediaLegacyLen) == 0) {
    legacyPrefix = kLegacyMediaPrefix;
    breezePrefix = kBreezeMediaPrefix;
    prefixLen = mediaLegacyLen;
  } else {
    return path;
  }

  const size_t breezePrefixLen = std::strlen(breezePrefix);
  const char *suffix = path + prefixLen;
  const size_t suffixLen = std::strlen(suffix);
  // breezePrefix (50) + suffix (up to ~4046) must fit in g_redirectBuf.
  if (breezePrefixLen + suffixLen >= sizeof(g_redirectBuf)) {
    return path;  // too long; leave untouched rather than truncate
  }
  std::memcpy(g_redirectBuf, breezePrefix, breezePrefixLen);
  std::memcpy(g_redirectBuf + breezePrefixLen, suffix, suffixLen);
  g_redirectBuf[breezePrefixLen + suffixLen] = '\0';
  return g_redirectBuf;
}

// Original function pointers, filled in by pl::memory::hook.
int (*orig_open)(const char *, int, ...) = nullptr;
int (*orig_openat)(int, const char *, int, ...) = nullptr;
int (*orig___open_2)(const char *, int) = nullptr;
int (*orig___openat_2)(int, const char *, int) = nullptr;
DIR *(*orig_opendir)(const char *) = nullptr;
int (*orig_access)(const char *, int) = nullptr;
int (*orig_faccessat)(int, const char *, int, int) = nullptr;
int (*orig_stat)(const char *, struct stat *) = nullptr;
int (*orig_lstat)(const char *, struct stat *) = nullptr;
int (*orig_fstatat)(int, const char *, struct stat *, int) = nullptr;
FILE *(*orig_fopen)(const char *, const char *) = nullptr;

int hook_open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = static_cast<mode_t>(va_arg(ap, int));
    va_end(ap);
  }
  return orig_open(RedirectPathIfNeeded(path), flags, mode);
}

int hook_openat(int dirfd, const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = static_cast<mode_t>(va_arg(ap, int));
    va_end(ap);
  }
  return orig_openat(dirfd, RedirectPathIfNeeded(path), flags, mode);
}

int hook___open_2(const char *path, int flags) {
  return orig___open_2(RedirectPathIfNeeded(path), flags);
}

int hook___openat_2(int dirfd, const char *path, int flags) {
  return orig___openat_2(dirfd, RedirectPathIfNeeded(path), flags);
}

DIR *hook_opendir(const char *path) {
  return orig_opendir(RedirectPathIfNeeded(path));
}

int hook_access(const char *path, int mode) {
  return orig_access(RedirectPathIfNeeded(path), mode);
}

int hook_faccessat(int dirfd, const char *path, int mode, int flags) {
  return orig_faccessat(dirfd, RedirectPathIfNeeded(path), mode, flags);
}

int hook_stat(const char *path, struct stat *buf) {
  return orig_stat(RedirectPathIfNeeded(path), buf);
}

int hook_lstat(const char *path, struct stat *buf) {
  return orig_lstat(RedirectPathIfNeeded(path), buf);
}

int hook_fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
  return orig_fstatat(dirfd, RedirectPathIfNeeded(path), buf, flags);
}

FILE *hook_fopen(const char *path, const char *mode) {
  return orig_fopen(RedirectPathIfNeeded(path), mode);
}

std::once_flag g_pathRedirectOnce;

void InstallPathRedirectHooks() {
  std::call_once(g_pathRedirectOnce, [] {
    // Resolve libc.so symbols. On Android, libc.so is always loaded and
    // dlsym(RTLD_DEFAULT, ...) finds the canonical entry points.
    auto tryHook = [](const char *name, void *detour, void **original) {
      void *target = dlsym(RTLD_DEFAULT, name);
      if (!target) {
        preloaderLogger.warn("Path redirect: symbol {} not found, skipping",
                            name);
        return;
      }
      if (pl::memory::hook(target, detour, original) != 0) {
        preloaderLogger.warn("Path redirect: failed to hook {}", name);
      }
    };
    tryHook("open", reinterpret_cast<void *>(hook_open),
            reinterpret_cast<void **>(&orig_open));
    tryHook("openat", reinterpret_cast<void *>(hook_openat),
            reinterpret_cast<void **>(&orig_openat));
    tryHook("__open_2", reinterpret_cast<void *>(hook___open_2),
            reinterpret_cast<void **>(&orig___open_2));
    tryHook("__openat_2", reinterpret_cast<void *>(hook___openat_2),
            reinterpret_cast<void **>(&orig___openat_2));
    tryHook("opendir", reinterpret_cast<void *>(hook_opendir),
            reinterpret_cast<void **>(&orig_opendir));
    tryHook("access", reinterpret_cast<void *>(hook_access),
            reinterpret_cast<void **>(&orig_access));
    tryHook("faccessat", reinterpret_cast<void *>(hook_faccessat),
            reinterpret_cast<void **>(&orig_faccessat));
    tryHook("stat", reinterpret_cast<void *>(hook_stat),
            reinterpret_cast<void **>(&orig_stat));
    tryHook("lstat", reinterpret_cast<void *>(hook_lstat),
            reinterpret_cast<void **>(&orig_lstat));
    tryHook("fstatat", reinterpret_cast<void *>(hook_fstatat),
            reinterpret_cast<void **>(&orig_fstatat));
    tryHook("fopen", reinterpret_cast<void *>(hook_fopen),
            reinterpret_cast<void **>(&orig_fopen));
    preloaderLogger.info("Path redirect hooks installed (compat mode)");
  });
}

jboolean LoadModFromJava(JNIEnv *env, jstring libPath, jstring modRootPath) {
  JavaVM *vm = pl::runtime::GetJavaVm();
  if (!vm) {
    preloaderLogger.error("JavaVM is not initialized");
    return JNI_FALSE;
  }

  const char *path = env->GetStringUTFChars(libPath, nullptr);
  if (!path) {
    preloaderLogger.error("Failed to access mod library path");
    return JNI_FALSE;
  }

  std::optional<std::filesystem::path> sourceModDirectory;
  const char *sourcePath = nullptr;
  if (modRootPath) {
    sourcePath = env->GetStringUTFChars(modRootPath, nullptr);
    if (!sourcePath) {
      env->ReleaseStringUTFChars(libPath, path);
      preloaderLogger.error("Failed to access original mod root path");
      return JNI_FALSE;
    }
    sourceModDirectory = std::filesystem::path(sourcePath);
  }

  const bool loaded = ModManager::LoadModLibrary(path, sourceModDirectory, vm);

  if (sourcePath) {
    env->ReleaseStringUTFChars(modRootPath, sourcePath);
  }
  env->ReleaseStringUTFChars(libPath, path);
  return loaded ? JNI_TRUE : JNI_FALSE;
}

} // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;
  pl::runtime::SetJavaVm(vm);
  return JNI_VERSION_1_4;
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_ModManager_nativeSetCompatMode(
    JNIEnv *env, jclass clazz, jboolean enabled) {
  (void)env;
  (void)clazz;
  g_compatModeEnabled = enabled == JNI_TRUE;
  // Apply the identity rewrite immediately so it is in place before any
  // external mod is dlopen-ed via nativeLoadMod. Called right after
  // System.loadLibrary("preloader") from the app-side ModManager.
  fakeLauncherIdentityIfNeeded();
  // In compat mode, also install filesystem path redirection so that mods
  // hard-coding org.levimc.launcher paths find the com.wisebreeze.launcher
  // equivalents. Idempotent via std::call_once.
  if (g_compatModeEnabled) {
    InstallPathRedirectHooks();
  }
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_ModManager_nativeLoadMod__Ljava_lang_String_2Lorg_levimc_launcher_core_mods_Mod_2(
    JNIEnv *env, jclass clazz, jstring libPath, jobject modObj) {
  (void)clazz;
  (void)modObj;
  return LoadModFromJava(env, libPath, nullptr);
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_ModManager_nativeLoadMod__Ljava_lang_String_2Ljava_lang_String_2Lorg_levimc_launcher_core_mods_Mod_2(
    JNIEnv *env, jclass clazz, jstring libPath, jstring modRootPath,
    jobject modObj) {
  (void)clazz;
  (void)modObj;
  return LoadModFromJava(env, libPath, modRootPath);
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_ModManager_nativeEnableLoadedMods(
    JNIEnv *env, jclass clazz) {
  (void)env;
  (void)clazz;
  ModManager::EnableLoadedMods();
  pl::runtime::InitGameHooks();
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_ModManager_nativeDisableAndUnloadLoadedMods(
    JNIEnv *env, jclass clazz) {
  (void)env;
  (void)clazz;
  ModManager::DisableAndUnloadLoadedMods();
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_minecraft_MinecraftRuntimePreparer_nativeSetupRuntime(
    JNIEnv *env, jclass clazz, jstring modsPath) {
  (void)clazz;
  if (!modsPath) {
    return;
  }

  const char *path = env->GetStringUTFChars(modsPath, nullptr);
  if (!path) {
    return;
  }

  preloaderLogger.debug("Native runtime mod directory: {}", path);
  env->ReleaseStringUTFChars(modsPath, path);
}

} // extern "C"
