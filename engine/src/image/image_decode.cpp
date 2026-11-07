#include <lykoware/image_decode.hpp>

// Declarations only: the implementation is compiled once, in wuffs_impl.cpp. The module list
// comes from the lykoware_wuffs target (cmake/VendorWuffs.cmake).
#include <wuffs-v0.4.c>

#include <cstring>
#include <string>
#include <utility>

namespace lykoware {

namespace {

// Restricts Wuffs to PNG and JPEG, asks for 8-bit non-premultiplied RGBA, and refuses images
// over the pixel limit before their buffer is allocated.
class Callbacks final : public wuffs_aux::DecodeImageCallbacks {
public:
    std::string refusal;

private:
    wuffs_base__image_decoder::unique_ptr SelectDecoder(uint32_t fourcc, wuffs_base__slice_u8 prefix_data,
                                                        bool prefix_closed) override
    {
        if (fourcc != WUFFS_BASE__FOURCC__PNG && fourcc != WUFFS_BASE__FOURCC__JPEG) {
            return wuffs_base__image_decoder::unique_ptr(nullptr);
        }
        wuffs_base__image_decoder::unique_ptr decoder = DecodeImageCallbacks::SelectDecoder(fourcc, prefix_data, prefix_closed);
        // Wuffs' helper skips PNG's CRC-32 and zlib Adler-32 checks for speed. A damaged asset
        // should fail clearly instead of loading wrong pixels, and the checks are cheap.
        if (decoder && fourcc == WUFFS_BASE__FOURCC__PNG) {
            decoder->set_quirk(WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, 0);
        }
        return decoder;
    }

    wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config& /*image_config*/) override
    {
        return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL);
    }

    AllocPixbufResult AllocPixbuf(const wuffs_base__image_config& image_config, bool allow_uninitialized_memory) override
    {
        const std::uint64_t width = image_config.pixcfg.width();
        const std::uint64_t height = image_config.pixcfg.height();
        if (width == 0 || height == 0) {
            refusal = "the image has no pixels";
            return AllocPixbufResult(std::string{refusal});
        }
        if (width * height > image_max_pixels) {
            refusal = "the image has more than " + std::to_string(image_max_pixels) + " pixels";
            return AllocPixbufResult(std::string{refusal});
        }
        return DecodeImageCallbacks::AllocPixbuf(image_config, allow_uninitialized_memory);
    }
};

}

std::optional<RgbaImage> decode_image(std::span<const std::byte> file, std::string& error)
{
    Callbacks callbacks;
    wuffs_aux::sync_io::MemoryInput input(reinterpret_cast<const std::uint8_t*>(file.data()), file.size());
    wuffs_aux::DecodeImageResult result =
        wuffs_aux::DecodeImage(callbacks, input, wuffs_aux::DecodeImageArgQuirks::DefaultValue(),
                               wuffs_aux::DecodeImageArgFlags::DefaultValue(),
                               wuffs_aux::DecodeImageArgPixelBlend(WUFFS_BASE__PIXEL_BLEND__SRC),
                               wuffs_aux::DecodeImageArgBackgroundColor::DefaultValue(),
                               wuffs_aux::DecodeImageArgMaxInclDimension(image_max_dimension));
    // A partly decoded image (a truncated file, say) is refused too.
    if (!result.error_message.empty() || !result.pixbuf.pixcfg.is_valid()) {
        if (!callbacks.refusal.empty()) {
            error = callbacks.refusal;
        } else if (result.error_message == wuffs_aux::DecodeImage_UnsupportedImageFormat) {
            error = "not a PNG or JPEG image";
        } else if (result.error_message == wuffs_aux::DecodeImage_MaxInclDimensionExceeded) {
            error = "the image is more than " + std::to_string(image_max_dimension) + " pixels on a side";
        } else {
            error = "the image cannot be decoded (" + result.error_message + ")";
        }
        return std::nullopt;
    }

    RgbaImage image;
    image.width = result.pixbuf.pixcfg.width();
    image.height = result.pixbuf.pixcfg.height();
    const std::size_t row = static_cast<std::size_t>(image.width) * 4;
    image.pixels.resize(row * image.height);
    const wuffs_base__table_u8 plane = result.pixbuf.plane(0);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        std::memcpy(image.pixels.data() + row * y, plane.ptr + plane.stride * y, row);
    }
    error.clear();
    return image;
}

}
