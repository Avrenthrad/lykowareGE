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
    add_test(NAME foundation.smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DSAMPLE=$<TARGET_FILE:lykoware_sandbox>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/foundation_smoke.cmake")
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
