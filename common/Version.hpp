#pragma once

// Single source of truth for the user-facing application version string.
// Keep in sync with project(StayPutVR VERSION ...) in the root CMakeLists.txt
// and the installer's OutFile/DisplayVersion.
#define STAYPUTVR_VERSION "1.5"

// Short git hash of the build, generated into the build dir by
// cmake/GitHash.cmake and shown in parentheses next to the version (splash +
// Settings > About). Falls back to "dev" when the header isn't present (e.g.
// editor indexing or a non-CMake build).
#if __has_include("git_hash.h")
#include "git_hash.h"
#endif
#ifndef STAYPUTVR_GIT_HASH
#define STAYPUTVR_GIT_HASH "dev"
#endif
