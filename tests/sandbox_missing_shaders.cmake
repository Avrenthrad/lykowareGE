# sandbox.missing_shaders: run a copy of the sandbox that has no "shaders"
# folder beside it. It must fail (exit 1) before any window or Vulkan work,
# with a message naming the missing shader file (ADR 0016).
#
# Inputs: SAMPLE (built sandbox executable), WORK_DIR (scratch directory
# under the build tree; recreated on every run).

foreach(required IN ITEMS SAMPLE WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "sandbox_missing_shaders.cmake: ${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
cmake_path(GET SAMPLE FILENAME name)
set(copy "${WORK_DIR}/${name}")
file(COPY_FILE "${SAMPLE}" "${copy}")
if(EXISTS "${WORK_DIR}/shaders")
    message(FATAL_ERROR "the scratch copy unexpectedly has a shaders folder")
endif()

execute_process(
    COMMAND "${copy}" --window --offscreen --vulkan --frames 1
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message(STATUS "sandbox output:\n${output}")
if(NOT status EQUAL 1)
    message(FATAL_ERROR "expected exit 1 without shaders, got ${status}")
endif()
string(FIND "${output}" "sandbox_triangle.vert.spv" named)
if(named EQUAL -1)
    message(FATAL_ERROR "the error does not name the missing shader file")
endif()
string(FIND "${output}" "Opened sandbox window" opened)
if(NOT opened EQUAL -1)
    message(FATAL_ERROR "the sandbox opened a window before checking its shaders")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
