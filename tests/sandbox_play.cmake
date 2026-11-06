# sandbox.play: the sandbox's --play mode on the offscreen driver (ADR 0047). It builds the
# obstacle course, puts the player at the start and draws it with the follow camera. With no
# input the player lands and stands at the start. Without a usable Vulkan device the sandbox
# exits 3 and this prints "SKIP:", which CTest reports as Skipped. --play without --vulkan is
# refused before any window opens.
#
# Inputs: SAMPLE (built sandbox executable).

if(NOT DEFINED SAMPLE)
    message(FATAL_ERROR "sandbox_play.cmake: SAMPLE is required")
endif()

execute_process(
    COMMAND "${SAMPLE}" --window --dummy --play --frames 1
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(status EQUAL 0 OR NOT output MATCHES "--play needs --vulkan")
    message(FATAL_ERROR "--play without --vulkan must be refused; got ${status}:\n${output}")
endif()

execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --play --no-audio --frames 60
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
if(NOT output MATCHES "Play: WASD")
    message(FATAL_ERROR "the sandbox did not start play mode")
endif()
# The start is (0, 0.05, 21); standing, the feet rest on the ground within 2 cm.
if(NOT output MATCHES "The player ended at \\(-?0\\.00[0-9]*, -?0\\.0[0-1][0-9]*, 21\\.00[0-9]*\\)")
    message(FATAL_ERROR "the player did not land and stand at the start")
endif()

if(NOT output MATCHES "Animation: GPU-skinned training mannequin")
    message(FATAL_ERROR "the GPU-skinned mannequin did not initialise")
endif()
