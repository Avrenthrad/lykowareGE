# Host tools: the Slang shader compiler and SPIRV-Tools (spirv-opt, spirv-val),
# built from the vendored sources in third_party/ (ADR 0013, ADR 0014).
#
# They run on the build machine only and are never shipped in the engine. They
# are built once per tool version, in Release, with their own flags (not the
# Lykoware warning, sanitizer or clang-tidy settings), into a cache directory
# shared by every build preset:
#
#   LYKOWARE_TOOL_CACHE (default: <source>/.lykoware-tools, ignored by git)
#     <key>/bin/slangc[.exe], spirv-opt[.exe], spirv-val[.exe]
#
# The key names the tool versions and this recipe's revision, so changing
# either rebuilds into a new directory. CI caches the directory with the GitHub
# Actions cache (owner approved 2026-11-03).
#
# The first configure on a machine builds Slang (about 12 minutes on 4 cores)
# and SPIRV-Tools (about 3 minutes). Nothing is downloaded: Slang's prebuilt
# LLVM and DXC downloads are switched off, along with every optional target.

set(LYKOWARE_SLANG_VERSION "2026.18.3")
set(LYKOWARE_SPIRV_TOOLS_VERSION "2026.4")
# Bump when the build options below change, so caches are not reused.
set(LYKOWARE_HOST_TOOLS_RECIPE "1")

set(LYKOWARE_TOOL_CACHE "${CMAKE_SOURCE_DIR}/.lykoware-tools" CACHE PATH
    "Directory holding prebuilt host tools (Slang, SPIRV-Tools), shared by presets")

function(_lykoware_run_step description)
    message(STATUS "Host tools: ${description}")
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Host tools: ${description} failed (${result}):\n${output}")
    endif()
endfunction()

# Configure, build and copy one host-tool project into ${install_dir}/bin.
function(_lykoware_build_tool name source_dir work_dir install_dir)
    cmake_parse_arguments(PARSE_ARGV 4 TOOL "" "" "TARGETS;OPTIONS;OUTPUT_DIRS")
    set(generator_args -G "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND generator_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND generator_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    file(REMOVE_RECURSE "${work_dir}")
    set(compiler_args "")
    if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
        # Single-config generators: use the same compilers as the engine.
        set(compiler_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    _lykoware_run_step("configuring ${name}"
        "${CMAKE_COMMAND}" -S "${source_dir}" -B "${work_dir}" ${generator_args}
            -DCMAKE_BUILD_TYPE=Release
            ${compiler_args}
            -DBUILD_TESTING=OFF
            ${TOOL_OPTIONS})
    include(ProcessorCount)
    ProcessorCount(jobs)
    if(jobs EQUAL 0)
        set(jobs 2)
    endif()
    _lykoware_run_step("building ${name} (${TOOL_TARGETS})"
        "${CMAKE_COMMAND}" --build "${work_dir}" --config Release --parallel ${jobs} --target ${TOOL_TARGETS})
    file(MAKE_DIRECTORY "${install_dir}/bin")
    foreach(relative IN LISTS TOOL_OUTPUT_DIRS)
        file(GLOB built LIST_DIRECTORIES false "${work_dir}/${relative}/*")
        foreach(file IN LISTS built)
            # Executables and shared libraries only; skip split debug info and
            # import/static libraries.
            if(NOT file MATCHES "\\.(dwarf|pdb|lib|a|exp|ilk)$")
                file(COPY "${file}" DESTINATION "${install_dir}/bin")
            endif()
        endforeach()
    endforeach()
endfunction()

function(lykoware_add_host_tools)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(key "slang-${LYKOWARE_SLANG_VERSION}_spirv-tools-${LYKOWARE_SPIRV_TOOLS_VERSION}_r${LYKOWARE_HOST_TOOLS_RECIPE}_${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
    set(root "${LYKOWARE_TOOL_CACHE}/${key}")
    set(exe "${CMAKE_EXECUTABLE_SUFFIX}")
    set(slangc "${root}/bin/slangc${exe}")
    set(spirv_opt "${root}/bin/spirv-opt${exe}")
    set(spirv_val "${root}/bin/spirv-val${exe}")

    # One configure at a time may build into the cache.
    file(MAKE_DIRECTORY "${LYKOWARE_TOOL_CACHE}")
    file(LOCK "${LYKOWARE_TOOL_CACHE}/${key}.lock" GUARD FUNCTION TIMEOUT 7200)

    if(NOT EXISTS "${root}/complete.stamp")
        message(STATUS "Host tools: building ${key} into ${LYKOWARE_TOOL_CACHE} (first time on this machine)")
        set(staging "${LYKOWARE_TOOL_CACHE}/${key}.partial")
        file(REMOVE_RECURSE "${staging}" "${root}")
        set(work "${LYKOWARE_TOOL_CACHE}/${key}.work")

        _lykoware_build_tool(SPIRV-Tools "${CMAKE_SOURCE_DIR}/third_party/spirv-tools" "${work}/spirv-tools" "${staging}"
            TARGETS spirv-opt spirv-val
            OPTIONS
                "-DSPIRV-Headers_SOURCE_DIR=${CMAKE_SOURCE_DIR}/third_party/spirv-headers"
                -DSPIRV_SKIP_TESTS=ON
                -DSPIRV_WERROR=OFF
                -DSPIRV_TOOLS_BUILD_STATIC=ON
                -DSPIRV_BUILD_FUZZER=OFF
                "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
            OUTPUT_DIRS tools tools/Release)

        _lykoware_build_tool(Slang "${CMAKE_SOURCE_DIR}/third_party/slang" "${work}/slang" "${staging}"
            TARGETS slangc
            OPTIONS
                -DSLANG_VERSION_FULL=v${LYKOWARE_SLANG_VERSION}
                -DSLANG_VERSION_NUMERIC=${LYKOWARE_SLANG_VERSION}
                # One self-contained slangc executable, relocatable into the cache.
                -DSLANG_LIB_TYPE=STATIC
                # No downloads: prebuilt LLVM and DXC stay off.
                -DSLANG_SLANG_LLVM_FLAVOR=DISABLE
                -DSLANG_ENABLE_DXIL=OFF
                # glslang and SPIRV-Tools inside Slang stay out (ADR 0013).
                -DSLANG_ENABLE_SLANG_GLSLANG=OFF
                -DSLANG_ENABLE_GFX=OFF
                -DSLANG_ENABLE_SLANG_RHI=OFF
                -DSLANG_ENABLE_TESTS=OFF
                -DSLANG_ENABLE_EXAMPLES=OFF
                -DSLANG_ENABLE_SLANGD=OFF
                -DSLANG_ENABLE_SLANGI=OFF
                -DSLANG_ENABLE_SLANGRT=OFF
                -DSLANG_ENABLE_CUDA=OFF
                -DSLANG_ENABLE_OPTIX=OFF
                -DSLANG_ENABLE_NVAPI=OFF
                -DSLANG_ENABLE_MIMALLOC=OFF
                -DSLANG_ENABLE_REPLAYER=OFF
                -DSLANG_ENABLE_RELEASE_DEBUG_INFO=OFF
                -DSLANG_ENABLE_SPLIT_DEBUG_INFO=OFF
                -DSLANG_INSTALL_USER_SKILLS=OFF
                -DLZ4_BUILD_CLI=OFF
                -DLZ4_BUILD_LEGACY_LZ4C=OFF
                # Shared vendored headers instead of Slang's own copies.
                "-DSLANG_OVERRIDE_SPIRV_HEADERS_PATH=${CMAKE_SOURCE_DIR}/third_party/spirv-headers"
                "-DSLANG_OVERRIDE_VULKAN_HEADERS_PATH=${CMAKE_SOURCE_DIR}/cmake/host-tools"
            OUTPUT_DIRS Release/bin Release/lib)

        foreach(tool IN ITEMS slangc spirv-opt spirv-val)
            if(NOT EXISTS "${staging}/bin/${tool}${exe}")
                message(FATAL_ERROR "Host tools: ${tool}${exe} was not produced in ${staging}/bin")
            endif()
        endforeach()
        file(WRITE "${staging}/complete.stamp" "${key}\n")
        file(RENAME "${staging}" "${root}")
        file(REMOVE_RECURSE "${work}")
    else()
        message(STATUS "Host tools: using cached ${key}")
    endif()

    set(LYKOWARE_SLANGC "${slangc}" PARENT_SCOPE)
    set(LYKOWARE_SPIRV_OPT "${spirv_opt}" PARENT_SCOPE)
    set(LYKOWARE_SPIRV_VAL "${spirv_val}" PARENT_SCOPE)
endfunction()
