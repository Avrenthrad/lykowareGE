add_executable(lyko main.cpp commands.cpp process.cpp scene_command.cpp character_command.cpp)
target_link_libraries(lyko PRIVATE lykoware::engine)
target_include_directories(lyko PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
add_dependencies(lyko lykoware_sandbox)
lykoware_configure_target(lyko)

# Absolute sandbox path for the build tree. The installed layout prefers the
# sibling executable; see find_sandbox in commands.cpp.
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lyko_sandbox_path.hpp"
    CONTENT [[#pragma once

namespace lykoware::cli {
inline constexpr char built_sandbox_path[] = R"gen($<TARGET_FILE:lykoware_sandbox>)gen";
}
]])

target_include_directories(lyko SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/json/include")
if(WIN32)
    target_link_libraries(lyko PRIVATE Psapi)
endif()
lykoware_compile_shader(cli_lit_vert SOURCE "${CMAKE_SOURCE_DIR}/engine/shaders/lit.slang"
    ENTRY vertexMain STAGE vertex NAME cli_lit.vert)
lykoware_compile_shader(cli_lit_frag SOURCE "${CMAKE_SOURCE_DIR}/engine/shaders/lit.slang"
    ENTRY fragmentMain STAGE fragment NAME cli_lit.frag)
lykoware_deploy_shaders(lyko INSTALL_DESTINATION bin
    SHADERS "${cli_lit_vert}" "${cli_lit_frag}")
