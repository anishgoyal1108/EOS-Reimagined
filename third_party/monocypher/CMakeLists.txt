# Vendored Monocypher 4.0.3 (X25519, ChaCha20-Poly1305). See UPSTREAM.md for provenance.
#
# Compiled as C++11 in its own target so the project's -Werror warning set is NOT applied to
# third-party source. Symbols are hidden; only our EOS_* flat surface is exported, and the
# Monocypher API is used exclusively from src/common/crypto.cpp.
add_library(monocypher STATIC monocypher.c)

# Build the C source as C++11 rather than enabling the C language project-wide (the project is CXX
# only); Monocypher supports both, and the header documents C99-or-C++.
set_source_files_properties(monocypher.c PROPERTIES LANGUAGE CXX)

set_target_properties(monocypher PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# PUBLIC so a PRIVATE consumer (eosr_core) sees the header while compiling, without the include
# leaking to that consumer's own dependents.
target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
