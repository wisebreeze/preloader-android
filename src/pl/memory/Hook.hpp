#pragma once

/**
 * @file memory/Hook.hpp
 * @brief C++ memory hook API (backported from 0.2.0+).
 *
 * Native mods built against the 0.2.0+ SDK call pl::memory::hook / unhook
 * with a pl::memory::HookPriority enum. The preloader at 95a40b1 only
 * exposed pl::hook::* (C-style, int priority) and the pl_hook / pl_unhook
 * C symbols, so mods importing pl::memory::hook failed to resolve.
 *
 * This header re-exports the same hook implementation under the
 * pl::memory namespace with the 0.2.0+ ABI.
 */

#include "pl/c/Macro.h"
#include "pl/c/Hook.h"

namespace pl::memory {

enum class HookPriority : int {
  Highest = 0,
  High = 100,
  Normal = 200,
  Low = 300,
  Lowest = 400,
};

PL_SHARED_EXPORT int hook(void *target, void *detour, void **originalFunc,
                          HookPriority priority = HookPriority::Normal);

PL_SHARED_EXPORT bool unhook(void *target, void *detour);

} // namespace pl::memory
