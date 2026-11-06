add_executable(lykoware_sandbox main.cpp)
target_link_libraries(lykoware_sandbox PRIVATE lykoware::engine)
# The obstacle course for --play, shared with the player.course test (ADR 0047).
target_include_directories(lykoware_sandbox PRIVATE "${CMAKE_SOURCE_DIR}/samples/course")

lykoware_configure_target(lykoware_sandbox)

# The engine's lit shader (ADR 0026), compiled at build time (ADR 0014) under
# sandbox names and deployed beside the executable, where it finds them at
# run time (ADR 0016).
lykoware_compile_shader(sandbox_lit_vert SOURCE "${CMAKE_SOURCE_DIR}/engine/shaders/lit.slang"
    ENTRY vertexMain STAGE vertex NAME sandbox_lit.vert)
lykoware_compile_shader(sandbox_lit_frag SOURCE "${CMAKE_SOURCE_DIR}/engine/shaders/lit.slang"
    ENTRY fragmentMain STAGE fragment NAME sandbox_lit.frag)
lykoware_compile_shader(sandbox_skin_vert SOURCE "${CMAKE_SOURCE_DIR}/engine/shaders/skinned.slang"
    ENTRY vertexMain STAGE vertex NAME sandbox_skin.vert)
lykoware_deploy_shaders(lykoware_sandbox INSTALL_DESTINATION bin
    SHADERS "${sandbox_lit_vert}" "${sandbox_lit_frag}" "${sandbox_skin_vert}")
