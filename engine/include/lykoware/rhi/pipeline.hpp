#pragma once

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace lykoware::rhi {

// Shader stages the RHI can build pipelines from today.
enum class ShaderStage {
    Vertex,
    Fragment,
};

// Colour formats a pipeline can render into. RenderTarget uses Rgba8Unorm;
// a swapchain may use either.
enum class ColorFormat {
    Rgba8Unorm,
    Bgra8Unorm,
};

// SPIR-V entry point every shader module must export. Slang names a
// single-entry-point SPIR-V module "main" (cmake/Shaders.cmake compiles one
// entry point per module, ADR 0014).
inline constexpr const char* shader_entry_point = "main";

// One compiled SPIR-V shader stage (VkShaderModule).
//
// Ownership: load()/load_file() return the sole owner. The SPIR-V words are
// copied into the driver during load; the caller's buffer is not retained.
// Copy and move are not provided.
//
// Lifetime: destroy before the Device. A pipeline built from a Shader does
// not need the Shader afterwards; the Shader may be destroyed once every
// pipeline using it has been created.
//
// Threading: create and destroy on the Device's thread.
//
// Failure: returns nullptr and fills error. Data that is empty, not a whole
// number of 32-bit words, or lacks the SPIR-V magic number is
// InvalidArgument; an unreadable file is InvalidArgument naming the path;
// driver refusals are DriverError. Shaders are validated with spirv-val at
// build time (ADR 0014); load() checks only the header.
class Shader {
public:
    [[nodiscard]] static std::unique_ptr<Shader> load(
        const Device& device, std::span<const std::uint32_t> spirv, ShaderStage stage, RhiError& error);
    [[nodiscard]] static std::unique_ptr<Shader> load_file(
        const Device& device, const std::filesystem::path& path, ShaderStage stage, RhiError& error);

    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

    [[nodiscard]] ShaderStage stage() const;
    // VkShaderModule as an opaque pointer for other RHI code.
    [[nodiscard]] void* native_handle() const;

private:
    struct Impl;
    explicit Shader(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

struct GraphicsPipelineDesc {
    const Shader* vertex = nullptr;
    const Shader* fragment = nullptr;
    // Format of the single colour attachment the pipeline renders into.
    ColorFormat color_format = ColorFormat::Rgba8Unorm;
};

// A graphics pipeline for dynamic rendering: one colour attachment, triangle
// lists, no vertex buffers (vertices come from the vertex shader), no
// culling, no blending, no depth, dynamic viewport and scissor. Later RHI
// changes add vertex input, descriptors, depth and blending.
//
// Ownership: create() returns the sole owner of the pipeline and its
// (empty) pipeline layout. Copy and move are not provided.
//
// Lifetime: destroy before the Device. The Shaders in the desc need only
// live for the create() call.
//
// Threading: create and destroy on the Device's thread.
//
// Failure: returns nullptr and fills error. A missing shader or a shader of
// the wrong stage is InvalidArgument; driver refusals are DriverError.
class GraphicsPipeline {
public:
    [[nodiscard]] static std::unique_ptr<GraphicsPipeline> create(
        const Device& device, const GraphicsPipelineDesc& desc, RhiError& error);

    ~GraphicsPipeline();
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    [[nodiscard]] ColorFormat color_format() const;
    // VkPipeline as an opaque pointer for other RHI code.
    [[nodiscard]] void* native_handle() const;

private:
    struct Impl;
    explicit GraphicsPipeline(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
