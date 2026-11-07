# sandbox.gpu_trace: the sandbox's Vulkan window on the offscreen driver writes a Chrome trace with
# each frame's CPU zones and, where the device has GPU timestamps, one GPU span per frame
# (ADR 0039). Without a usable Vulkan device the sandbox exits 3 and this prints "SKIP:", which
# CTest reports as Skipped.
#
# Inputs: SAMPLE (built sandbox executable), WORK_DIR (scratch directory under the build tree;
# recreated on every run).

foreach(required IN ITEMS SAMPLE WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "sandbox_gpu_trace.cmake: ${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(trace "${WORK_DIR}/gpu trace.json")
set(frames 5)

execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --no-audio --frames ${frames} --trace "${trace}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message(STATUS "sandbox output:\n${output}")
if(status EQUAL 3)
    message(STATUS "SKIP: no usable Vulkan device for the sandbox window")
    return()
endif()
if(NOT status EQUAL 0)
    message(FATAL_ERROR "expected exit 0, got ${status}")
endif()
if(NOT output MATCHES "Wrote CPU and GPU trace\\.")
    message(FATAL_ERROR "the sandbox did not report the trace")
endif()

file(READ "${trace}" json)
string(REGEX MATCHALL "\"name\":\"sandbox\\.frame\",\"cat\":\"cpu\"" cpu_frames "${json}")
list(LENGTH cpu_frames cpu_count)
if(NOT cpu_count EQUAL frames)
    message(FATAL_ERROR "expected ${frames} sandbox.frame zones, found ${cpu_count}")
endif()
foreach(zone IN ITEMS sandbox.simulate sandbox.render)
    if(NOT json MATCHES "\"name\":\"${zone}\",\"cat\":\"cpu\"")
        message(FATAL_ERROR "the trace has no ${zone} zone")
    endif()
endforeach()

string(REGEX MATCHALL "\"name\":\"gpu\\.frame\",\"cat\":\"gpu\",\"ph\":\"X\",\"ts\":[0-9]+,\"dur\":[0-9]+,\"pid\":1,\"tid\":2}"
       gpu_frames "${json}")
list(LENGTH gpu_frames gpu_count)
if(output MATCHES "GPU time per frame: [0-9]+\\.[0-9][0-9][0-9] ms on average over ${frames} frames\\.")
    if(NOT gpu_count EQUAL frames)
        message(FATAL_ERROR "the device has timestamps; expected ${frames} GPU frame spans, found ${gpu_count}")
    endif()
    if(NOT json MATCHES "\"tid\":2,\"args\":{\"name\":\"GPU\"}")
        message(FATAL_ERROR "the GPU track is not named")
    endif()
elseif(output MATCHES "No GPU frame times: the device has no timestamps\\.")
    if(NOT gpu_count EQUAL 0)
        message(FATAL_ERROR "GPU spans without timestamps")
    endif()
    message(STATUS "note: the device has no GPU timestamps; only CPU zones were checked")
else()
    message(FATAL_ERROR "the sandbox did not report GPU frame times for all ${frames} frames")
endif()
