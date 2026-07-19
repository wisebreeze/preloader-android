#include "pl/cpp/ModContext.hpp"

/**
 * @file Mod.cpp
 * @brief Out-of-line definitions for pl::mod::NativeMod::current() and
 *        ScopedCurrentMod, backported from upstream 0.2.1 (f45fc42).
 *
 * These must live in a .cpp file (not inline in the header) so that the
 * symbols are exported from libpreloader.so with stable linkage. Mods
 * built against the 0.2.0+ SDK import _ZN2pl3mod9NativeMod7currentEv
 * and expect it to return the NativeMod* installed by ScopedCurrentMod
 * during lifecycle dispatch.
 */

namespace {
thread_local pl::mod::NativeMod *gCurrentMod{};
}

namespace pl::mod {

NativeMod *NativeMod::current() noexcept { return gCurrentMod; }

namespace detail {

ScopedCurrentMod::ScopedCurrentMod(NativeMod *current) noexcept
    : mPrevious(gCurrentMod) {
  gCurrentMod = current;
}

ScopedCurrentMod::~ScopedCurrentMod() { gCurrentMod = mPrevious; }

} // namespace detail

} // namespace pl::mod
