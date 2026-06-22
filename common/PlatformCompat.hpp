#pragma once

// Cross-platform compatibility shim for the Linux development build.
//
// Much of StayPutVR was written against <windows.h>, which defines the
// `min`/`max` function-style macros. A lot of call sites therefore use bare
// `min(a, b)` / `max(a, b)`. On Linux there is no windows.h, so we bring the
// std versions into the global namespace (NOT as macros, to avoid breaking
// the codebase's deliberate `(std::min)` / `(std::max)` call sites).
//
// This header is force-included (-include) for the common and application
// targets on non-Windows builds. It is a no-op on Windows and in C sources.

#if !defined(_WIN32) && defined(__cplusplus)

#include <algorithm>
#include <cstddef>

using std::min;
using std::max;

// MSVC's bounds-checked string copy, used throughout the UI panels as
// strcpy_s(dest, sizeof(dest), src). Provide a portable equivalent.
inline int strcpy_s(char* dest, std::size_t destsz, const char* src) {
    if (!dest || destsz == 0) return 22; // EINVAL
    std::size_t i = 0;
    for (; src && src[i] != '\0' && i + 1 < destsz; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return 0;
}

#endif
