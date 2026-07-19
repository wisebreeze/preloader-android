#pragma once

/**
 * @file Input.hpp
 * @brief C++ input callback API for native mods (backported from 0.2.0+).
 *
 * Native mods built against the 0.2.0+ SDK call into the pl::input namespace
 * to register std::function-based touch/key/mouse callbacks and to drive the
 * soft keyboard. The preloader at 95a40b1 only exposed the C-style
 * pl::runtime::* API, so mods importing pl::input::* failed to resolve
 * symbols at dlopen time.
 */

#include <functional>

#include "pl/c/Macro.h"

namespace pl::input {

struct TouchEvent {
  int action{};
  int pointerId{};
  float x{};
  float y{};
};

struct KeyEvent {
  int keyCode{};
  unsigned int unicodeChar{};
  bool isKeyDown{};
};

struct MouseEvent {
  int button{};
  bool isDown{};
};

using TouchCallback = std::function<bool(const TouchEvent &)>;
using KeyCallback = std::function<bool(const KeyEvent &)>;
using MouseCallback = std::function<bool(const MouseEvent &)>;

PL_SHARED_EXPORT void registerTouchCallback(TouchCallback callback);
PL_SHARED_EXPORT void registerKeyCallback(KeyCallback callback);
PL_SHARED_EXPORT void registerMouseCallback(MouseCallback callback);

PL_SHARED_EXPORT void showKeyboard();
PL_SHARED_EXPORT void hideKeyboard();

} // namespace pl::input
