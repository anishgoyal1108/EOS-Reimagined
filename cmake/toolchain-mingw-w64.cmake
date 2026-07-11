# Cross-toolchain: build the Windows .dll / .exe from a Linux host with MinGW-w64.
# Configure with: cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Resolve libraries/headers only inside the MinGW sysroot, but find programs on the host.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# The shipped DLL must not depend on the MinGW runtime: it drops in next to a game with no
# extra DLLs alongside it. Linking fully static bundles libgcc, libstdc++, and libwinpthread
# (pulled in by std::thread/std::mutex) into the DLL. Only the C ABI crosses the boundary, so
# a self-contained runtime is safe. Test executables link static for the same reason.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# Let ctest run the cross-built test binaries by launching them through Wine.
find_program(WINE_EXECUTABLE wine)
if(WINE_EXECUTABLE)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE_EXECUTABLE})
endif()
