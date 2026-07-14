# doctest is a dev-only, single-header dependency. It is compiled into the test binaries
# and never linked into the shipped library.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(unit_tests
    unit/main.cpp
    unit/test_frame_result.cpp
    unit/test_callback_manager.cpp
    unit/test_ids.cpp
    unit/test_handle_store.cpp
    unit/test_json_writer.cpp
    unit/test_trace_event.cpp
    unit/test_trace_sink.cpp
    unit/test_tracer.cpp
    unit/test_peer_fp.cpp
    unit/test_config.cpp
    unit/test_config_file.cpp
    unit/test_system_config_source.cpp
    unit/test_crypto.cpp
    unit/test_secure_channel.cpp
    unit/test_identity.cpp
    unit/test_peer_channel.cpp
    unit/test_wire.cpp
    unit/test_socket.cpp
    unit/test_message_router.cpp
    unit/test_log.cpp
    unit/test_settings.cpp
    unit/test_client.cpp
    unit/test_platform.cpp
    unit/test_connect.cpp
    unit/test_auth.cpp
    unit/test_p2p.cpp
    unit/test_discovery.cpp
    unit/test_sessions.cpp
    unit/test_presence.cpp
    unit/test_lobby.cpp
    unit/test_ui.cpp
    unit/test_integratedplatform.cpp
    unit/test_friends.cpp
    unit/test_userinfo.cpp
)
target_link_libraries(unit_tests PRIVATE eosr_core doctest)

# The identity tests write real profile keys. Pin them to an absolute path under the build tree so
# they land there no matter which directory the binary is run from -- a relative path put generated
# keys wherever the caller happened to be standing, and they were once committed from the repo root.
target_compile_definitions(unit_tests PRIVATE
    EOSR_TEST_PROFILE_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-profiles")

# The authenticated-mesh ADR requires secret HKDF intermediates to be wiped.  GNU-compatible
# linkers can interpose the vendored wipe primitive so the unit test observes that contract without
# reading freed storage.  Both supported build targets (Linux and MinGW) use this linker facility.
if((UNIX AND NOT APPLE) OR MINGW)
    target_link_options(unit_tests PRIVATE "-Wl,--wrap=crypto_wipe")
    target_compile_definitions(unit_tests PRIVATE EOSR_TEST_WRAP_CRYPTO_WIPE=1)
endif()
if(UNIX AND NOT APPLE)
    target_link_options(unit_tests PRIVATE
        "-Wl,--wrap=_ZN4eosr8platform12random_bytesEPhm")
    target_compile_definitions(unit_tests PRIVATE EOSR_TEST_WRAP_RANDOM_BYTES=1)
elseif(MINGW)
    target_link_options(unit_tests PRIVATE
        "-Wl,--wrap=_ZN4eosr8platform12random_bytesEPhy")
    target_compile_definitions(unit_tests PRIVATE EOSR_TEST_WRAP_RANDOM_BYTES=1)
endif()

add_test(NAME unit_tests COMMAND unit_tests)

# Integration tests load the real built library through the C ABI, exactly like a game would,
# so they link neither eosr_core nor eos_shared and resolve every entry point at runtime. Only
# the OS-specific loader shim (kept under src/platform/) is compiled in.
add_executable(integration_tests
    integration/main.cpp
    integration/test_bootstrap.cpp
)
if(WIN32)
    target_sources(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/dynlib_win32.cpp)
else()
    target_sources(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/dynlib_linux.cpp)
    target_link_libraries(integration_tests PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include/eos
)
target_link_libraries(integration_tests PRIVATE doctest)
add_dependencies(integration_tests eos_shared)

# Co-locate the test binary with the library so the loader finds it by name under Wine too.
set_target_properties(integration_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(NAME integration_tests COMMAND integration_tests $<TARGET_FILE:eos_shared>)

# A dedicated probe for the real tracing path: it enables tracing through the loaded library, drives
# one whole EOS lifetime, and verifies the run it produced. Kept separate from the bootstrap suite
# because it sets EOSR_* in its own process environment and asserts on the files the library writes.
add_executable(integration_trace
    integration/main.cpp
    integration/test_trace_run.cpp
)
if(WIN32)
    target_sources(integration_trace PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/dynlib_win32.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/paths_win32.cpp)
else()
    target_sources(integration_trace PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/dynlib_linux.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/paths_linux.cpp)
    target_link_libraries(integration_trace PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(integration_trace PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include/eos
)
target_link_libraries(integration_trace PRIVATE doctest)
target_compile_definitions(integration_trace PRIVATE
    "EOSR_PROBE_DIR=\"${CMAKE_BINARY_DIR}/trace-probe\"")
add_dependencies(integration_trace eos_shared)
set_target_properties(integration_trace PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME integration_trace COMMAND integration_trace $<TARGET_FILE:eos_shared>)

if(UNIX AND CMAKE_NM)
    add_test(
        NAME export_surface
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DLIBRARY=$<TARGET_FILE:eos_shared>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake
    )
endif()
