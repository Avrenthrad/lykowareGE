#pragma once

#include <lykoware/rhi/instance.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace lykoware::rhi {

class Device;

enum class TextureFormat : std::uint32_t {
    // Colour (base colour): stored as sRGB, read by shaders as linear values.
    Rgba8Srgb,
    // Data (masks, later normal maps): read exactly as stored.
    Rgba8Unorm,
};

// What happens to texture coordinates outside 0..1.
enum class TextureWrap : std::uint32_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

enum class TextureFilter : std::uint32_t {
    // Bilinear within a level and between mip levels.
    Linear,
    // The nearest texel and the nearest mip level.
    Nearest,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    // A full chain of smaller levels down to 1x1, made on the CPU by averaging 2x2 texels.
    // For Rgba8Srgb the average is taken in linear light, so detail fades to the right
    // brightness instead of darkening. The result is the same on every GPU.
    bool mipmaps = true;
    TextureWrap wrap = TextureWrap::Repeat;
    TextureFilter filter = TextureFilter::Linear;
};

// A sampled 2D texture with its sampler, ready to bind to a pipeline with
// GraphicsPipelineDesc::texture (ADR 0037).
//
// Ownership: create() returns the sole owner of the image, its memory, view, sampler and
// descriptor set. Copy and move are not provided.
//
// Lifetime: destroy before the Device, and not while a frame that uses it may still be in
// flight (Swapchain's destructor and Device::wait_idle wait for the GPU; RenderTarget draws
// finish before they return).
//
// Threading: create and destroy on the Device's thread.
//
// Failure: returns nullptr and fills error. A zero size, a side larger than the device allows,
// or `rgba` not exactly width * height * 4 bytes is InvalidArgument; driver refusals are
// DriverError.
class Texture {
public:
    // `rgba`: 8-bit RGBA texels, rows from top to bottom, tightly packed. Texture coordinate
    // (0, 0) is the top-left texel.
    [[nodiscard]] static std::unique_ptr<Texture> create(const Device& device, const TextureDesc& desc,
                                                         std::span<const std::byte> rgba, RhiError& error);

    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;
    [[nodiscard]] std::uint32_t mip_levels() const;
    [[nodiscard]] TextureFormat format() const;
    // VkDescriptorSet (set 0, binding 0: the combined image sampler) for other RHI code.
    [[nodiscard]] void* native_descriptor_set() const;

private:
    struct Impl;
    explicit Texture(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

// The number of levels in a full mip chain for a width x height texture.
[[nodiscard]] std::uint32_t full_mip_levels(std::uint32_t width, std::uint32_t height) noexcept;

}
