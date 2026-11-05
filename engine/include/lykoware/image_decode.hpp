#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// PNG and JPEG decoding (ADR 0036) through the vendored Wuffs library, which upstream builds to
// be memory-safe on untrusted files. No Wuffs type appears in this header.

namespace lykoware {

// 8-bit RGBA, not premultiplied, rows from top to bottom, tightly packed (width * 4 bytes per
// row). Colour values are as the file stores them: no gamma or colour-profile conversion is
// applied (glTF base colour textures are sRGB and are used as such).
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

// The largest image decoded: 16,384 pixels on a side, and 67,108,864 pixels in all (an
// 8192 x 8192 image, 256 MiB as RGBA).
inline constexpr std::uint32_t image_max_dimension = 16384;
inline constexpr std::uint64_t image_max_pixels = std::uint64_t{1} << 26U;

// Decodes a PNG or JPEG file, told apart by its first bytes. Greyscale, palette, 16-bit and
// alpha images all become 8-bit RGBA; images without alpha are opaque. An animated PNG gives its
// first frame. Fails (nullopt, error filled) for any other format, for malformed or truncated
// data, and for images over the limits, which are refused before their pixels are allocated.
//
// Threading: no shared state; safe to call from several threads at once.
[[nodiscard]] std::optional<RgbaImage> decode_image(std::span<const std::byte> file, std::string& error);

}
