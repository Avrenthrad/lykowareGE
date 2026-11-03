#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace lykoware {

// GPU-free image harness. This is not a renderer, not a window capture, and
// not a screenshot test. Real screenshot tests wait for a renderer.
// `lyko screenshot` stays an unsupported-renderer error.
//
// A later Vulkan offscreen capture can implement ImageCapture and feed the
// same comparison. ADR 0002 accepts that RHI; no graphics backend is linked
// here. PPM output is a diagnostic fixture, not an authored project format.
// ADR 0004 accepts canonical JSON, TOML config, and glTF 2.0 meshes.

// Allocation guard for one harness image. This is not a display mode, a
// device limit, or a renderer capability.
inline constexpr std::uint32_t image_harness_max_extent = 4096;

// 8-bit RGB, tightly packed, row-major, no padding and no alpha.
// Origin is the top-left pixel. X increases to the right. Y increases
// downward. Channel order is R, G, B. Comparison is raw byte distance,
// not a color space.
//
// Ownership: an Image owns its pixel buffer. Copy copies the pixels. Move
// transfers the buffer and leaves the source empty (width 0, height 0, no
// pixels). Callers that pass Image& keep ownership. No function in this
// header retains an Image, a pixel pointer, or a span after it returns.
// pixels() is a view of the owned buffer. It is invalidated by reset, clear,
// move, or destruction.
//
// Lifetime: the buffer lives until the Image is destroyed, moved-from, or
// replaced by reset or clear. A moved-from Image is empty and may be reset
// or destroyed. The destructor releases the buffer and does not write a file.
//
// Threading: one Image is used from one thread. There is no internal lock.
// Distinct images may be read on different threads. Do not read an image
// while another thread mutates it. No function here starts a thread.
//
// Failure: reset returns false when either extent exceeds
// image_harness_max_extent, or when width * height * 3 does not fit in
// size_t. On false, the image is unchanged. reset may throw std::bad_alloc;
// if it does, the image is unchanged. clear does not throw. Empty dimensions
// (including 0x0) are a valid buffer with no pixels.
class Image {
public:
    Image() = default;
    Image(const Image&) = default;
    Image& operator=(const Image&) = default;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    ~Image() = default;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept;
    [[nodiscard]] std::span<std::uint8_t> pixels() noexcept;

    [[nodiscard]] bool reset(std::uint32_t width, std::uint32_t height);
    void clear() noexcept;

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

// Fills one caller-owned Image. Implementations are harness sources.
// TestPatternCapture is a software pattern, not a rendered frame.
// rhi::RenderTargetCapture (lykoware/rhi/render_target.hpp) reads back a
// Vulkan offscreen render target.
//
// Ownership: the caller owns `destination` before and after capture().
// The implementation must not retain the Image or its pixel span.
//
// Lifetime: capture() is synchronous and finishes before it returns.
//
// Threading: call capture() on one image from one thread. No lock is taken.
// No background thread is started.
//
// Failure: false means no image was produced. `destination` is then empty.
// Validation failures do not throw. std::bad_alloc from a buffer allocation
// propagates, and destination is left unchanged in that case.
class ImageCapture {
public:
    virtual ~ImageCapture();

    ImageCapture(const ImageCapture&) = delete;
    ImageCapture& operator=(const ImageCapture&) = delete;
    ImageCapture(ImageCapture&&) = delete;
    ImageCapture& operator=(ImageCapture&&) = delete;

    [[nodiscard]] virtual bool capture(Image& destination) const = 0;

protected:
    ImageCapture() = default;
};

// Deterministic software image. Not a scene, camera, or screenshot.
// Pixel (x, y) uses the top-left origin described on Image.
// When both extents are at least 4, the outer one-pixel border is white
// (255, 255, 255). Every other pixel, and every pixel of a smaller image, is:
//   R = (x * 13 + y * 7) mod 256
//   G = (x * 3 + y * 17) mod 256
//   B = ((x xor y) * 11 + 40) mod 256
// The same request always produces the same bytes. There is no clock and no
// seed.
//
// Failure: false, and an empty destination, when either extent is 0 or
// greater than image_harness_max_extent. A successful capture replaces any
// previous contents of destination.
class TestPatternCapture final : public ImageCapture {
public:
    TestPatternCapture(std::uint32_t width, std::uint32_t height) noexcept;

    [[nodiscard]] bool capture(Image& destination) const override;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    std::uint32_t width_;
    std::uint32_t height_;
};

// Hand-written Netpbm P6. Not an authored project format.
// Canonical bytes from write_ppm, including on Windows:
//   P6\n
//   <width> <height>\n
//   255\n
//   width * height * 3 raw RGB bytes, row-major, no padding, no trailing byte.
// The writer emits no comments and no carriage returns.
//
// read_ppm accepts one or more spaces, tabs, CR, or LF between the magic
// number and the three decimal tokens. It requires exactly one of those
// whitespace bytes after the maxval token, then the raw payload. It rejects
// comments, any maxval other than 255, extents above image_harness_max_extent,
// a payload that is short or long, and a header larger than the small fixed
// budget enforced by the file-size check. Token values are decimal without a
// sign.
//
// Ownership: the caller owns the Image and the path. Neither function retains
// them. read_ppm replaces `destination` only on success.
//
// Lifetime: both calls are synchronous and finish before they return.
//
// Threading: one call, one thread, no lock. Do not read or write the same
// Image or the same path from two threads. No thread is started.
//
// Failure: I/O and malformed input return false and do not throw.
// write_ppm returns false when the pixel count does not match the dimensions
// or the file cannot be created or fully written. If the destination cannot
// be opened, an existing file is left untouched. If it is opened, previous
// contents are replaced; a later stream failure removes that destination.
// read_ppm returns false on I/O or parse failure and then leaves `destination`
// empty. A file larger than 64 bytes plus the maximum harness image
// (image_harness_max_extent squared, times 3) is rejected before the pixel
// buffer is allocated. std::bad_alloc from a successful parse's buffer
// allocation propagates, and `destination` is unchanged in that case.
[[nodiscard]] bool write_ppm(const Image& image, const std::filesystem::path& path);
[[nodiscard]] bool read_ppm(const std::filesystem::path& path, Image& destination);

// Result of one harness comparison. first_x and first_y are the top-left-origin
// coordinates of the first mismatched pixel in row-major order. They are
// meaningful only when mismatched_pixels is greater than zero.
struct ImageCompareResult {
    bool matches = false;
    bool size_mismatch = false;
    std::uint64_t mismatched_pixels = 0;
    std::uint32_t first_x = 0;
    std::uint32_t first_y = 0;
};

// Per-pixel comparison. A pixel matches when R, G, and B each differ by at
// most `tolerance` (absolute raw-byte distance). tolerance 0 is exact.
// Different dimensions, or a pixel count that does not match the declared
// dimensions, do not match: size_mismatch is true, matches is false, and
// mismatched_pixels is 0. Two images with the same dimensions and no pixels
// match, including two 0x0 images.
//
// Ownership: no pixel data is copied or retained. Lifetime: the call ends
// before it returns. Threading: read-only on both images; the caller must
// not mutate them during the call. Failure: does not throw and does not
// allocate.
[[nodiscard]] ImageCompareResult compare_images(
    const Image& expected,
    const Image& actual,
    std::uint8_t tolerance) noexcept;

// Diff image for the harness. On equal dimensions, `diff` has those dimensions.
// A channel is 0 when its absolute difference is <= tolerance. Otherwise the
// channel is that absolute difference. A comparison that matches therefore
// produces a black image. The diff is not a rendered frame.
//
// Ownership: the caller owns all three images. `diff` is replaced on success.
// Lifetime: synchronous. Threading: one thread, no lock. Do not mutate the
// inputs during the call.
//
// Failure: returns false and leaves `diff` empty when the dimensions differ
// or either pixel count is inconsistent. Does not throw for that rejection.
// std::bad_alloc from allocating `diff` propagates, and `diff` is unchanged
// in that case. This function does not write a file; use write_ppm.
[[nodiscard]] bool make_diff_image(
    const Image& expected,
    const Image& actual,
    std::uint8_t tolerance,
    Image& diff);

}
