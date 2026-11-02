configure_file(
    include/lykoware/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/lykoware/version.hpp"
    @ONLY)

add_library(lykoware_engine STATIC src/log.cpp src/trace.cpp)
add_library(lykoware::engine ALIAS lykoware_engine)
target_compile_features(lykoware_engine PUBLIC cxx_std_20)
target_include_directories(lykoware_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

lykoware_configure_target(lykoware_engine)
