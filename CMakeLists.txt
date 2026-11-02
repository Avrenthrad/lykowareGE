cmake_minimum_required(VERSION 3.25)
project(lykowareGE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(cmake/ProjectOptions.cmake)
add_subdirectory(engine)
add_subdirectory(samples/sandbox)
add_subdirectory(tools/cli)

if(BUILD_TESTING)
    add_executable(lykoware_trace_test tests/trace_api.cpp)
    target_link_libraries(lykoware_trace_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_trace_test)
    add_test(NAME trace.api COMMAND lykoware_trace_test)

    add_executable(lykoware_cpu_budget tests/cpu_budget.cpp tools/cli/process.cpp)
    target_link_libraries(lykoware_cpu_budget PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_cpu_budget)

    # Provisional, CPU-only thresholds. Valid only for the pinned hosted
    # runners windows-2022 and ubuntu-24.04. These are not an RTX 4070 or
    # MSI Claw profile, and they do not satisfy the hardware-profile or
    # image/performance-gate checklist items. Margins are wide so a busy
    # shared runner does not flake. Change them in this file; do not weaken
    # a failure through a local cache override. See docs/PERFORMANCE.md.
    set(LYKOWARE_PROVISIONAL_CPU_SANDBOX_STARTUP_MS 20000)
    set(LYKOWARE_PROVISIONAL_CPU_FIXTURE_ITERATIONS 1000000)
    set(LYKOWARE_PROVISIONAL_CPU_FIXTURE_MS 20000)

    add_test(NAME perf.sandbox_startup
        COMMAND lykoware_cpu_budget startup
            "$<TARGET_FILE:lykoware_sandbox>"
            "${LYKOWARE_PROVISIONAL_CPU_SANDBOX_STARTUP_MS}")
    add_test(NAME perf.cpu_fixture
        COMMAND lykoware_cpu_budget fixture
            "${LYKOWARE_PROVISIONAL_CPU_FIXTURE_ITERATIONS}"
            "${LYKOWARE_PROVISIONAL_CPU_FIXTURE_MS}")
    add_test(NAME perf.budget_rejects_overrun
        COMMAND lykoware_cpu_budget check 5000 0 over)
    add_test(NAME perf.budget_accepts_within
        COMMAND lykoware_cpu_budget check 1000 5 within)
    set_tests_properties(perf.sandbox_startup perf.cpu_fixture PROPERTIES
        PASS_REGULAR_EXPRESSION "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04; within"
        TIMEOUT 90)
    set_tests_properties(perf.budget_rejects_overrun perf.budget_accepts_within
        PROPERTIES TIMEOUT 20)

    add_test(NAME foundation.smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DSAMPLE=$<TARGET_FILE:lykoware_sandbox>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            "-DTRACE_DIR=$<TARGET_FILE_DIR:lykoware_sandbox>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/foundation_smoke.cmake")
    add_test(NAME cli.commands
        COMMAND "${CMAKE_COMMAND}"
            "-DLYKO=$<TARGET_FILE:lyko>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            "-DTRACE_DIR=$<TARGET_FILE_DIR:lyko>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_commands.cmake")
    set_tests_properties(cli.commands PROPERTIES TIMEOUT 60)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_executable(lyko_tool_fixture tests/tool_fixture.cpp tools/cli/process.cpp)
    target_compile_features(lyko_tool_fixture PRIVATE cxx_std_20)
    lykoware_configure_target(lyko_tool_fixture)
    add_test(NAME cli.integration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_integration.py"
            "$<TARGET_FILE:lyko>" "$<TARGET_FILE:lyko_tool_fixture>"
            "${CMAKE_COMMAND}" "${CMAKE_CTEST_COMMAND}")
    set_tests_properties(cli.integration PROPERTIES TIMEOUT 120)
endif()

install(TARGETS lykoware_sandbox lyko RUNTIME DESTINATION bin)
