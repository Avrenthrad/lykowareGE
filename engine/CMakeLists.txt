configure_file(
    include/lykoware/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/lykoware/version.hpp"
    @ONLY)

add_library(lykoware_engine STATIC
    src/foot_plant.cpp
    src/animation_graph.cpp
    src/retarget.cpp
    src/character_asset.cpp
    src/assets.cpp
    src/audio.cpp
    src/audio_stream.cpp
    src/audio_output.cpp
    src/ik.cpp
    src/animation.cpp
    src/skinning.cpp
    src/audio_spatial.cpp
    src/navigation_crowd.cpp
    src/navigation.cpp
    src/jobs.cpp
    src/clock.cpp
    src/scene.cpp
    src/scene_script.cpp
    src/ecs.cpp
    src/follow_camera.cpp
    src/gltf.cpp
    src/image/image_decode.cpp
    src/mesh.cpp
    src/world_identity.cpp
    src/locomotion.cpp
    src/log.cpp
    src/paths.cpp
    src/physics.cpp
    src/ragdoll.cpp
    src/physics_ecs.cpp
    src/npc.cpp
    src/player.cpp
    src/render.cpp
    src/script/script.cpp
    src/trace.cpp
    src/image.cpp
    src/input.cpp
    src/platform_map.cpp
    src/window.cpp
    src/rhi/skin_arena.cpp
    src/rhi/buffer.cpp
    src/rhi/common.cpp
    src/rhi/instance.cpp
    src/rhi/device.cpp
    src/rhi/render_target.cpp
    src/rhi/swapchain.cpp
    src/rhi/shadow_map.cpp
    src/rhi/hdr_target.cpp
    src/rhi/texture.cpp
    src/rhi/pipeline.cpp)
add_library(lykoware::engine ALIAS lykoware_engine)
target_compile_features(lykoware_engine PUBLIC cxx_std_20)
target_include_directories(lykoware_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(lykoware_engine PRIVATE SDL3-static lykoware_volk lykoware_vma lykoware_wuffs lykoware_lua Jolt)

target_link_libraries(lykoware_engine PRIVATE Threads::Threads)

lykoware_configure_target(lykoware_engine)

# The mixer's output is bit-identical across compilers (ADR 0032), so the compiler must not
# fuse a multiply and an add into one instruction with a single rounding. MSVC's default
# /fp:precise already does not.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/audio.cpp src/audio_spatial.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# JSON stays private behind scene.hpp; upstream licence/notices are vendored.
target_include_directories(lykoware_engine SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/json/include")
