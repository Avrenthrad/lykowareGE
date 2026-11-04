# Vendored Jolt Physics v5.6.0 (ADR 0003, ADR 0018). See third_party/jolt/PROVENANCE.md.
# Options are forced so a preset cannot change them: the build must be identical on every
# platform for cross-platform determinism, and nothing may be downloaded. Jolt's own warnings
# are not promoted to errors (it is vendored code, like SDL).

function(lykoware_add_jolt)
    # Owner decisions in ADR 0003: cross-platform determinism on, float precision.
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "Same physics results on every compiler, OS and CPU" FORCE)
    set(DOUBLE_PRECISION OFF CACHE BOOL "Float precision until the world-scope decision" FORCE)

    # Build integration: a static library compiled with the same runtime as the engine.
    set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "Lykoware links Jolt statically" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use the engine's MSVC runtime" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "No link-time optimisation inside the vendored library" FORCE)
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "Keep CMake's per-configuration flags" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Vendored warnings are not errors" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "Lykoware installs its own programs only" FORCE)
    # RTTI on so sanitizer builds can check calls into Jolt's polymorphic classes.
    set(CPP_RTTI_ENABLED ON CACHE BOOL "Match the engine's RTTI" FORCE)

    # Not used yet: debug drawing, Jolt's profiler, object streams and GPU compute (hair
    # simulation only; its backends need DXC, which is not vendored).
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "No Jolt debug renderer" FORCE)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "No Jolt profiler" FORCE)
    set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "No Jolt object streams" FORCE)
    set(JPH_USE_DX12 OFF CACHE BOOL "No GPU compute" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "No GPU compute" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "No GPU compute" FORCE)
    set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "No compute emulation" FORCE)

    # SYSTEM: Jolt's headers are vendored code, so the engine's warnings and clang-tidy do
    # not report inside them.
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/jolt/Build" "${CMAKE_BINARY_DIR}/third_party/jolt"
                     EXCLUDE_FROM_ALL SYSTEM)
    if(NOT TARGET Jolt)
        message(FATAL_ERROR "Vendored Jolt did not produce the Jolt library target")
    endif()

    if(LYKOWARE_SANITIZERS)
        target_compile_options(Jolt PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(Jolt PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
