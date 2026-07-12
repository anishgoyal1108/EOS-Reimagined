if(NOT DEFINED NM OR NOT DEFINED LIBRARY)
    message(FATAL_ERROR "NM and LIBRARY are required")
endif()

execute_process(
    COMMAND "${NM}" -D --defined-only "${LIBRARY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "nm failed: ${error}")
endif()

string(REPLACE "\n" ";" lines "${output}")
set(unexpected)
foreach(line IN LISTS lines)
    if(line STREQUAL "")
        continue()
    endif()
    string(REGEX REPLACE ".*[ \t]([^ \t]+)$" "\\1" symbol "${line}")
    if(NOT symbol MATCHES "^EOS_")
        list(APPEND unexpected "${symbol}")
    endif()
endforeach()

if(unexpected)
    list(JOIN unexpected "\n  " formatted)
    message(FATAL_ERROR "non-EOS symbols exported:\n  ${formatted}")
endif()
