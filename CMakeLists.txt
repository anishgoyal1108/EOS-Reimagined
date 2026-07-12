cmake_minimum_required(VERSION 3.16)
project(eos_reimagined CXX)

# C++11, strictly. No compiler extensions.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_TESTING "Build the test suite" ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_warnings.cmake)

# Portable, OS-agnostic core. Platform-specific code will live under src/platform/ and be
# added per-OS in a later commit; nothing here touches an OS API directly.
add_library(eosr_core STATIC
    src/common/ids.cpp
    src/common/byte_buffer.cpp
    src/common/log.cpp
    src/core/frame_result.cpp
    src/core/callback_manager.cpp
    src/core/settings.cpp
    src/core/client.cpp
    src/core/platform.cpp
    src/core/runtime.cpp
    src/interfaces/connect.cpp
    src/net/wire.cpp
    src/net/message_router.cpp
)

# We build the SDK, so expose Epic's headers and select the exporting macro path.
target_include_directories(eosr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include/eos
)
target_compile_definitions(eosr_core PUBLIC EOS_BUILDING_SDK=1)

# All OS-specific code lives behind src/platform/; select the right implementation here.
if(WIN32)
    target_sources(eosr_core PRIVATE src/platform/socket_win32.cpp)
    target_compile_definitions(eosr_core PRIVATE _WIN32_WINNT=0x0600)
    target_link_libraries(eosr_core PUBLIC ws2_32)
else()
    target_sources(eosr_core PRIVATE src/platform/socket_linux.cpp)
endif()

eosr_apply_warnings(eosr_core)

# The core is linked into the shared library below, so it must be position-independent, and its
# symbols stay hidden — only the flat EOS_* entry points are exported.
set_target_properties(eosr_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# The drop-in replacement: the flat C ABI over the portable core. The output is named exactly
# like the library it replaces so a game loads it unchanged. Visibility is hidden, so the only
# exported symbols are the EOS_* functions the vendored headers mark for export.
add_library(eos_shared SHARED
    src/flat/eos_init_flat.cpp
    src/flat/eos_platform_flat.cpp
    src/flat/eos_logging_flat.cpp
    src/flat/eos_common_flat.cpp
    src/flat/eos_connect_flat.cpp
)
target_link_libraries(eos_shared PRIVATE eosr_core)
set_target_properties(eos_shared PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
if(WIN32)
    set_target_properties(eos_shared PROPERTIES
        OUTPUT_NAME "EOSSDK-Win64-Shipping" PREFIX "" SUFFIX ".dll")
else()
    set_target_properties(eos_shared PROPERTIES
        OUTPUT_NAME "EOSSDK-Linux-Shipping" PREFIX "lib" SUFFIX ".so")
endif()
eosr_apply_warnings(eos_shared)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
