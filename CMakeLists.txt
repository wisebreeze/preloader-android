cmake_minimum_required(VERSION 3.22)
project(preloader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PRELOADER_SOURCES
        src/pl/PreLoader.cpp
        src/pl/Mod.cpp
        src/pl/internal/ModManager.cpp
        src/pl/memory/Hook.cpp
        src/pl/memory/Patch.cpp
        src/pl/memory/Signature.cpp
        src/pl/runtime/InputBridge.cpp
        src/pl/runtime/JavaRuntime.cpp
)

add_library(preloader SHARED ${PRELOADER_SOURCES})

target_include_directories(preloader PUBLIC src)
target_compile_definitions(preloader PRIVATE PRELOADER_EXPORT UNICODE)
target_compile_options(preloader PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>)

include(FetchContent)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_Declare(
        boost_pfr
        GIT_REPOSITORY https://github.com/boostorg/pfr.git
        GIT_TAG boost-1.88.0
)
FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.9.7
)
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.0.2
)
set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
FetchContent_MakeAvailable(nlohmann_json boost_pfr magic_enum fmt)

# fmt is statically linked into libpreloader.so, but native mods built
# against the 0.2.0+ SDK import fmt::v11::vformat and expect it to be
# resolved from libpreloader.so (loaded with RTLD_GLOBAL). The preloader
# compiles with -fvisibility=hidden, so we must force fmt's own symbols
# to default visibility so they appear in the dynamic symbol table.
if(TARGET fmt)
  set_target_properties(fmt PROPERTIES
          CXX_VISIBILITY_PRESET default
          VISIBILITY_INLINES_HIDDEN OFF)
  target_compile_definitions(fmt PRIVATE FMT_LIB_EXPORT)
endif()

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)

if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(GLOSSHOOK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/ARM/libGlossHook.a")
elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(GLOSSHOOK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/ARM64/libGlossHook.a")
else()
    message(FATAL_ERROR "Unsupported ABI: ${CMAKE_ANDROID_ARCH_ABI}")
endif()

target_link_libraries(preloader
        PRIVATE
        ${GLOSSHOOK_PATH}
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${gles-lib}
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        fmt::fmt
)

install(TARGETS preloader
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(FILES
        src/pl/Gloss.h
        src/pl/Logger.h
        src/pl/Input.hpp
        DESTINATION include/pl)

install(FILES
        src/pl/c/Hook.h
        src/pl/c/Macro.h
        src/pl/c/Mod.h
        src/pl/c/Patch.h
        src/pl/c/PreloaderInput.h
        src/pl/c/Signature.h
        src/pl/c/Types.h
        DESTINATION include/pl/c)

install(FILES
        src/pl/cpp/Config.hpp
        src/pl/cpp/Hook.hpp
        src/pl/cpp/Mod.hpp
        src/pl/cpp/ModContext.hpp
        src/pl/cpp/Patch.hpp
        src/pl/cpp/PreloaderInput.hpp
        src/pl/cpp/Signature.hpp
        src/pl/cpp/Types.hpp
        DESTINATION include/pl/cpp)

install(FILES
        src/pl/memory/Hook.hpp
        DESTINATION include/pl/memory)

install(FILES
        src/pl/cpp/mod/Mod.hpp
        src/pl/cpp/mod/NativeMod.hpp
        src/pl/cpp/mod/RegisterHelper.hpp
        DESTINATION include/pl/cpp/mod)

install(FILES
        src/pl/api/Macro.h
        src/pl/api/Encoding.h
        src/pl/api/Types.h
        DESTINATION include/pl/api)

install(FILES
        src/pl/api/memory/Hook.h
        src/pl/api/memory/Memory.h
        DESTINATION include/pl/api/memory)
