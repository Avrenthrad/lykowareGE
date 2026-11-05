#pragma once

#include <lykoware/image.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace lykoware::rhi {

// Linear colour channels, 0..1. Values are clamped by the conversion to
// 8-bit UNORM; exact bytes come from k/255 values.
struct ClearColor {
    float r = 0.F;
    float g = 0.F;
    float b = 0.F;
    float a = 1.F;
};

// Largest width or height accepted by RenderTarget. It matches the image
// harness allocation guard so every target can be read back into an Image.
inline constexpr std::uint32_t render_target_max_extent = image_harness_max_extent;

struct RenderTargetDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Also create a 32-bit float depth buffer, for pipelines with depth.
    bool depth = false;
};


// An offscreen colour target: an 8-bit RGBA UNORM image the RHI renders
// into with dynamic rendering, plus a host-visible buffer for read-back.
// This is the headless path; a window swapchain uses the same rendering.
//
// Ownership: create() returns the sole owner. The target owns its image, its
// read-back buffer and their GPU memory. Copy and move are not provided.
//
// Lifetime: destroy every RenderTarget before the Device it came from.
// The target stays bound to that Device.
//
// Threading: one thread, the same one that uses the Device. clear() and
// read_back() submit work and wait for it before returning, so nothing is
// in flight between calls.
//
// Failure: create() returns nullptr and fills error. Extents outside
// 1..render_target_max_extent, or beyond the device limit, are
// RhiFailure::InvalidArgument. read_back() before any clear() is
// InvalidArgument because the contents are undefined. Driver refusals are
// DriverError. Nothing throws for those; std::bad_alloc can propagate.
class RenderTarget {
public:
    [[nodiscard]] static std::unique_ptr<RenderTarget> create(
        const Device& device, const RenderTargetDesc& desc, RhiError& error);

    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;

    // Renders one dynamic-rendering pass whose only work is a load-op clear
    // of the whole target, then waits for it.
    [[nodiscard]] bool clear(const ClearColor& color, RhiError& error);

    // Format pipelines must target to draw here: always Rgba8Unorm.
    [[nodiscard]] ColorFormat color_format() const;

    // One dynamic-rendering pass: load-op clear to `clear`, then draw
    // `vertex_count` vertices with `pipeline` (no vertex buffers), then wait.
    // A pipeline built for another colour format is InvalidArgument and
    // nothing is submitted.
    [[nodiscard]] bool draw(const ClearColor& clear, const GraphicsPipeline& pipeline, std::uint32_t vertex_count,
                            RhiError& error);

    [[nodiscard]] bool has_depth() const;

    // One dynamic-rendering pass: clear colour to `clear` and depth to far,
    // then draw each mesh in order with `pipeline`, then wait. Refused with
    // InvalidArgument, nothing submitted, when: the pipeline's colour format
    // differs; it has no mesh vertex layout (PositionNormal or
    // PositionNormalUv); it uses depth and this target has none; a mesh lacks
    // a vertex or index buffer of the right usage; the indices it draws
    // (first_index, index_count) are none, not a multiple of 3 or past the
    // end of the buffer; an index reaches past
    // the last vertex; its push constants are not exactly the pipeline's
    // push_constant_bytes; or it names no texture for a pipeline that reads
    // one, or names one for a pipeline that does not.
    [[nodiscard]] bool draw_meshes(const ClearColor& clear, const GraphicsPipeline& pipeline,
                                   std::span<const MeshDraw> meshes, RhiError& error);

    // Copies the current contents into `image` as tightly packed RGB8 with
    // the top-left origin of lykoware::Image. Alpha is dropped. On failure
    // `image` is left empty.
    [[nodiscard]] bool read_back(Image& image, RhiError& error) const;

private:
    struct Impl;
    explicit RenderTarget(std::unique_ptr<Impl> impl);
    // Shared by clear() and draw(); pipeline is a VkPipeline or null.
    [[nodiscard]] bool render(const ClearColor& color, void* pipeline, std::uint32_t vertex_count, RhiError& error);

    std::unique_ptr<Impl> impl_;
};

// Adapts a RenderTarget to the image harness (ADR 0007): capture() reads the
// target back. The adapter refers to the target and must not outlive it.
// Failure details are logged; capture() returns false with an empty image.
class RenderTargetCapture final : public ImageCapture {
public:
    explicit RenderTargetCapture(const RenderTarget& target) noexcept;

    [[nodiscard]] bool capture(Image& destination) const override;

private:
    const RenderTarget& target_;
};

}
