#include <fmt/format.h>

#include <string>

/**
 * @file FmtExport.cpp
 * @brief Retain fmt::v11::vformat in libpreloader.so.
 *
 * The preloader itself uses std::format (C++20) and never calls into the
 * {fmt} library directly. Without an explicit reference the linker's
 * --gc-sections pass would strip fmt::v11::vformat from the final .so,
 * leaving native mods that import it with an unresolved symbol at
 * dlopen time.
 *
 * This translation unit forces the symbol to be retained by taking its
 * address through a volatile pointer. fmt is compiled with
 * -fvisibility=default and FMT_LIB_EXPORT (see CMakeLists.txt), so the
 * retained symbol is exported into the dynamic symbol table.
 */

namespace {

// volatile defeats the compiler's dead-code elimination: it cannot prove
// the pointer is never dereferenced, so it must emit the reference.
[[maybe_unused]] volatile auto g_fmtVformatAddr =
    reinterpret_cast<void *>(&fmt::v11::vformat);

} // namespace
