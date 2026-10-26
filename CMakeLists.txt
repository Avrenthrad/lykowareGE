cmake_minimum_required(VERSION 3.25)
project(lykowareGE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
add_subdirectory(engine)
add_subdirectory(samples/sandbox)

if(BUILD_TESTING)
    add_test(NAME foundation.smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DSAMPLE=$<TARGET_FILE:lykoware_sandbox>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/foundation_smoke.cmake")
endif()

install(TARGETS lykoware_sandbox RUNTIME DESTINATION bin)
