#pragma once

/**
 * @file ModContext.hpp
 * @brief C++ mod lifecycle registration API (backported from 0.2.1).
 *
 * Provides the pl::mod::NativeMod / ModContext / ModRegistration /
 * ScopedCurrentMod definitions that native mods built against the 0.2.0+
 * SDK expect. The preloader at 95a40b1 only shipped the legacy
 * pl::cpp::mod::NativeMod (shared_ptr-based, different ABI), so mods
 * importing pl::mod::NativeMod::current() failed to resolve symbols.
 *
 * ABI is kept byte-compatible with upstream 0.2.1 (f45fc42) so that mods
 * compiled against the 0.2.0+ SDK can dlopen and dispatch lifecycle
 * callbacks without recompilation.
 */

#include <filesystem>
#include <jni.h>
#include <string>
#include <utility>

#include "pl/Logger.h"
#include "pl/c/Macro.h"

namespace pl::mod {

inline constexpr const char *ModRegistrationSymbol =
    "PLGetModRegistration";

/**
 * @brief Manifest and filesystem metadata resolved by the preloader.
 */
struct ModInfo {
  std::string id;
  std::string displayName;
  std::string author;
  std::string version;
  std::filesystem::path entryPath;
  std::string entryFileName;
  std::filesystem::path libraryPath;
  std::filesystem::path iconPath;
  std::filesystem::path manifestPath;
  std::filesystem::path modRootPath;
};

/**
 * @brief Runtime context passed to each C++ mod lifecycle phase.
 */
class ModContext {
public:
  ModContext(JavaVM *javaVm, ModInfo info)
      : mJavaVm(javaVm), mInfo(std::move(info)),
        mLogger(&pl::log::Logger::getOrCreate(
            mInfo.displayName.empty() ? fallbackName(mInfo.id)
                                      : mInfo.displayName)) {}

  [[nodiscard]] JavaVM *javaVm() const noexcept { return mJavaVm; }
  [[nodiscard]] const ModInfo &info() const noexcept { return mInfo; }
  [[nodiscard]] pl::log::Logger &logger() const noexcept { return *mLogger; }

  [[nodiscard]] const std::filesystem::path &modRootPath() const noexcept {
    return mInfo.modRootPath;
  }
  [[nodiscard]] std::filesystem::path dataDir() const {
    return mInfo.modRootPath / "data";
  }
  [[nodiscard]] std::filesystem::path configDir() const {
    return mInfo.modRootPath / "config";
  }
  [[nodiscard]] std::filesystem::path resourceDir() const {
    return mInfo.modRootPath / "resources";
  }

private:
  JavaVM *mJavaVm{};
  ModInfo mInfo;
  pl::log::Logger *mLogger{};

  static std::string fallbackName(const std::string &id) {
    return id.empty() ? "LeviMod" : id;
  }
};

/**
 * @brief Runtime mod object for the currently executing native mod.
 *
 * Member layout MUST match upstream 0.2.1 (include/pl/Mod.hpp) so that
 * mods compiled against the 0.2.0+ SDK can access getId()/getLogger()/
 * getDataDir() etc. through the pointer returned by current().
 */
class NativeMod {
public:
  enum class State {
    Unloaded,
    Loaded,
    Enabled,
  };

  NativeMod(JavaVM *javaVm, ModInfo info)
      : mJavaVm(javaVm), mInfo(std::move(info)),
        mDataDir(mInfo.modRootPath / "data"),
        mConfigDir(mInfo.modRootPath / "config"),
        mResourceDir(mInfo.modRootPath / "resources"),
        mLogger(&pl::log::Logger::getOrCreate(mInfo.displayName.empty()
                                                  ? fallbackName(mInfo.id)
                                                  : mInfo.displayName)) {}

  [[nodiscard]] State getState() const noexcept { return mState; }
  [[nodiscard]] bool isEnabled() const noexcept {
    return mState == State::Enabled;
  }
  [[nodiscard]] bool isLoaded() const noexcept {
    return mState == State::Loaded;
  }
  [[nodiscard]] bool isUnloaded() const noexcept {
    return mState == State::Unloaded;
  }
  [[nodiscard]] bool isDisabled() const noexcept {
    return mState != State::Enabled;
  }

  [[nodiscard]] const std::string &getId() const noexcept { return mInfo.id; }
  [[nodiscard]] const std::string &getName() const noexcept {
    return mInfo.displayName;
  }
  [[nodiscard]] const std::string &getAuthor() const noexcept {
    return mInfo.author;
  }
  [[nodiscard]] const std::string &getVersion() const noexcept {
    return mInfo.version;
  }
  [[nodiscard]] const std::filesystem::path &getEntryPath() const noexcept {
    return mInfo.entryPath;
  }
  [[nodiscard]] const std::string &getEntryFileName() const noexcept {
    return mInfo.entryFileName;
  }
  [[nodiscard]] const std::filesystem::path &getIconPath() const noexcept {
    return mInfo.iconPath;
  }
  [[nodiscard]] const std::filesystem::path &getModDir() const noexcept {
    return mInfo.modRootPath;
  }
  [[nodiscard]] const std::filesystem::path &getDataDir() const noexcept {
    return mDataDir;
  }
  [[nodiscard]] const std::filesystem::path &getConfigDir() const noexcept {
    return mConfigDir;
  }
  [[nodiscard]] const std::filesystem::path &getResourceDir() const noexcept {
    return mResourceDir;
  }
  [[nodiscard]] const std::filesystem::path &getManifestPath() const noexcept {
    return mInfo.manifestPath;
  }
  [[nodiscard]] const std::filesystem::path &getLibraryPath() const noexcept {
    return mInfo.libraryPath;
  }
  [[nodiscard]] JavaVM *getJavaVM() const noexcept { return mJavaVm; }
  [[nodiscard]] pl::log::Logger &getLogger() const noexcept { return *mLogger; }

  /**
   * @brief Returns the mod currently being registered or executing lifecycle.
   */
  [[nodiscard]] PL_SHARED_EXPORT static NativeMod *current() noexcept;

  void setState(State state) noexcept { mState = state; }

private:
  JavaVM *mJavaVm{};
  ModInfo mInfo;
  std::filesystem::path mDataDir;
  std::filesystem::path mConfigDir;
  std::filesystem::path mResourceDir;
  pl::log::Logger *mLogger{};
  State mState{State::Unloaded};

  [[nodiscard]] static std::string fallbackName(const std::string &id) {
    return id.empty() ? "LeviMod" : id;
  }
};

using LifecycleFunction = bool (*)(void *instance, ModContext &context);

/**
 * @brief Type-erased lifecycle dispatch table exported by a C++ mod.
 */
struct ModRegistration {
  void *instance{};
  LifecycleFunction load{};
  LifecycleFunction enable{};
  LifecycleFunction disable{};
  LifecycleFunction unload{};
};

namespace detail {

/**
 * @brief RAII guard that sets the "current" mod for the duration of a
 *        lifecycle callback, restoring the previous value on destruction.
 */
class ScopedCurrentMod {
public:
  explicit ScopedCurrentMod(NativeMod *current) noexcept;
  ScopedCurrentMod(const ScopedCurrentMod &) = delete;
  ScopedCurrentMod &operator=(const ScopedCurrentMod &) = delete;
  ~ScopedCurrentMod();

private:
  NativeMod *mPrevious{};
};

} // namespace detail

} // namespace pl::mod

extern "C" {

/**
 * @brief Returns the C++ lifecycle registration for a loaded mod library.
 */
PL_SHARED_EXPORT ::pl::mod::ModRegistration *PLGetModRegistration();

} // extern "C"
