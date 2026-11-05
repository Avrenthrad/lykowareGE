cmake_minimum_required(VERSION 3.25)
# C is for vendored sources only (SDL3, volk). Engine code is C++20.
project(lykowareGE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/RuntimeNotices.cmake)
include(cmake/VendorSdl3.cmake)
lykoware_add_sdl3()
include(cmake/VendorVulkan.cmake)
lykoware_add_vulkan()
include(cmake/VendorJolt.cmake)
lykoware_add_jolt()
include(cmake/HostTools.cmake)
lykoware_add_host_tools()
include(cmake/Shaders.cmake)
add_subdirectory(engine)
add_subdirectory(samples/sandbox)
add_subdirectory(tools/cli)
lykoware_install_runtime_notices()

if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME package.runtime_notices COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/package_notices.py"
        "${CMAKE_COMMAND}" "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" "$<CONFIG>" "$<TARGET_EXISTS:lykoware_wuffs>")
    set_tests_properties(package.runtime_notices PROPERTIES TIMEOUT 120)

    add_executable(lykoware_trace_test tests/trace_api.cpp)
    target_link_libraries(lykoware_trace_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_trace_test)
    add_test(NAME trace.api COMMAND lykoware_trace_test)

    # The simulation clock (ADR 0017): exact tick counts, frame-rate
    # independence and limits, all from fixed frame times.
    add_executable(lykoware_clock_test tests/clock.cpp)
    target_link_libraries(lykoware_clock_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_clock_test)
    add_test(NAME clock.fixed_step COMMAND lykoware_clock_test)
    set_tests_properties(clock.fixed_step PROPERTIES TIMEOUT 30)

    # GPU-free image harness. These tests compare a software pattern with
    # committed PPM fixtures. They are not renderer screenshot tests.
    # Outputs are confined to the build directory. lyko screenshot stays
    # exit 3; this executable is the test-only harness path.
    add_executable(lykoware_image_harness_test tests/image_harness.cpp)
    target_link_libraries(lykoware_image_harness_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_image_harness_test)
    set(LYKOWARE_IMAGE_HARNESS_GOLDEN "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    add_test(NAME image.harness.api
        COMMAND lykoware_image_harness_test api
            "${LYKOWARE_IMAGE_HARNESS_GOLDEN}"
            "${CMAKE_CURRENT_BINARY_DIR}/image-harness/api")
    add_test(NAME image.harness.golden_pass
        COMMAND lykoware_image_harness_test pass
            "${LYKOWARE_IMAGE_HARNESS_GOLDEN}"
            "${CMAKE_CURRENT_BINARY_DIR}/image-harness/pass")
    add_test(NAME image.harness.golden_fail
        COMMAND lykoware_image_harness_test fail
            "${LYKOWARE_IMAGE_HARNESS_GOLDEN}"
            "${CMAKE_CURRENT_BINARY_DIR}/image-harness/fail")
    set_tests_properties(image.harness.api image.harness.golden_pass image.harness.golden_fail
        PROPERTIES TIMEOUT 20)

    add_executable(lykoware_input_test tests/input_state.cpp)
    target_link_libraries(lykoware_input_test PRIVATE lykoware::engine)
    target_include_directories(lykoware_input_test PRIVATE
        "${CMAKE_SOURCE_DIR}/engine/src")
    target_include_directories(lykoware_input_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/sdl3/include")
    lykoware_configure_target(lykoware_input_test)
    add_test(NAME input.mapping COMMAND lykoware_input_test)
    set_tests_properties(input.mapping PROPERTIES TIMEOUT 20)

    add_executable(lykoware_window_test tests/window_lifecycle.cpp)
    target_link_libraries(lykoware_window_test PRIVATE lykoware::engine)
    target_include_directories(lykoware_window_test SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/sdl3/include")
    lykoware_configure_target(lykoware_window_test)
    add_test(NAME window.lifecycle COMMAND lykoware_window_test)
    set_tests_properties(window.lifecycle PROPERTIES TIMEOUT 30)

    # Needs a Vulkan loader at run time. Exit 77 is reported as Skipped, not
    # Passed, on a machine without one (ADR 0009).
    add_executable(lykoware_rhi_instance_test tests/rhi_instance.cpp)
    target_link_libraries(lykoware_rhi_instance_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_instance_test)
    add_test(NAME rhi.instance COMMAND lykoware_rhi_instance_test)
    # Same binary with every driver hidden from the loader: must report NoDriver.
    # VK_DRIVER_FILES is the current loader variable; VK_ICD_FILENAMES the older one.
    add_test(NAME rhi.instance.no_driver COMMAND lykoware_rhi_instance_test --expect-no-driver)
    set_tests_properties(rhi.instance.no_driver PROPERTIES
        ENVIRONMENT "VK_DRIVER_FILES=lykoware-no-such-driver.json;VK_ICD_FILENAMES=lykoware-no-such-driver.json")
    add_executable(lykoware_rhi_device_test tests/rhi_device.cpp)
    target_link_libraries(lykoware_rhi_device_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_device_test)
    add_test(NAME rhi.device COMMAND lykoware_rhi_device_test)
    add_executable(lykoware_rhi_render_target_test tests/rhi_render_target.cpp)
    target_link_libraries(lykoware_rhi_render_target_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_render_target_test)
    # Mismatch artefacts (actual and diff PPM) go under the build directory only.
    add_test(NAME rhi.render_target
        COMMAND lykoware_rhi_render_target_test "${CMAKE_CURRENT_BINARY_DIR}/rhi-render-target")
    set_tests_properties(rhi.instance rhi.instance.no_driver rhi.device rhi.render_target
        PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

    # Test shaders, compiled ahead of time from Slang and validated at build
    # time (cmake/Shaders.cmake). shaders.spirv_valid re-validates the outputs
    # so the evidence also appears in the test log.
    lykoware_compile_shader(triangle_vert_spv SOURCE tests/shaders/triangle.slang
        ENTRY vertexMain STAGE vertex NAME triangle.vert)
    lykoware_compile_shader(triangle_frag_spv SOURCE tests/shaders/triangle.slang
        ENTRY fragmentMain STAGE fragment NAME triangle.frag)
    lykoware_compile_shader(lit_vert_spv SOURCE engine/shaders/lit.slang
        ENTRY vertexMain STAGE vertex NAME lit.vert)
    lykoware_compile_shader(lit_frag_spv SOURCE engine/shaders/lit.slang
        ENTRY fragmentMain STAGE fragment NAME lit.frag)
    add_custom_target(lykoware_test_shaders ALL DEPENDS "${triangle_vert_spv}" "${triangle_frag_spv}"
        "${lit_vert_spv}" "${lit_frag_spv}")

    # The interactive path: present, resize, and the test triangle drawn
    # through the swapchain. Exit 77 is Skipped.
    add_executable(lykoware_rhi_swapchain_test tests/rhi_swapchain.cpp)
    target_link_libraries(lykoware_rhi_swapchain_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_swapchain_test)
    add_dependencies(lykoware_rhi_swapchain_test lykoware_test_shaders)
    add_test(NAME rhi.swapchain
        COMMAND lykoware_rhi_swapchain_test "${triangle_vert_spv}" "${triangle_frag_spv}")
    set_tests_properties(rhi.swapchain PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    add_test(NAME shaders.spirv_valid
        COMMAND "${LYKOWARE_SPIRV_VAL}" --target-env vulkan1.3 "${triangle_vert_spv}")
    add_test(NAME shaders.spirv_valid_fragment
        COMMAND "${LYKOWARE_SPIRV_VAL}" --target-env vulkan1.3 "${triangle_frag_spv}")
    set_tests_properties(shaders.spirv_valid shaders.spirv_valid_fragment PROPERTIES TIMEOUT 30)

    # First draw: the Slang triangle through rhi::GraphicsPipeline. Mismatch
    # images go under the build directory only. Exit 77 is Skipped.
    add_executable(lykoware_rhi_pipeline_test tests/rhi_pipeline.cpp)
    target_link_libraries(lykoware_rhi_pipeline_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_pipeline_test)
    add_dependencies(lykoware_rhi_pipeline_test lykoware_test_shaders)
    add_test(NAME rhi.pipeline
        COMMAND lykoware_rhi_pipeline_test "${triangle_vert_spv}" "${triangle_frag_spv}"
            "${CMAKE_CURRENT_BINARY_DIR}/rhi-pipeline")
    set_tests_properties(rhi.pipeline PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

    # Lit 3D meshes: buffers, depth, culling, push constants (ADR 0025). Exit 77 is Skipped.
    add_executable(lykoware_rhi_mesh_test tests/rhi_mesh.cpp)
    target_link_libraries(lykoware_rhi_mesh_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_rhi_mesh_test)
    add_dependencies(lykoware_rhi_mesh_test lykoware_test_shaders)
    add_test(NAME rhi.mesh
        COMMAND lykoware_rhi_mesh_test "${lit_vert_spv}" "${lit_frag_spv}" "${triangle_vert_spv}" "${triangle_frag_spv}"
            "${CMAKE_CURRENT_BINARY_DIR}/rhi-mesh")
    set_tests_properties(rhi.mesh PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

    # The falling-crates milestone: physics, ECS and the renderer together (ADR 0026).
    add_executable(lykoware_render_scene_test tests/render_scene.cpp)
    target_link_libraries(lykoware_render_scene_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_render_scene_test)
    add_dependencies(lykoware_render_scene_test lykoware_test_shaders)
    add_test(NAME render.scene
        COMMAND lykoware_render_scene_test "${lit_vert_spv}" "${lit_frag_spv}" "${CMAKE_CURRENT_BINARY_DIR}/render-scene")
    set_tests_properties(render.scene PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

    # The sandbox's Vulkan window on the headless offscreen driver: it loads
    # its deployed shaders and draws the triangle. The sandbox exits 3
    # ("unsupported") without a usable Vulkan device, which CTest reports as
    # Skipped; it exits 1 on any validation error.
    add_test(NAME sandbox.vulkan_window
        COMMAND lykoware_sandbox --window --offscreen --vulkan --frames 3)
    set_tests_properties(sandbox.vulkan_window PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 3)
    # A copy of the sandbox with no shaders folder beside it must fail with a
    # message naming the missing file, before any Vulkan work, so this runs
    # (and is not skipped) on machines without Vulkan too (ADR 0016).
    add_test(NAME sandbox.missing_shaders
        COMMAND "${CMAKE_COMMAND}"
            "-DSAMPLE=$<TARGET_FILE:lykoware_sandbox>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/sandbox-missing-shaders/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/sandbox_missing_shaders.cmake")
    set_tests_properties(sandbox.missing_shaders PROPERTIES TIMEOUT 60)

    if(LYKOWARE_SANITIZERS)
        # The Vulkan loader unloads drivers at vkDestroyInstance. Memory a
        # driver's own threads still hold (Mesa lavapipe's LLVM compiler
        # threads) then appears as unsymbolised "leaks". Keeping drivers loaded
        # (a Khronos loader option meant for leak checkers) removes those false
        # reports; leaks in Lykoware code are still caught (ADR 0015 records the
        # deliberate-leak check).
        set_property(TEST rhi.instance rhi.instance.no_driver rhi.device rhi.render_target rhi.swapchain
                          sandbox.vulkan_window rhi.pipeline rhi.mesh render.scene
            APPEND PROPERTY ENVIRONMENT "VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1")
    endif()

    # Physics spike (ADR 0003, ADR 0018): the vendored Jolt build, the Lykoware replacements
    # of its Numerical Recipes-derived functions, and cross-platform determinism. The test
    # links Jolt directly; game code will use a Lykoware wrapper.
    add_executable(lykoware_physics_jolt_test tests/physics_jolt.cpp)
    target_link_libraries(lykoware_physics_jolt_test PRIVATE lykoware::engine Jolt)
    lykoware_configure_target(lykoware_physics_jolt_test)
    add_test(NAME physics.jolt_math COMMAND lykoware_physics_jolt_test math)
    add_test(NAME physics.jolt_determinism COMMAND lykoware_physics_jolt_test determinism)
    set_tests_properties(physics.jolt_math physics.jolt_determinism PROPERTIES TIMEOUT 120)

    # The Lykoware physics API (ADR 0020). Links only the engine: no Jolt types are needed.
    add_executable(lykoware_physics_world_test tests/physics_world.cpp)
    target_link_libraries(lykoware_physics_world_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_physics_world_test)
    add_test(NAME physics.world COMMAND lykoware_physics_world_test)
    set_tests_properties(physics.world PROPERTIES TIMEOUT 120)

    # The maths module (ADR 0024). Links only the engine.
    add_executable(lykoware_math_test tests/math.cpp)
    target_link_libraries(lykoware_math_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_math_test)
    add_test(NAME math.types COMMAND lykoware_math_test)
    set_tests_properties(math.types PROPERTIES TIMEOUT 60)

    # The entity-component system (ADR 0023). Links only the engine.
    add_test(NAME scene.cli_capture COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/scene_capture.py" "$<TARGET_FILE:lyko>"
        "${CMAKE_SOURCE_DIR}/samples/scenes" "${CMAKE_BINARY_DIR}/test-output/scene-cli")
    set_tests_properties(scene.cli_capture PROPERTIES TIMEOUT 240 SKIP_RETURN_CODE 77)

    add_executable(lykoware_scene_replay_test tests/scene_replay.cpp)
    target_link_libraries(lykoware_scene_replay_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_scene_replay_test)
    add_test(NAME scene.replay COMMAND lykoware_scene_replay_test
        "${CMAKE_SOURCE_DIR}/samples/scenes/falling-crates.scene.json"
        "${CMAKE_SOURCE_DIR}/samples/scenes/falling-crates.commands.json")
    set_tests_properties(scene.replay PROPERTIES TIMEOUT 120)

    add_executable(lykoware_ecs_test tests/ecs.cpp)
    target_link_libraries(lykoware_ecs_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_ecs_test)
    add_test(NAME ecs.world COMMAND lykoware_ecs_test)
    set_tests_properties(ecs.world PROPERTIES TIMEOUT 120)

    # Physics bodies and characters on ECS entities (ADR 0023). Links only the engine.
    add_executable(lykoware_ecs_allocation_test tests/ecs_allocation.cpp)
    target_link_libraries(lykoware_ecs_allocation_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_ecs_allocation_test)
    add_test(NAME ecs.allocation_recovery COMMAND lykoware_ecs_allocation_test)
    set_tests_properties(ecs.allocation_recovery PROPERTIES TIMEOUT 120)

    add_executable(lykoware_physics_ecs_test tests/physics_ecs.cpp)
    target_link_libraries(lykoware_physics_ecs_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_physics_ecs_test)
    add_test(NAME physics.ecs_link COMMAND lykoware_physics_ecs_test)
    set_tests_properties(physics.ecs_link PROPERTIES TIMEOUT 120)

    # Characters on height-field terrain (physics spike 3). Links only the engine.
    add_executable(lykoware_physics_character_test tests/physics_character.cpp)
    target_link_libraries(lykoware_physics_character_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_physics_character_test)
    add_test(NAME physics.character COMMAND lykoware_physics_character_test)
    set_tests_properties(physics.character PROPERTIES TIMEOUT 120)

    # The same scene 16 km and 64 km from the origin (ADR 0021, ADR 0022). Links only the engine.
    add_executable(lykoware_physics_far_test tests/physics_far.cpp)
    target_link_libraries(lykoware_physics_far_test PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_physics_far_test)
    add_test(NAME physics.far_from_origin COMMAND lykoware_physics_far_test)
    set_tests_properties(physics.far_from_origin PROPERTIES TIMEOUT 120)

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

    # Physics body-count benchmark (physics spike 4). Provisional and CPU-only like the
    # checks above: the mean step limit is slack for unoptimised and sanitizer builds on a
    # busy shared runner, not a frame budget. See docs/PERFORMANCE.md.
    set(LYKOWARE_PROVISIONAL_PHYSICS_TICKS 300)
    set(LYKOWARE_PROVISIONAL_PHYSICS_MEAN_STEP_MS 250)
    add_executable(lykoware_physics_benchmark tests/physics_benchmark.cpp)
    target_link_libraries(lykoware_physics_benchmark PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_physics_benchmark)
    foreach(bodies IN ITEMS 256 1024)
        add_test(NAME perf.physics_bodies_${bodies}
            COMMAND lykoware_physics_benchmark ${bodies}
                "${LYKOWARE_PROVISIONAL_PHYSICS_TICKS}" "${LYKOWARE_PROVISIONAL_PHYSICS_MEAN_STEP_MS}")
        set_tests_properties(perf.physics_bodies_${bodies} PROPERTIES
            PASS_REGULAR_EXPRESSION "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04; within"
            TIMEOUT 300)
    endforeach()
    # ECS iteration benchmark (ADR 0023): a movement system over 100,000 entities. Provisional and
    # CPU-only like the checks above; the limit is slack for Debug and sanitizer builds.
    add_executable(lykoware_ecs_benchmark tests/ecs_benchmark.cpp)
    target_link_libraries(lykoware_ecs_benchmark PRIVATE lykoware::engine)
    lykoware_configure_target(lykoware_ecs_benchmark)
    add_test(NAME perf.ecs_iterate_100k COMMAND lykoware_ecs_benchmark 100000 300 250)
    set_tests_properties(perf.ecs_iterate_100k PROPERTIES
        PASS_REGULAR_EXPRESSION "provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04; within"
        TIMEOUT 300)

    # The same 1024 boxes 16 km from the origin (ADR 0021, ADR 0022).
    add_test(NAME perf.physics_bodies_1024_far
        COMMAND lykoware_physics_benchmark 1024
            "${LYKOWARE_PROVISIONAL_PHYSICS_TICKS}" "${LYKOWARE_PROVISIONAL_PHYSICS_MEAN_STEP_MS}" 16000)
    set_tests_properties(perf.physics_bodies_1024_far PROPERTIES
        PASS_REGULAR_EXPRESSION "1024 boxes 16000 m from the origin.*provisional CPU-only; pinned hosted runners windows-2022 and ubuntu-24.04; within"
        TIMEOUT 300)

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
    add_executable(lyko_tool_fixture tests/tool_fixture.cpp tools/cli/process.cpp)
    target_compile_features(lyko_tool_fixture PRIVATE cxx_std_20)
    lykoware_configure_target(lyko_tool_fixture)
    add_test(NAME cli.integration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_integration.py"
            "$<TARGET_FILE:lyko>" "$<TARGET_FILE:lyko_tool_fixture>"
            "${CMAKE_COMMAND}" "${CMAKE_CTEST_COMMAND}")
    set_tests_properties(cli.integration PROPERTIES TIMEOUT 120)
    add_test(NAME validation.runner COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/validation_runner.py" "${CMAKE_COMMAND}" "${CMAKE_CTEST_COMMAND}")
    set_tests_properties(validation.runner PROPERTIES TIMEOUT 120)

endif()

install(TARGETS lykoware_sandbox lyko RUNTIME DESTINATION bin)

# nlohmann/json is compiled into runtime/tools; preserve its MIT notice in staged builds.
install(FILES third_party/json/LICENSE.MIT third_party/json/PROVENANCE.md
    DESTINATION share/lykoware/licenses/json)
