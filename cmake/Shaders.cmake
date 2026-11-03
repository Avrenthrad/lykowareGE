# Ahead-of-time shader compilation (ADR 0002, ADR 0013, ADR 0014).
#
# One Slang entry point becomes one SPIR-V file under <build>/shaders/:
#   1. slangc -O0, warnings as errors, SPIR-V 1.6. -O0 because Slang's own
#      optimiser lives in the slang-glslang part that is not built (ADR 0013).
#   2. spirv-opt -O (vendored SPIRV-Tools) for the Vulkan 1.3 environment.
#   3. spirv-val. The final file is written only after validation passes, so a
#      rejected shader is never left looking up to date.
# SPIR-V is a build output and is never committed (owner rule).
#
# lykoware_compile_shader(<out_var>
#     SOURCE <file.slang> ENTRY <entry point> STAGE <vertex|fragment|compute|...>
#     NAME <output base name>)
# Sets <out_var> to the absolute path of the validated .spv.

function(lykoware_compile_shader out_var)
    cmake_parse_arguments(PARSE_ARGV 1 SHADER "" "SOURCE;ENTRY;STAGE;NAME" "")
    foreach(required IN ITEMS SOURCE ENTRY STAGE NAME)
        if(NOT SHADER_${required})
            message(FATAL_ERROR "lykoware_compile_shader: ${required} is required")
        endif()
    endforeach()
    if(NOT LYKOWARE_SLANGC OR NOT LYKOWARE_SPIRV_OPT OR NOT LYKOWARE_SPIRV_VAL)
        message(FATAL_ERROR "lykoware_compile_shader: call lykoware_add_host_tools() first")
    endif()

    cmake_path(ABSOLUTE_PATH SHADER_SOURCE BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE source)
    set(dir "${CMAKE_BINARY_DIR}/shaders")
    set(raw "${dir}/${SHADER_NAME}.slang.spv")
    set(optimised "${dir}/${SHADER_NAME}.opt.spv")
    set(output "${dir}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}"
        COMMAND "${LYKOWARE_SLANGC}" "${source}"
            -target spirv -profile spirv_1_6 -O0 -warnings-as-errors all
            -entry "${SHADER_ENTRY}" -stage "${SHADER_STAGE}"
            -o "${raw}"
        COMMAND "${LYKOWARE_SPIRV_OPT}" -O --target-env=vulkan1.3 "${raw}" -o "${optimised}"
        COMMAND "${LYKOWARE_SPIRV_VAL}" --target-env vulkan1.3 "${optimised}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${optimised}" "${output}"
        DEPENDS "${source}" "${LYKOWARE_SLANGC}" "${LYKOWARE_SPIRV_OPT}" "${LYKOWARE_SPIRV_VAL}"
        COMMENT "Shader ${SHADER_NAME}: Slang -> SPIR-V -> spirv-opt -> spirv-val"
        VERBATIM)
    set(${out_var} "${output}" PARENT_SCOPE)
endfunction()
