# doctest is a dev-only, single-header dependency. It is compiled into the test binaries
# and never linked into the shipped library.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(unit_tests
    unit/main.cpp
    unit/test_frame_result.cpp
    unit/test_callback_manager.cpp
    unit/test_ids.cpp
)
target_link_libraries(unit_tests PRIVATE eosr_core doctest)

add_test(NAME unit_tests COMMAND unit_tests)
