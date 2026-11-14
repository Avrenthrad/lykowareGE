#pragma once

#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <cstdint>
#include <memory>
#include <span>

// A high-dynamic-range colour target (ADR 0087): a 16-bit float RGBA image with a depth buffer,
// drawn like a RenderTarget and then sampled by a post pass (MeshDraw::source).

namespace lykoware::rhi {

class Device;

// Ownership: create() returns the sole owner of the colour and depth images, the sampler and the
// descriptor set. Copy and move are not provided. Lifetime: destroy it before the Device, and
// after any draw that names it has finished (render targets and the swapchain wait for their
// frames). Threading: the Device's thread.
//
// Values above 1 are kept, which is the point: a tone-mapping pass decides how they become
// 8-bit colour. A new target is cleared to black, so a post pass drawn before any render() reads 0.
class HdrTarget {
public:
    // Extents outside 1..render_target_max_extent, or past the device's 2D limit, are
    // InvalidArgument. A device that cannot render to and sample the format is NoSuitableDevice.
    // Driver refusals are DriverError.
    [[nodiscard]] static std::unique_ptr<HdrTarget> create(const Device& device, std::uint32_t width,
                                                           std::uint32_t height, RhiError& error);

    ~HdrTarget();
    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;
    HdrTarget(HdrTarget&&) = delete;
    HdrTarget& operator=(HdrTarget&&) = delete;

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;

    // One pass, as RenderTarget::draw_meshes: clear colour to `clear` and depth to far, draw
    // `meshes` with `pipeline` (or a draw's own), then wait; the colour is then ready to sample.
    // The pipeline must target ColorFormat::Rgba16Float. Refused with InvalidArgument, the target
    // unchanged, on the draw rules of RenderTarget::draw_meshes, or for a skinned pipeline (a
    // later slice).
    [[nodiscard]] bool render(const ClearColor& clear, const GraphicsPipeline& pipeline,
                              std::span<const MeshDraw> meshes, RhiError& error);

    // Set 0 with the texture layout (one combined image sampler, linear, clamped).
    [[nodiscard]] void* native_descriptor_set() const;

private:
    struct Impl;
    explicit HdrTarget(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
