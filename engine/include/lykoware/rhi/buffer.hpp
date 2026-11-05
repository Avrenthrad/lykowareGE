#pragma once

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

struct MeshDraw {
    const Buffer* vertices = nullptr;
    const Buffer* indices = nullptr;
    // Indices to draw from the start of the index buffer; 0 means all.
    std::uint32_t index_count = 0;
    std::span<const std::byte> push_constants;
    // Required when the pipeline reads a texture (GraphicsPipelineDesc::texture), and must be
    // null otherwise.
    const Texture* texture = nullptr;
};

}
