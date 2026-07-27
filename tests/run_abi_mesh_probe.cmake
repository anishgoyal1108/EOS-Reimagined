if(NOT DEFINED WORKER OR NOT DEFINED LIBRARY OR NOT DEFINED PROBE_ROOT)
    message(FATAL_ERROR "WORKER, LIBRARY, and PROBE_ROOT are required")
endif()

file(REMOVE_RECURSE "${PROBE_ROOT}")
foreach(dir alice-data bob-data alice-run bob-run coord)
    file(MAKE_DIRECTORY "${PROBE_ROOT}/${dir}")
endforeach()

set(alice_command ${WORKER} ${LIBRARY} alice
    ${PROBE_ROOT}/alice-data ${PROBE_ROOT}/alice-run ${PROBE_ROOT}/coord Alice)
set(bob_command ${WORKER} ${LIBRARY} bob
    ${PROBE_ROOT}/bob-data ${PROBE_ROOT}/bob-run ${PROBE_ROOT}/coord Bob)
if(DEFINED EMULATOR AND NOT "${EMULATOR}" STREQUAL "")
    list(INSERT alice_command 0 ${EMULATOR})
    list(INSERT bob_command 0 ${EMULATOR})
endif()

# execute_process starts a pipeline concurrently. The workers do not use stdin/stdout; their owned
# result files carry exact failures without a shell or platform-specific process launcher.
execute_process(
    COMMAND ${alice_command}
    COMMAND ${bob_command}
    RESULTS_VARIABLE results
    ERROR_VARIABLE errors
    TIMEOUT 45
)
if(NOT "${results}" STREQUAL "0;0")
    set(details "${errors}")
    foreach(role alice bob)
        if(EXISTS "${PROBE_ROOT}/coord/${role}.result")
            file(READ "${PROBE_ROOT}/coord/${role}.result" result)
            string(APPEND details " ${role}=${result}")
        endif()
    endforeach()
    message(FATAL_ERROR "ABI mesh workers failed (${results}):${details}")
endif()

foreach(role alice bob)
    file(READ "${PROBE_ROOT}/coord/${role}.result" result)
    if(NOT result STREQUAL "ok")
        message(FATAL_ERROR "${role} result: ${result}")
    endif()
    file(READ "${PROBE_ROOT}/${role}-run/trace.jsonl" ${role}_trace)
    file(READ "${PROBE_ROOT}/${role}-run/runtime.json" ${role}_runtime)
    string(FIND "${${role}_runtime}" "\"peer_seed_count\":1" seed_count)
    string(FIND "${${role}_runtime}" "127.0.0.1" raw_seed)
    if(seed_count EQUAL -1 OR NOT raw_seed EQUAL -1)
        message(FATAL_ERROR "${role} runtime did not apply or redacted peer seeds incorrectly")
    endif()
    foreach(required
        "\"event\":\"run_start\""
        "\"event\":\"profile\""
        "\"event\":\"adopt\""
        "\"fn\":\"EOS_Friends_GetFriendsCount\""
        "\"fn\":\"EOS_UserInfo_CopyUserInfo\""
        "\"event\":\"shutdown\""
    )
        string(FIND "${${role}_trace}" "${required}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${role} trace lacks ${required}")
        endif()
    endforeach()
endforeach()

string(FIND "${alice_trace}" "\"fn\":\"EOS_Sessions_UpdateSession\"" alice_session)
string(FIND "${alice_trace}" "\"fn\":\"EOS_P2P_SendPacket\"" alice_send)
string(FIND "${bob_trace}" "\"fn\":\"EOS_SessionSearch_Find\"" bob_search)
string(FIND "${bob_trace}" "\"fn\":\"EOS_Sessions_JoinSession\"" bob_join)
string(FIND "${bob_trace}" "\"fn\":\"EOS_P2P_ReceivePacket\"" bob_receive)
if(alice_session EQUAL -1 OR alice_send EQUAL -1 OR bob_search EQUAL -1 OR
   bob_join EQUAL -1 OR bob_receive EQUAL -1)
    message(FATAL_ERROR "session or P2P path is incomplete")
endif()

string(REGEX MATCH "\"event\":\"profile\"[^\n]*\"peer_fp\":\"([0-9a-f]+)\""
    alice_profile "${alice_trace}")
set(alice_fp "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"event\":\"profile\"[^\n]*\"peer_fp\":\"([0-9a-f]+)\""
    bob_profile "${bob_trace}")
set(bob_fp "${CMAKE_MATCH_1}")
if(alice_fp STREQUAL "" OR bob_fp STREQUAL "" OR alice_fp STREQUAL bob_fp)
    message(FATAL_ERROR "profiles are missing or not distinct")
endif()
string(FIND "${alice_trace}" "\"peer_fp\":\"${bob_fp}\"" alice_has_bob)
string(FIND "${bob_trace}" "\"peer_fp\":\"${alice_fp}\"" bob_has_alice)
if(alice_has_bob EQUAL -1 OR bob_has_alice EQUAL -1)
    message(FATAL_ERROR "cross-process peer fingerprints do not join")
endif()

file(READ "${PROBE_ROOT}/coord/alice.id" alice_id)
file(READ "${PROBE_ROOT}/coord/bob.id" bob_id)
foreach(trace alice_trace bob_trace)
    string(FIND "${${trace}}" "${alice_id}" raw_alice)
    string(FIND "${${trace}}" "${bob_id}" raw_bob)
    string(FIND "${${trace}}" "abi-private-payload" raw_payload)
    if(NOT raw_alice EQUAL -1 OR NOT raw_bob EQUAL -1 OR NOT raw_payload EQUAL -1)
        message(FATAL_ERROR "${trace} leaked a raw identity or packet payload")
    endif()
endforeach()
