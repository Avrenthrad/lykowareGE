# Exit codes and help text for the lyko dispatcher. No renderer is claimed.
function(check_lyko expected_exit expected_text)
    execute_process(
        COMMAND "${LYKO}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 20)
    if(NOT "${result}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR "Unexpected exit ${result} for '${ARGN}', expected ${expected_exit}\n${stdout}\n${stderr}")
    endif()
    string(FIND "${stdout}${stderr}" "${expected_text}" match)
    if(match EQUAL -1)
        message(FATAL_ERROR "Missing '${expected_text}' for '${ARGN}':\n${stdout}\n${stderr}")
    endif()
    if("${expected_exit}" STREQUAL "3")
        string(FIND "${stdout}${stderr}" "Foundation check complete" launched)
        if(NOT launched EQUAL -1)
            message(FATAL_ERROR "Unsupported command launched the sandbox:\n${stdout}\n${stderr}")
        endif()
    endif()
endfunction()

check_lyko(0 "Usage:" --help)
check_lyko(0 "lyko run" --help)
check_lyko(0 "unsupported: no renderer" --help)
check_lyko(0 "lyko ${EXPECTED_VERSION}" --version)
check_lyko(0 "Foundation check complete" run)
check_lyko(3 "unsupported: no renderer" screenshot)
check_lyko(3 "unsupported: no renderer" trace)
check_lyko(3 "unsupported: no renderer" run --scene)
check_lyko(3 "unsupported: no renderer" run --scene fixture)
check_lyko(2 "Unknown command:" nope)
check_lyko(2 "run accepts no options" run --extra)
