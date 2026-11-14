#pragma once

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

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

// Reads a compiled .spv file into 32-bit words. Needs no Device, so a
// program can report a missing or damaged shader before it touches the GPU.
// Fails with InvalidArgument naming the path when the file cannot be opened,
// is empty or not a whole number of words, or lacks the SPIR-V magic number;
// `words` is then left empty.
[[nodiscard]] bool read_spirv_file(const std::filesystem::path& path, std::vector<std::uint32_t>& words,
                                   RhiError& error);

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

// How vertices reach the vertex shader.
enum class VertexLayout {
    // No vertex buffer: the vertex shader makes its own vertices.
    None,
    // One vertex buffer of 24-byte vertices: float3 position at location 0,
    // float3 normal at location 1 (ADR 0025).
    PositionNormal,
    // One vertex buffer of 32-byte vertices: float3 position at location 0,
    // float3 normal at location 1, float2 texture coordinate at location 2 (ADR 0037).
    PositionNormalUv,
    // PositionNormalUv plus uint16x4 joints at location 3 and float4 weights at location 4.
    // Weights are normalised/nonnegative (all zero selects static geometry); joints < 256.
    SkinnedPositionNormalUv,
};

// Size in bytes of one VertexLayout::PositionNormal vertex.
inline constexpr std::uint32_t position_normal_stride = 24;
// Size in bytes of one VertexLayout::PositionNormalUv vertex.
inline constexpr std::uint32_t position_normal_uv_stride = 32;

inline constexpr std::uint32_t skinned_position_normal_uv_stride = 56;
inline constexpr std::size_t skin_palette_max_joints = 256;
inline constexpr std::size_t skin_palette_max_draws = 4096;

// Largest push-constant block, the size every Vulkan device guarantees.
inline constexpr std::uint32_t push_constant_max_bytes = 128;

struct GraphicsPipelineDesc {
    const Shader* vertex = nullptr;
    const Shader* fragment = nullptr;
    // Format of the single colour attachment the pipeline renders into.
    ColorFormat color_format = ColorFormat::Rgba8Unorm;
    VertexLayout vertex_layout = VertexLayout::None;
    // Depth test and write against a 32-bit float depth buffer with reversed
    // depth (math::perspective): nearer is larger, the buffer clears to 0
    // and a fragment passes when its depth is greater. The target must have
    // a depth buffer.
    bool depth = false;
    // Discard triangles facing away from the camera. Front faces wind
    // counter-clockwise as seen by the camera (right-handed, ADR 0024).
    bool cull_back_faces = false;
    // Bytes of push constants the shaders read, 0..push_constant_max_bytes,
    // a multiple of 4; visible to both stages.
    std::uint32_t push_constant_bytes = 0;
    // The fragment shader reads one texture: a combined image sampler at set 0,
    // binding 0 (ADR 0037). Every mesh drawn then names its Texture.
    bool texture = false;
    // A shadow caster (ADR 0084): depth only, drawn into a ShadowMap. No fragment shader and
    // no colour attachment; needs depth, a mesh layout without skinning, and no texture or
    // shadow. color_format is ignored.
    bool depth_only = false;
    // Receives a shadow (ADR 0084): set 1 is a ShadowMap's set (binding 0 its depth, binding 1
    // its uniform block), and every mesh drawn names that ShadowMap. Needs `texture`, and a
    // layout without skinning (the skinned set 1 is the palette).
    bool shadow = false;
};

// A graphics pipeline for dynamic rendering: one colour attachment, triangle
// lists, no blending, dynamic viewport and scissor; vertex input, depth,
// culling, push constants and an optional texture as the desc says. Later
// RHI changes add blending.
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
// the wrong stage, or push_constant_bytes out of range or not a multiple of
// 4, is InvalidArgument; driver refusals are DriverError.
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
    [[nodiscard]] VertexLayout vertex_layout() const;
    [[nodiscard]] bool depth() const;
    [[nodiscard]] std::uint32_t push_constant_bytes() const;
    [[nodiscard]] bool texture() const;
    [[nodiscard]] bool depth_only() const;
    [[nodiscard]] bool shadow() const;
    // VkPipeline as an opaque pointer for other RHI code.
    [[nodiscard]] void* native_handle() const;
    // VkPipelineLayout as an opaque pointer for other RHI code.
    [[nodiscard]] void* native_layout() const;

private:
    struct Impl;
    explicit GraphicsPipeline(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
