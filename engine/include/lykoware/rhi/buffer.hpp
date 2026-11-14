#pragma once

#include <lykoware/math.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace lykoware::rhi {

// What a Buffer feeds: vertex attributes, or 32-bit triangle indices.
enum class BufferUsage {
    Vertex,
    Index,
};

// Largest buffer the RHI accepts, 256 MiB, well under every device's limit.
inline constexpr std::size_t buffer_max_bytes = std::size_t{256} * 1024 * 1024;

// GPU memory holding mesh data, filled once at creation (ADR 0025).
//
// Memory: the RHI picks memory the CPU can write and the GPU can read
// (device-local where the driver offers it). Staged uploads into
// device-only memory come with asset streaming.
//
// Ownership: create() returns the sole owner. Copy and move are not provided.
// Lifetime: destroy before the Device. Threading: the Device's thread.
//
// Failure: create() returns nullptr and fills error. Empty data, data over
// buffer_max_bytes, or index data that is not a whole number of 32-bit
// indices is InvalidArgument. Driver refusals are DriverError.
class Buffer {
public:
    [[nodiscard]] static std::unique_ptr<Buffer> create(
        const Device& device, BufferUsage usage, std::span<const std::byte> data, RhiError& error);

    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    [[nodiscard]] BufferUsage usage() const;
    [[nodiscard]] std::size_t size_bytes() const;
    // For an index buffer, its largest index (checked against the vertex
    // count before drawing); 0 for a vertex buffer.
    [[nodiscard]] std::uint32_t max_index() const;
    // VkBuffer as an opaque pointer for other RHI code.
    [[nodiscard]] void* native_handle() const;

private:
    struct Impl;
    explicit Buffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

// One indexed mesh to draw: a VertexLayout::PositionNormal vertex buffer,
// a buffer of 32-bit triangle indices, and the bytes the pipeline's push
// constants hold for this mesh.
class Texture;
class ShadowMap;
class GraphicsPipeline;

struct MeshDraw {
    const Buffer* vertices = nullptr;
    const Buffer* indices = nullptr;
    // Indices to draw from the start of the index buffer; 0 means all.
    std::uint32_t index_count = 0;
    std::span<const std::byte> push_constants;
    // Required when the pipeline reads a texture (GraphicsPipelineDesc::texture), and must be
    // null otherwise.
    const Texture* texture = nullptr;
    // The first index drawn, so one index buffer can hold several parts (ADR 0038). With
    // index_count 0, the draw runs from here to the end of the buffer.
    std::uint32_t first_index = 0;
    // Skinned pipelines only: 1..256 finite affine matrices, copied during the draw call.
    // Empty uses the identity palette. Other pipelines require empty. The caller retains no
    // GPU lifetime obligation for this span after draw returns.
    std::span<const math::Mat4> skin_matrices{};
    // Required when the pipeline receives a shadow (GraphicsPipelineDesc::shadow), and must be
    // null otherwise (ADR 0084).
    const ShadowMap* shadow = nullptr;
    // Both required when the pipeline shades a material (GraphicsPipelineDesc::material), and
    // both null otherwise (ADR 0085): set 2 and set 3.
    const Texture* normal_map = nullptr;
    const Texture* metal_roughness = nullptr;
    // Draws this mesh with another pipeline than the pass's (ADR 0086), so opaque and
    // transparent surfaces share one pass. Null: the pass's pipeline. It must target the same
    // colour format and use depth exactly when the pass's pipeline does, and is not depth-only;
    // every other rule above is checked against it.
    const GraphicsPipeline* pipeline = nullptr;
};

}
