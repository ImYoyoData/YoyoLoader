#pragma once

#include <cstddef>
#include <string_view>

namespace yoyo {

enum class NativeAbi {
    Msvc,   /* Windows: 32-byte std::string */
    Libcxx, /* Linux:   24-byte std::string (LLVM 20 layout) */
};

constexpr NativeAbi native_abi() {
#ifdef _WIN32
    return NativeAbi::Msvc;
#else
    return NativeAbi::Libcxx;
#endif
}

constexpr size_t native_string_size(NativeAbi abi) {
    return abi == NativeAbi::Msvc ? 32 : 24;
}

/*
 * Read a native std::string at `p` without linking against the same STL.
 * The returned view points into the game's memory and is only valid while
 * that object is alive.
 */
std::string_view read_native_string(const void* p, NativeAbi abi);

/*
 * Rewrite a native std::string in place.  Returns false if the new value does
 * not fit the existing allocation; the caller must then keep the original.
 */
bool write_native_string(void* p, std::string_view value, NativeAbi abi);

} // namespace yoyo
