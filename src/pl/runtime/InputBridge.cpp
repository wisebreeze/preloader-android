#include "pl/runtime/InputBridge.h"

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "pl/Input.hpp"
#include "pl/runtime/JavaRuntime.h"

namespace pl::runtime {
namespace {

std::vector<PreloaderInput_OnTouch_Fn> g_touchCallbacks;
std::vector<PreloaderInput_OnKeyEvent_Fn> g_keyEventCallbacks;
std::vector<PreloaderInput_OnMouse_Fn> g_mouseCallbacks;
std::vector<pl::input::TouchCallback> g_cppTouchCallbacks;
std::vector<pl::input::KeyCallback> g_cppKeyCallbacks;
std::vector<pl::input::MouseCallback> g_cppMouseCallbacks;
std::mutex g_callbackMutex;

void RegisterTouchCallback(PreloaderInput_OnTouch_Fn callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_touchCallbacks.push_back(callback);
}

void RegisterKeyEventCallback(PreloaderInput_OnKeyEvent_Fn callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_keyEventCallbacks.push_back(callback);
}

void RegisterMouseCallback(PreloaderInput_OnMouse_Fn callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_mouseCallbacks.push_back(callback);
}

void ShowKeyboard() { CallActivityVoidMethod("showSoftKeyboard"); }

void HideKeyboard() { CallActivityVoidMethod("hideSoftKeyboard"); }

PreloaderInput_Interface g_inputInterface = {
    .RegisterTouchCallback = RegisterTouchCallback,
    .RegisterKeyEventCallback = RegisterKeyEventCallback,
    .ShowKeyboard = ShowKeyboard,
    .HideKeyboard = HideKeyboard,
    .RegisterMouseCallback = RegisterMouseCallback,
};

} // namespace

PreloaderInput_Interface *GetInputInterface() { return &g_inputInterface; }

bool DispatchTouch(int action, int pointerId, float x, float y) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  bool consumed = false;
  for (auto callback : g_touchCallbacks) {
    if (callback) {
      consumed |= callback(action, pointerId, x, y);
    }
  }
  const pl::input::TouchEvent event{
      .action = action, .pointerId = pointerId, .x = x, .y = y};
  for (const auto &callback : g_cppTouchCallbacks) {
    if (callback) {
      consumed |= callback(event);
    }
  }
  return consumed;
}

bool DispatchKeyEvent(int keyCode, unsigned int unicodeChar, bool isKeyDown) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  bool consumed = false;
  for (auto callback : g_keyEventCallbacks) {
    if (callback) {
      consumed |= callback(keyCode, unicodeChar, isKeyDown);
    }
  }
  const pl::input::KeyEvent event{
      .keyCode = keyCode, .unicodeChar = unicodeChar, .isKeyDown = isKeyDown};
  for (const auto &callback : g_cppKeyCallbacks) {
    if (callback) {
      consumed |= callback(event);
    }
  }
  return consumed;
}

bool DispatchMouse(int button, bool isDown) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  bool consumed = false;
  for (auto callback : g_mouseCallbacks) {
    if (callback) {
      consumed |= callback(button, isDown);
    }
  }
  const pl::input::MouseEvent event{.button = button, .isDown = isDown};
  for (const auto &callback : g_cppMouseCallbacks) {
    if (callback) {
      consumed |= callback(event);
    }
  }
  return consumed;
}

namespace {

void RegisterCppTouchCallback(pl::input::TouchCallback callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_cppTouchCallbacks.push_back(std::move(callback));
}

void RegisterCppKeyCallback(pl::input::KeyCallback callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_cppKeyCallbacks.push_back(std::move(callback));
}

void RegisterCppMouseCallback(pl::input::MouseCallback callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_cppMouseCallbacks.push_back(std::move(callback));
}

} // namespace

} // namespace pl::runtime

namespace pl::input {

void registerTouchCallback(TouchCallback callback) {
  pl::runtime::RegisterCppTouchCallback(std::move(callback));
}

void registerKeyCallback(KeyCallback callback) {
  pl::runtime::RegisterCppKeyCallback(std::move(callback));
}

void registerMouseCallback(MouseCallback callback) {
  pl::runtime::RegisterCppMouseCallback(std::move(callback));
}

void showKeyboard() { pl::runtime::ShowKeyboard(); }

void hideKeyboard() { pl::runtime::HideKeyboard(); }

} // namespace pl::input

