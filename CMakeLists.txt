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

# Vendored, audited crypto primitives (X25519, ChaCha20-Poly1305) for the authenticated mesh. Built
# in its own target so project warnings do not apply to third-party source. See docs/adr/0001.
add_subdirectory(third_party/monocypher)

# Portable, OS-agnostic core. Platform-specific code will live under src/platform/ and be
# added per-OS in a later commit; nothing here touches an OS API directly.
add_library(eosr_core STATIC
    src/common/ids.cpp
    src/common/byte_buffer.cpp
    src/common/log.cpp
    src/common/crypto.cpp
    src/core/frame_result.cpp
    src/core/callback_manager.cpp
    src/core/config.cpp
    src/core/identity.cpp
    src/core/settings.cpp
    src/core/client.cpp
    src/core/platform.cpp
    src/core/runtime.cpp
    src/interfaces/connect.cpp
    src/interfaces/auth.cpp
    src/interfaces/p2p.cpp
    src/interfaces/sessions.cpp
    src/interfaces/presence.cpp
    src/interfaces/lobby.cpp
    src/interfaces/ui.cpp
    src/interfaces/integratedplatform.cpp
    src/interfaces/friends.cpp
    src/interfaces/userinfo.cpp
    src/net/wire.cpp
    src/net/secure_channel.cpp
    src/net/peer_channel.cpp
    src/net/message_router.cpp
)

# We build the SDK, so expose Epic's headers and select the exporting macro path.
target_include_directories(eosr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include/eos
)
target_compile_definitions(eosr_core PUBLIC EOS_BUILDING_SDK=1)

# The crypto wrapper (src/common/crypto.cpp) is the only place the Monocypher API is used; link it
# privately so the vendor header does not leak to the core's own consumers.
target_link_libraries(eosr_core PRIVATE monocypher)

# All OS-specific code lives behind src/platform/; select the right implementation here.
if(WIN32)
    target_sources(eosr_core PRIVATE
        src/platform/socket_win32.cpp
        src/platform/net_iface_win32.cpp
        src/platform/rng_win32.cpp
        src/platform/paths_win32.cpp
    )
    target_compile_definitions(eosr_core PRIVATE _WIN32_WINNT=0x0600)
    # iphlpapi enumerates the adapters discovery broadcasts to; bcrypt is the system random source
    # the per-instance identity is minted from.
    target_link_libraries(eosr_core PUBLIC ws2_32 iphlpapi bcrypt)
else()
    target_sources(eosr_core PRIVATE
        src/platform/socket_linux.cpp
        src/platform/net_iface_linux.cpp
        src/platform/rng_linux.cpp
        src/platform/paths_linux.cpp
    )
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
    src/flat/eos_auth_flat.cpp
    src/flat/eos_p2p_flat.cpp
    src/flat/eos_sessions_flat.cpp
    src/flat/eos_presence_flat.cpp
    src/flat/eos_lobby_flat.cpp
    src/flat/eos_ui_flat.cpp
    src/flat/eos_integratedplatform_flat.cpp
    src/flat/eos_friends_flat.cpp
    src/flat/eos_userinfo_flat.cpp
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
    # -fvisibility=hidden does not hide the weak libstdc++ template instantiations the compiler
    # emits with vague linkage, so pin the exported surface to EOS_* with a linker version script.
    include(CheckCXXSourceCompiles)
    set(eosr_version_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/eos_exports.map")
    set(CMAKE_REQUIRED_FLAGS "-Wl,--version-script=${eosr_version_script}")
    check_cxx_source_compiles("int main(){return 0;}" EOSR_HAVE_VERSION_SCRIPT)
    unset(CMAKE_REQUIRED_FLAGS)
    if(EOSR_HAVE_VERSION_SCRIPT)
        target_link_options(eos_shared PRIVATE "-Wl,--version-script=${eosr_version_script}")
    endif()
endif()
eosr_apply_warnings(eos_shared)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
