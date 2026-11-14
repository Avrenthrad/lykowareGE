#pragma once

#include <lykoware/math.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// A directional light's shadow map (ADR 0084): a square depth image drawn from the light, and
// the uniform block a receiving pipeline reads to find each surface in it.

namespace lykoware::rhi {

class Device;

inline constexpr std::uint32_t shadow_map_min_size = 16;
inline constexpr std::uint32_t shadow_map_max_size = 8192;

// What a receiving shader reads besides the depth image: set 1, binding 1, std140.
struct ShadowBlock {
    // The receiver's camera clip space to the light's clip space, by column (math::Mat4).
    math::Mat4 clip_to_light{};
    // x: depth bias in light depth units (0..1); y: one texel in the map's texture
    // coordinates; z and w unused.
    math::Vec4 params{};
};
static_assert(sizeof(ShadowBlock) == 80);

// Ownership: create() returns the sole owner of the depth image, its sampler, the uniform block
// and the descriptor set. Copy and move are not provided. Lifetime: destroy it before the Device,
// and after any draw that names it has finished (render targets and the swapchain wait for
// their frames). Threading: the Device's thread.
//
// Reversed depth, as everywhere in the RHI: the map clears to 0 (nothing casts), and a caster
// nearer the light is larger. A new map is cleared, so a receiver drawn before any render() is
// fully lit.
class ShadowMap {
public:
    // A size outside shadow_map_min_size..shadow_map_max_size, or past the device's 2D limit,
    // is InvalidArgument. Driver refusals are DriverError.
    [[nodiscard]] static std::unique_ptr<ShadowMap> create(const Device& device, std::uint32_t size,
                                                           RhiError& error);

    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&&) = delete;
    ShadowMap& operator=(ShadowMap&&) = delete;

    [[nodiscard]] std::uint32_t size() const;

    // Clears the map, draws `casters` with a depth-only pipeline (GraphicsPipelineDesc::depth_only)
    // and stores `block` for receivers. Waits for the GPU before it returns. False and
    // InvalidArgument, with the map unchanged, for a pipeline that is not depth-only, a mesh
    // the pipeline refuses, a block that is not finite, or a bias outside 0..1.
    [[nodiscard]] bool render(const GraphicsPipeline& casters, std::span<const MeshDraw> meshes,
                              const ShadowBlock& block, RhiError& error);

    // Casters drawn by the last successful render().
    [[nodiscard]] std::size_t last_caster_count() const;

    [[nodiscard]] void* native_descriptor_set() const;

private:
    struct Impl;
    explicit ShadowMap(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
