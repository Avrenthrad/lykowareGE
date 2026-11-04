# Exit codes and help text for the lyko dispatcher. Includes validation of the versioned scene command path.
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
check_lyko(0 "lyko trace --frames" --help)
check_lyko(0 "run --scene FILE" --help)
check_lyko(0 "lyko ${EXPECTED_VERSION}" --version)
check_lyko(0 "Foundation check complete" run)
check_lyko(2 "requires --scene" screenshot)
check_lyko(2 "missing scene option value" screenshot --harness)
check_lyko(2 "requires --scene" screenshot --output "not-a-screenshot.ppm")
check_lyko(2 "missing scene option value" run --scene)
check_lyko(2 "cannot read input" run --scene fixture)
check_lyko(2 "Unknown command:" nope)
check_lyko(2 "run accepts no options" run --extra)
check_lyko(2 "trace requires --frames" trace)
check_lyko(2 "Missing value for --frames" trace --frames)
check_lyko(2 "integer from 1 to 1000000" trace --frames 0)
check_lyko(2 "integer from 1 to 1000000" trace --frames -1)
check_lyko(2 "integer from 1 to 1000000" trace --frames abc)
check_lyko(2 "integer from 1 to 1000000" trace --frames 1000001)
check_lyko(2 "Unknown option for trace" trace --frames 1 --gpu)
check_lyko(2 "Duplicate option" trace --frames 1 --frames 2)

if(NOT TRACE_DIR)
    message(FATAL_ERROR "TRACE_DIR is required")
endif()
set(trace_file "${TRACE_DIR}/lyko-frames.json")
file(REMOVE "${trace_file}")
execute_process(
    COMMAND "${LYKO}" trace --frames 4 --output "${trace_file}"
    RESULT_VARIABLE trace_result
    OUTPUT_VARIABLE trace_stdout
    ERROR_VARIABLE trace_stderr
    TIMEOUT 20)
if(NOT trace_result EQUAL 0)
    message(FATAL_ERROR "trace --frames failed (${trace_result}):\n${trace_stdout}\n${trace_stderr}")
endif()
string(FIND "${trace_stderr}${trace_stdout}" "Wrote CPU trace." trace_log)
if(trace_log EQUAL -1)
    message(FATAL_ERROR "trace command did not report the file:\n${trace_stdout}\n${trace_stderr}")
endif()
file(READ "${trace_file}" trace_json)
string(REGEX MATCHALL "sandbox.frame" trace_frames "${trace_json}")
list(LENGTH trace_frames trace_frame_count)
if(NOT trace_frame_count EQUAL 4)
    message(FATAL_ERROR "Expected 4 sandbox.frame events, got ${trace_frame_count}:\n${trace_json}")
endif()
string(FIND "${trace_json}" "\"ph\":\"X\"" trace_phase)
if(trace_phase EQUAL -1)
    message(FATAL_ERROR "Chrome trace is missing complete events:\n${trace_json}")
endif()

set(default_dir "${TRACE_DIR}/trace-default")
file(REMOVE_RECURSE "${default_dir}")
file(MAKE_DIRECTORY "${default_dir}")
execute_process(
    COMMAND "${LYKO}" trace --frames 1
    WORKING_DIRECTORY "${default_dir}"
    RESULT_VARIABLE default_result
    OUTPUT_VARIABLE default_stdout
    ERROR_VARIABLE default_stderr
    TIMEOUT 20)
if(NOT default_result EQUAL 0 OR NOT EXISTS "${default_dir}/lykoware-cpu-trace.json")
    message(FATAL_ERROR "Default trace path was not written (${default_result}):\n${default_stdout}\n${default_stderr}")
endif()

execute_process(
    COMMAND "${LYKO}" trace --frames 1 --output "${TRACE_DIR}/missing-parent/trace.json"
    RESULT_VARIABLE missing_result
    ERROR_VARIABLE missing_stderr
    TIMEOUT 20)
if(NOT missing_result EQUAL 1)
    message(FATAL_ERROR "Unwritable trace returned ${missing_result}, expected 1:\n${missing_stderr}")
endif()
string(FIND "${missing_stderr}" "Could not write CPU trace." missing_text)
if(missing_text EQUAL -1)
    message(FATAL_ERROR "Missing parent did not report a write failure:\n${missing_stderr}")
endif()
