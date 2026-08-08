#include <jni.h>

#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fcntl.h>
#include <optional>
#include <unistd.h>
#include <sys/prctl.h>

#include "pl/Logger.hpp"
#include "pl/internal/ModManager.h"
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
void fakeLauncherIdentityIfNeeded() {
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
  fakeLauncherIdentityIfNeeded();
  pl::runtime::SetJavaVm(vm);
  return JNI_VERSION_1_4;
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
