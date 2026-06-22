#!/usr/bin/env bash
# --- StayPutVR Linux development build script ---
#
# Builds the overlay GUI application (no SteamVR driver) for local development
# and OSC simulation. The GUI runs; PiShock/Twitch/audio are stubbed on Linux.
#
# Usage:
#   ./build_linux.sh [Debug|Release] [run]
#     Debug|Release  build type (default: Release)
#     run            launch the app after a successful build
#
# Requirements: cmake, a C++20 compiler, and the GLFW runtime (libglfw.so.3)
# plus an OpenGL/Mesa runtime. On Fedora/Bazzite:  sudo dnf install glfw mesa-libGL
# (only the runtime libraries are needed; headers are fetched automatically).

set -euo pipefail

# Resolve and enter the repo root (directory containing this script).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-linux"
BUILD_TYPE="Release"
DO_RUN=0

DO_CLEAN=0
for arg in "$@"; do
    case "$arg" in
        Debug|Release) BUILD_TYPE="$arg" ;;
        run|--run)     DO_RUN=1 ;;
        clean|--clean) DO_CLEAN=1 ;;
        *) echo "Unknown argument: $arg (expected Debug|Release|run|clean)" >&2; exit 1 ;;
    esac
done

JOBS="$(nproc 2>/dev/null || echo 4)"

# A clean build was requested explicitly...
if [[ "$DO_CLEAN" -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR (requested)"
    rm -rf "$BUILD_DIR"
fi

# ...or the build type changed since the last configure. Switching build type in
# place can leave objects compiled with mismatched flags, so start fresh.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    CACHED_TYPE="$(grep -E '^CMAKE_BUILD_TYPE:' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2)"
    if [[ -n "$CACHED_TYPE" && "$CACHED_TYPE" != "$BUILD_TYPE" ]]; then
        echo "Build type changed ($CACHED_TYPE -> $BUILD_TYPE); cleaning $BUILD_DIR"
        rm -rf "$BUILD_DIR"
    fi
fi

echo "=== StayPutVR Linux build ==="
echo "Build type : $BUILD_TYPE"
echo "Build dir  : $BUILD_DIR"
echo "Jobs       : $JOBS"
echo

# --- Configure ---
if ! cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -Wno-dev; then
    echo
    echo "CMAKE CONFIGURE FAILED" >&2
    exit 1
fi

# --- Build ---
if ! cmake --build "$BUILD_DIR" -j "$JOBS"; then
    echo
    echo "BUILD FAILED" >&2
    exit 1
fi

APP="$BUILD_DIR/application/stayputvr_app"
echo
echo "BUILD SUCCEEDED ($BUILD_TYPE)"
echo "Output: $SCRIPT_DIR/$APP"

if [[ "$DO_RUN" -eq 1 ]]; then
    echo
    echo "=== Launching $APP ==="
    # Help the dynamic loader find the GLFW runtime in distrobox/toolbox setups
    # where it lives under the host mount. Harmless on a native host.
    export LD_LIBRARY_PATH="/usr/lib64:/run/host/usr/lib64:${LD_LIBRARY_PATH:-}"
    exec "./$APP"
fi

echo "Run it with:  ./build_linux.sh $BUILD_TYPE run     (or)     ./$APP"
