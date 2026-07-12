# doctest is a dev-only, single-header dependency. It is compiled into the test binaries
# and never linked into the shipped library.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(unit_tests
    unit/main.cpp
    unit/test_frame_result.cpp
    unit/test_callback_manager.cpp
    unit/test_ids.cpp
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
)
target_link_libraries(unit_tests PRIVATE eosr_core doctest)

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

if(UNIX AND CMAKE_NM)
    add_test(
        NAME export_surface
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DLIBRARY=$<TARGET_FILE:eos_shared>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake
    )
endif()
