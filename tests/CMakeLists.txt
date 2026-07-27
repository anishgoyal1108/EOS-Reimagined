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
    unit/test_label_registry.cpp
    unit/test_peer_fp.cpp
    unit/test_config.cpp
    unit/test_config_file.cpp
    unit/test_bootstrap_descriptor.cpp
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
    unit/test_stub_completion.cpp
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

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME alpha_runner_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_alpha_runner.py
    )
    add_test(
        NAME release_package_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_release_package.py
    )
    add_test(
        NAME manager_package_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_manager_package.py
    )
endif()

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
    "EOSR_PROBE_DIR=\"${CMAKE_BINARY_DIR}/trace-probe\""
    "EOSR_EXPECTED_BUILD_ID=\"${EOSR_BUILD_ID}\"")
add_dependencies(integration_trace eos_shared)
set_target_properties(integration_trace PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME integration_trace COMMAND integration_trace $<TARGET_FILE:eos_shared>)

# This probe writes eosr-bootstrap.json beside the built module, changes the test working directory,
# then loads the real SDK. The native and Wine runs therefore prove loader-relative lookup and the
# selected profile directory through the public ABI rather than through a test-only SDK export.
add_executable(bootstrap_descriptor_probe
    integration/bootstrap_descriptor_probe.cpp
)
if(WIN32)
    target_sources(bootstrap_descriptor_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/dynlib_win32.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/paths_win32.cpp)
else()
    target_sources(bootstrap_descriptor_probe PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/dynlib_linux.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/paths_linux.cpp)
    target_link_libraries(bootstrap_descriptor_probe PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(bootstrap_descriptor_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include/eos
)
add_dependencies(bootstrap_descriptor_probe eos_shared)
set_target_properties(bootstrap_descriptor_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bootstrap-probe-cwd)
foreach(mode descriptor environment invalid)
    add_test(
        NAME integration_bootstrap_descriptor_${mode}
        COMMAND bootstrap_descriptor_probe $<TARGET_FILE:eos_shared> ${mode}
    )
    set_tests_properties(integration_bootstrap_descriptor_${mode} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bootstrap-probe-cwd
        RESOURCE_LOCK eosr_bootstrap_descriptor)
endforeach()

# Two independent processes load only the real C ABI, then discover, socialize, join a session, and
# exchange a P2P packet. The CMake coordinator works natively and through the configured Wine
# emulator without adding a process-spawn API to the shipping library.
add_executable(abi_mesh_probe integration/abi_mesh_probe.cpp)
if(WIN32)
    target_sources(abi_mesh_probe PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/dynlib_win32.cpp)
else()
    target_sources(abi_mesh_probe PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/dynlib_linux.cpp)
    target_link_libraries(abi_mesh_probe PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(abi_mesh_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include/eos
)
add_dependencies(abi_mesh_probe eos_shared)
set_target_properties(abi_mesh_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(
    NAME integration_abi_mesh
    COMMAND ${CMAKE_COMMAND}
        "-DWORKER=$<TARGET_FILE:abi_mesh_probe>"
        "-DLIBRARY=$<TARGET_FILE:eos_shared>"
        "-DPROBE_ROOT=${CMAKE_BINARY_DIR}/abi-mesh-probe"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_abi_mesh_probe.cmake
)
set_tests_properties(integration_abi_mesh PROPERTIES TIMEOUT 60)

if(UNIX AND CMAKE_NM)
    add_test(
        NAME export_surface
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DLIBRARY=$<TARGET_FILE:eos_shared>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake
    )
endif()

if(BUILD_MANAGER AND UNIX AND NOT APPLE)
    add_executable(manager_platform_tests
        unit/main.cpp
        manager/test_proton_paths.cpp
        manager/test_manager_discovery_filesystem.cpp
        manager/test_manager_transactions_platform.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/paths_linux.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/proton_paths_linux.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/manager_discovery_linux.cpp
        ${CMAKE_SOURCE_DIR}/src/manager/steam_discovery.cpp
        ${CMAKE_SOURCE_DIR}/src/manager/vdf.cpp
    )
    target_include_directories(manager_platform_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include/eos
    )
    target_link_libraries(manager_platform_tests PRIVATE
        eosr_manager_core eosr_manager_platform doctest ${CMAKE_DL_LIBS})
    target_compile_definitions(manager_platform_tests PRIVATE
        EOSR_MANAGER_TEST_DIR="${CMAKE_BINARY_DIR}/manager-platform-fixtures")
    add_test(NAME manager_platform_tests COMMAND manager_platform_tests)
endif()

if(BUILD_MANAGER AND WIN32)
    add_executable(manager_win_platform_tests
        unit/main.cpp
        manager/test_manager_transactions_win32.cpp
    )
    target_include_directories(manager_win_platform_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include/eos
    )
    target_link_libraries(manager_win_platform_tests PRIVATE
        eosr_manager_core eosr_manager_platform doctest)
    add_test(NAME manager_win_platform_tests COMMAND manager_win_platform_tests)
endif()

if(BUILD_MANAGER)
    add_executable(manager_tests
        unit/main.cpp
        manager/test_steam_launch.cpp
        manager/test_vdf.cpp
        manager/test_steam_discovery.cpp
        manager/test_target_inspection.cpp
        manager/test_ui_policy.cpp
        manager/test_release_catalog.cpp
        manager/test_manager_state.cpp
        manager/test_install_transaction.cpp
        manager/test_configuration_model.cpp
        manager/test_configuration_conformance.cpp
        manager/test_instance_service.cpp
        manager/test_run_service.cpp
    )
    target_link_libraries(manager_tests PRIVATE eosr_manager_core eosr_core doctest)
    add_test(NAME manager_tests COMMAND manager_tests)
endif()

if(BUILD_MANAGER)
    add_test(NAME manager_cli_smoke COMMAND eosr_manager_cli version)
    if(Python3_Interpreter_FOUND AND UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME manager_cli_workflow
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_manager_cli.py
                         --cli $<TARGET_FILE:eosr_manager_cli>)
        add_test(NAME manager_loaded_sdk_workflow
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/test_manager_loaded_sdk.py
                         --cli $<TARGET_FILE:eosr_manager_cli>
                         --probe $<TARGET_FILE:bootstrap_descriptor_probe>
                         --artifact $<TARGET_FILE:eos_shared>)
    endif()
    if(WIN32)
        add_test(NAME manager_gui_smoke COMMAND eosr_manager --smoke-test)
    else()
        find_program(XVFB_RUN_EXECUTABLE xvfb-run)
        if(XVFB_RUN_EXECUTABLE)
            add_test(NAME manager_gui_smoke
                     COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:eosr_manager> --smoke-test)
        elseif(DEFINED ENV{DISPLAY} AND NOT "$ENV{DISPLAY}" STREQUAL "")
            add_test(NAME manager_gui_smoke COMMAND eosr_manager --smoke-test)
        endif()
    endif()
endif()
