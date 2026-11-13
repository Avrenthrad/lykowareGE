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

# The locomotion options (ADR 0074): refused unless valid and with --play.
foreach(refused IN ITEMS "--agility;2" "--agility;x" "--agility;-0.1" "--directions" "--agility;0.5")
    execute_process(
        COMMAND "${SAMPLE}" --window --dummy --frames 1 ${refused}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE refusal
        ERROR_VARIABLE refusal)
    if(status EQUAL 0)
        message(FATAL_ERROR "${refused} must be refused; got ${status}:\n${refusal}")
    endif()
endforeach()
execute_process(
    COMMAND "${SAMPLE}" --window --dummy --frames 1 --play --agility 2
    RESULT_VARIABLE status OUTPUT_VARIABLE refusal ERROR_VARIABLE refusal)
if(status EQUAL 0 OR NOT refusal MATCHES "--agility must be a number from 0 to 1")
    message(FATAL_ERROR "--agility 2 must be refused for its range; got ${status}:\n${refusal}")
endif()
execute_process(
    COMMAND "${SAMPLE}" --window --dummy --frames 1 --directions
    RESULT_VARIABLE status OUTPUT_VARIABLE refusal ERROR_VARIABLE refusal)
if(status EQUAL 0 OR NOT refusal MATCHES "require --play")
    message(FATAL_ERROR "--directions without --play must be refused; got ${status}:\n${refusal}")
endif()
execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --play --directions --play-input "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE refusal ERROR_VARIABLE refusal)
if(status EQUAL 0 OR NOT refusal MATCHES "--directions cannot mix with --play-input")
    message(FATAL_ERROR "--directions with --play-input must be refused; got ${status}:\n${refusal}")
endif()
execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --play --frames 1 --locomotion "${CMAKE_CURRENT_LIST_DIR}/no_such_profile.json"
    RESULT_VARIABLE status OUTPUT_VARIABLE refusal ERROR_VARIABLE refusal)
if(status EQUAL 0 OR NOT refusal MATCHES "Cannot open locomotion profile")
    message(FATAL_ERROR "a missing --locomotion file must be refused; got ${status}:\n${refusal}")
endif()
# A file that is not a valid profile is refused with the decoder's error.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sandbox_bad_locomotion.json" "{\"version\":1,\"agility\":3}")
execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --play --frames 1
        --locomotion "${CMAKE_CURRENT_BINARY_DIR}/sandbox_bad_locomotion.json"
    RESULT_VARIABLE status OUTPUT_VARIABLE refusal ERROR_VARIABLE refusal)
if(status EQUAL 0 OR NOT refusal MATCHES "Locomotion profile: ")
    message(FATAL_ERROR "an invalid --locomotion file must be refused; got ${status}:\n${refusal}")
endif()

# --directions: the fixed direction-change sequence drives the player on fixed laps, running the
# whole fixture when --frames is not given.
execute_process(
    COMMAND "${SAMPLE}" --window --offscreen --vulkan --play --no-audio --directions --agility 0.8
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message(STATUS "sandbox --directions output:\n${output}")
if(status EQUAL 3)
    message(STATUS "SKIP: no usable Vulkan device for the sandbox window")
    return()
endif()
if(NOT status EQUAL 0)
    message(FATAL_ERROR "--directions: expected exit 0, got ${status}")
endif()
if(NOT output MATCHES "Locomotion: agility 0\\.800000, pivots ([0-9]+), gait ([A-Za-z]+), speed -?[0-9.]+ m/s")
    message(FATAL_ERROR "--directions did not log the Locomotion line")
endif()
if(CMAKE_MATCH_1 LESS 2)
    message(FATAL_ERROR "--directions: expected at least 2 pivots, got ${CMAKE_MATCH_1}")
endif()
if(NOT output MATCHES "The player ended at \\((-?[0-9.]+), (-?[0-9.]+), (-?[0-9.]+)\\)")
    message(FATAL_ERROR "--directions: no end position logged")
endif()
set(end_x "${CMAKE_MATCH_1}")
set(end_z "${CMAKE_MATCH_3}")
if(end_x MATCHES "^-?0\\.0" AND end_z MATCHES "^21\\.0")
    message(FATAL_ERROR "--directions: the player did not leave the start (${end_x}, ${end_z})")
endif()
if(NOT output MATCHES "Play: WASD[^\n]*Ctrl to walk")
    message(FATAL_ERROR "the play help line does not mention Ctrl to walk")
endif()
