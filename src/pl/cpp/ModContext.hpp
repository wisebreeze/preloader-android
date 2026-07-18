#pragma once

/**
 * @file ModContext.hpp
 * @brief C++ mod lifecycle registration API backported from preloader 0.2.0.
 *
 * Some native mods (e.g. LeviMap) are built against the 0.2.0+ SDK and only
 * export PLGetModRegistration, returning a pl::mod::ModRegistration* with
 * type-erased load/enable/disable/unload callbacks that receive a
 * ModContext&. The preloader pinned at 95a40b1 only knew about PLMod_Load /
 * LeviMod_Load, so those mods were dlopen-ed but their lifecycle was never
 * invoked and the mod silently did nothing.
 *
 * This header provides the minimal ModInfo / ModContext / ModRegistration
 * definitions needed to dispatch those callbacks, using the pl::log::Logger
 * that already exists in this preloader checkout.
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

} // namespace pl::mod

extern "C" {

/**
 * @brief Returns the C++ lifecycle registration for a loaded mod library.
 */
PL_SHARED_EXPORT ::pl::mod::ModRegistration *PLGetModRegistration();

} // extern "C"
