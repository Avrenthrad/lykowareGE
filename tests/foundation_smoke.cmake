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
check_run(1 "CPU trace requires --frames" --frames 2)
check_run(1 "CPU trace requires --frames" --trace "${TRACE_DIR}/unused.json")

if(NOT TRACE_DIR)
    message(FATAL_ERROR "TRACE_DIR is required")
endif()
set(sandbox_trace "${TRACE_DIR}/sandbox-direct-trace.json")
file(REMOVE "${sandbox_trace}")
check_run(0 "Wrote CPU trace." --frames 3 --trace "${sandbox_trace}")
file(READ "${sandbox_trace}" sandbox_json)
string(REGEX MATCHALL "sandbox.frame" sandbox_frames "${sandbox_json}")
list(LENGTH sandbox_frames sandbox_frame_count)
if(NOT sandbox_frame_count EQUAL 3)
    message(FATAL_ERROR "Expected 3 sandbox.frame events, got ${sandbox_frame_count}:\n${sandbox_json}")
endif()
string(FIND "${sandbox_json}" "sandbox.loop" sandbox_loop)
if(sandbox_loop EQUAL -1)
    message(FATAL_ERROR "Sandbox trace is missing sandbox.loop:\n${sandbox_json}")
endif()

check_run(0 "Opened sandbox window." --window --dummy --frames 1)
check_run(1 "requires --frames N" --window --dummy)
check_run(1 "--dummy cannot host Vulkan" --window --dummy --vulkan --frames 1)
check_run(1 "requires --frames N" --window --offscreen --vulkan)
