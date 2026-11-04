add_executable(lykoware_sandbox main.cpp)
target_link_libraries(lykoware_sandbox PRIVATE lykoware::engine)

lykoware_configure_target(lykoware_sandbox)

# The sandbox's own shaders, compiled at build time (ADR 0014) and deployed
# beside the executable, where it finds them at run time (ADR 0016).
lykoware_compile_shader(sandbox_triangle_vert SOURCE shaders/triangle.slang
    ENTRY vertexMain STAGE vertex NAME sandbox_triangle.vert)
lykoware_compile_shader(sandbox_triangle_frag SOURCE shaders/triangle.slang
    ENTRY fragmentMain STAGE fragment NAME sandbox_triangle.frag)
lykoware_deploy_shaders(lykoware_sandbox INSTALL_DESTINATION bin
    SHADERS "${sandbox_triangle_vert}" "${sandbox_triangle_frag}")
