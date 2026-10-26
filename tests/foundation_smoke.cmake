# Exercise the executable as a user would, including failure exit codes.
function(check_run expected_exit expected_text)
    execute_process(
        COMMAND "${SAMPLE}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 10)
    if(NOT "${result}" STREQUAL "${expected_exit}")
        message(FATAL_ERROR "Unexpected exit: ${result}\n${stdout}\n${stderr}")
    endif()
    string(FIND "${stdout}${stderr}" "${expected_text}" match)
    if(match EQUAL -1)
        message(FATAL_ERROR "Missing '${expected_text}':\n${stdout}\n${stderr}")
    endif()
endfunction()

check_run(0 "[INFO] Starting lykowareGE ${EXPECTED_VERSION}")
check_run(0 "lykowareGE ${EXPECTED_VERSION}" --version)
check_run(0 "Usage: lykoware_sandbox" --help)
check_run(1 "[ERROR] Unknown option:" --unknown)
check_run(1 "[ERROR] Expected at most one option." --version extra)
