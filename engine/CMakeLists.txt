configure_file(
    include/lykoware/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/lykoware/version.hpp"
    @ONLY)

add_library(lykoware_engine STATIC
    src/clock.cpp
    src/scene.cpp
    src/ecs.cpp
    src/world_identity.cpp
    src/log.cpp
    src/paths.cpp
    src/physics.cpp
    src/physics_ecs.cpp
    src/render.cpp
    src/trace.cpp
    src/image.cpp
    src/input.cpp
    src/platform_map.cpp
    src/window.cpp
    src/rhi/buffer.cpp
    src/rhi/common.cpp
    src/rhi/instance.cpp
    src/rhi/device.cpp
    src/rhi/render_target.cpp
    src/rhi/swapchain.cpp
    src/rhi/pipeline.cpp)
add_library(lykoware::engine ALIAS lykoware_engine)
target_compile_features(lykoware_engine PUBLIC cxx_std_20)
target_include_directories(lykoware_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(lykoware_engine PRIVATE SDL3-static lykoware_volk lykoware_vma Jolt)

lykoware_configure_target(lykoware_engine)

# JSON stays private behind scene.hpp; upstream licence/notices are vendored.
target_include_directories(lykoware_engine SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/json/include")
