cmake_minimum_required(VERSION 3.22)
project(preloader LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR
            "preloader is an Android runtime target. "
            "Use it only from an Android toolchain build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PRELOADER_SOURCES
        src/pl/Mod.cpp
        src/pl/PreLoader.cpp
        src/pl/internal/LoadedModRegistry.cpp
        src/pl/internal/ModManifest.cpp
        src/pl/internal/ModManager.cpp
        src/pl/internal/NativeModLifecycle.cpp
        src/pl/legacy/LegacyHook.cpp
        src/pl/legacy/LegacyInput.cpp
        src/pl/legacy/LegacyPatch.cpp
        src/pl/legacy/LegacySignature.cpp
        src/pl/memory/Hook.cpp
        src/pl/memory/Patch.cpp
        src/pl/memory/Signature.cpp
        src/pl/memory/Vtable.cpp
        src/pl/runtime/GameHookRules.cpp
        src/pl/runtime/GameHooks.cpp
        src/pl/runtime/ExternalModBridgeJni.cpp
        src/pl/runtime/InputBridge.cpp
        src/pl/runtime/InputJni.cpp
        src/pl/runtime/JavaRuntime.cpp
        src/pl/runtime/ModMenuBridge.cpp
)

add_library(preloader SHARED ${PRELOADER_SOURCES})

target_include_directories(preloader
        PUBLIC
        include
        PRIVATE
        src)
target_compile_definitions(preloader PRIVATE PRELOADER_EXPORT UNICODE)
target_compile_options(preloader PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>)

# Third-party deps are vendored under third_party/ so CMake configure
# never needs network access. Pinned versions:
#   nlohmann_json v3.11.3  (header-only)
#   boost_pfr     boost-1.88.0 (header-only)
#   magic_enum    v0.9.7   (header-only)
#   fmt           11.2.0
#   lunasvg       v3.5.0
set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "Disable lunasvg examples" FORCE)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON CACHE BOOL "Disable lunasvg system font scan" FORCE)

# fmt / lunasvg build static libs; force BUILD_SHARED_LIBS=OFF around
# their add_subdirectory so they don't accidentally pick up the parent
# shared-lib setting.
set(_preloader_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/nlohmann_json EXCLUDE_FROM_ALL)
add_subdirectory(third_party/pfr EXCLUDE_FROM_ALL)
add_subdirectory(third_party/magic_enum EXCLUDE_FROM_ALL)
add_subdirectory(third_party/fmt EXCLUDE_FROM_ALL)
add_subdirectory(third_party/lunasvg EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_preloader_build_shared_libs})

find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)
find_library(egl-lib EGL REQUIRED)
find_library(gles-lib GLESv3 REQUIRED)

if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(GLOSSHOOK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/ARM/libGlossHook.a")
elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(GLOSSHOOK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/ARM64/libGlossHook.a")
else()
    message(FATAL_ERROR
            "Unsupported Android ABI for preloader: ${CMAKE_ANDROID_ARCH_ABI}")
endif()

if(NOT EXISTS "${GLOSSHOOK_PATH}")
    message(FATAL_ERROR
            "GlossHook static library not found for ${CMAKE_ANDROID_ARCH_ABI}: "
            "${GLOSSHOOK_PATH}")
endif()

target_link_libraries(preloader
        PUBLIC
        fmt::fmt
        PRIVATE
        ${GLOSSHOOK_PATH}
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${gles-lib}
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        lunasvg::lunasvg
)

install(TARGETS preloader
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(DIRECTORY include/pl
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp")
