#!/usr/bin/env bash
# Build and test both target binaries: the Linux-native one and the Windows one
# (cross-compiled with MinGW-w64 and run under Wine).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

echo "== Linux (native) =="
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure

echo
echo "== Windows (MinGW-w64 + Wine) =="
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && command -v wine >/dev/null 2>&1; then
    # The toolchain file is only read on the first configure of a fresh build dir.
    if [ ! -f build-win/CMakeCache.txt ]; then
        cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake >/dev/null
    fi
    cmake --build build-win --parallel
    # Keep Wine's prefix inside the (ignored) build dir and stay quiet.
    export WINEPREFIX="$root/build-win/.wineprefix"
    export WINEDEBUG=-all
    ctest --test-dir build-win --output-on-failure
else
    echo "  skipped: MinGW-w64 or Wine not installed (CI covers the Windows build)"
fi

echo
echo "All target tests passed."
