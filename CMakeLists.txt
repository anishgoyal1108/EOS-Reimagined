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
option(BUILD_MANAGER "Build the EOS Reimagined desktop manager" OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_warnings.cmake)

# Vendored, audited crypto primitives (X25519, ChaCha20-Poly1305) for the authenticated mesh. Built
# in its own target so project warnings do not apply to third-party source. See wiki/developers/internals/adr/0001.
add_subdirectory(third_party/monocypher)

# A precise, artifact-specific build id for the trace and runtime.json metadata: a base version plus
# the git revision (with a -dirty marker) when the tree is a working checkout, so two alpha traces can
# be tied to the exact build that produced them. Injected as a compile definition read by tracer.cpp;
# a build outside a git checkout still gets the version.
set(EOSR_VERSION "0.1.0-alpha" CACHE STRING "Release version embedded in runtime metadata")
set(EOSR_BUILD_REVISION "" CACHE STRING "Exact source revision embedded by release builds")
find_package(Git QUIET)
set(EOSR_GIT_REV "")
if(NOT EOSR_BUILD_REVISION STREQUAL "")
    set(EOSR_GIT_REV "${EOSR_BUILD_REVISION}")
elseif(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE EOSR_GIT_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(EOSR_GIT_REV STREQUAL "")
    set(EOSR_BUILD_ID "eosr ${EOSR_VERSION}")
else()
    set(EOSR_BUILD_ID "eosr ${EOSR_VERSION} (${EOSR_GIT_REV})")
endif()

# Portable, OS-agnostic core. Platform-specific code will live under src/platform/ and be
# added per-OS in a later commit; nothing here touches an OS API directly.
add_library(eosr_core STATIC
    src/common/ids.cpp
    src/common/byte_buffer.cpp
    src/common/log.cpp
    src/common/eos_names.cpp
    src/common/crypto.cpp
    src/core/frame_result.cpp
    src/core/callback_manager.cpp
    src/common/json_writer.cpp
    src/core/trace_event.cpp
    src/core/trace_sink.cpp
    src/core/peer_fp.cpp
    src/core/label_registry.cpp
    src/core/tracer.cpp
    src/core/config.cpp
    src/core/config_file.cpp
    src/core/bootstrap_descriptor.cpp
    src/core/system_config_source.cpp
    src/core/identity.cpp
    src/core/settings.cpp
    src/core/client.cpp
    src/core/platform.cpp
    src/core/runtime.cpp
    src/core/stub_completion.cpp
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
target_compile_definitions(eosr_core PRIVATE "EOSR_BUILD_ID=\"${EOSR_BUILD_ID}\"")

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
    target_link_libraries(eosr_core PUBLIC ${CMAKE_DL_LIBS})
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
    src/flat/eos_stubs_flat.cpp
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

if(BUILD_MANAGER)
    # The manager vendors one pinned static GUI toolkit. Disable everything outside the desktop
    # shell's needs before adding FLTK so no tools, shared GUI runtime, GL, or docs reach a package.
    set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FORMS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_HTML_DOCS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_PDF_DOCS OFF CACHE BOOL "" FORCE)
    set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
    set(FLTK_OPTION_PRINT_SUPPORT OFF CACHE BOOL "" FORCE)
    set(FLTK_OPTION_SVG OFF CACHE BOOL "" FORCE)
    set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_DBUS OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_SYSTEM_LIBJPEG OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_SYSTEM_LIBPNG OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
    set(FLTK_MSVC_RUNTIME_DLL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/fltk EXCLUDE_FROM_ALL)

    add_library(eosr_manager_core STATIC
        src/manager/manager_info.cpp
        src/manager/steam_launch.cpp
        src/manager/vdf.cpp
        src/manager/steam_discovery.cpp
        src/manager/sha256.cpp
        src/manager/target_inspection.cpp
        src/manager/release_catalog.cpp
        src/manager/json.cpp
        src/manager/manager_state.cpp
        src/manager/state_store.cpp
        src/manager/install_transaction.cpp
        src/manager/configuration_model.cpp
        src/manager/instance_service.cpp
        src/manager/operation_log.cpp
        src/manager/run_service.cpp
        src/manager/ui_policy.cpp
    )
    target_include_directories(eosr_manager_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(eosr_manager_core PRIVATE
        "EOSR_MANAGER_BUILD_ID=\"${EOSR_BUILD_ID}\"")
    eosr_apply_warnings(eosr_manager_core)

    add_library(eosr_manager_platform STATIC)
    if(WIN32)
        target_sources(eosr_manager_platform PRIVATE
            src/platform/proton_paths_win32.cpp
            src/platform/manager_discovery_win32.cpp
            src/platform/manager_transactions_win32.cpp
            src/platform/manager_runs_win32.cpp
            src/platform/manager_application_win32.cpp)
        target_link_libraries(eosr_manager_platform PUBLIC advapi32 bcrypt shell32 ole32)
    else()
        target_sources(eosr_manager_platform PRIVATE
            src/platform/proton_paths_linux.cpp
            src/platform/manager_discovery_linux.cpp
            src/platform/manager_transactions_linux.cpp
            src/platform/manager_runs_linux.cpp
            src/platform/manager_application_linux.cpp)
    endif()
    target_include_directories(eosr_manager_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    eosr_apply_warnings(eosr_manager_platform)

    if(WIN32)
        add_executable(eosr_manager WIN32
            src/manager/ui/main.cpp
            src/manager/ui/application.cpp)
        set_target_properties(eosr_manager PROPERTIES OUTPUT_NAME "EOSReimaginedManager")
    else()
        add_executable(eosr_manager
            src/manager/ui/main.cpp
            src/manager/ui/application.cpp)
        set_target_properties(eosr_manager PROPERTIES OUTPUT_NAME "eos-reimagined-manager")
    endif()
    target_link_libraries(eosr_manager PRIVATE
        eosr_manager_core
        eosr_manager_platform
        fltk::fltk
    )
    set_target_properties(eosr_manager PROPERTIES SKIP_BUILD_RPATH ON INSTALL_RPATH "")
    eosr_apply_warnings(eosr_manager)

    add_executable(eosr_manager_cli src/manager/cli/main.cpp)
    if(WIN32)
        set_target_properties(eosr_manager_cli PROPERTIES OUTPUT_NAME "EOSReimaginedManagerCLI")
    else()
        set_target_properties(eosr_manager_cli PROPERTIES OUTPUT_NAME "eosr-manager-cli")
    endif()
    target_link_libraries(eosr_manager_cli PRIVATE eosr_manager_core eosr_manager_platform)
    eosr_apply_warnings(eosr_manager_cli)

    if(MSVC)
        set_property(TARGET eosr_manager_core eosr_manager_platform eosr_manager eosr_manager_cli
                     PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_link_options(eosr_manager PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(eosr_manager_cli PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # A GUI dependency in either SDK target is an architectural failure, not a packaging detail.
    get_target_property(eosr_core_links eosr_core LINK_LIBRARIES)
    get_target_property(eos_shared_links eos_shared LINK_LIBRARIES)
    if("${eosr_core_links};${eos_shared_links}" MATCHES "(^|;)fltk(::|;|$)")
        message(FATAL_ERROR "FLTK must not link into the replacement SDK")
    endif()
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
