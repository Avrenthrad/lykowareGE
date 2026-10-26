configure_file(
    include/lykoware/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/lykoware/version.hpp"
    @ONLY)

add_library(lykoware_engine STATIC src/log.cpp)
add_library(lykoware::engine ALIAS lykoware_engine)
target_compile_features(lykoware_engine PUBLIC cxx_std_20)
target_include_directories(lykoware_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

if(MSVC)
    target_compile_options(lykoware_engine PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lykoware_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
