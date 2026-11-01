#include <lykoware/image.hpp>

#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace lykoware {
namespace {

constexpr std::uint64_t ppm_header_budget = 64;

bool extents_fit(std::uint32_t width, std::uint32_t height) noexcept
{
    if (width > image_harness_max_extent || height > image_harness_max_extent) {
        return false;
    }
    const std::uint64_t count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 3ULL;
    return count <= static_cast<std::uint64_t>(static_cast<std::size_t>(-1));
}

bool consistent(const Image& image) noexcept
{
    if (!extents_fit(image.width(), image.height())) {
        return false;
    }
    const std::uint64_t count =
        static_cast<std::uint64_t>(image.width()) * static_cast<std::uint64_t>(image.height()) * 3ULL;
    return count == image.pixels().size();
}

bool is_header_space(unsigned char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

void paint_test_pattern(std::uint32_t width, std::uint32_t height, std::uint8_t* pixels)
{
    const bool framed = width >= 4 && height >= 4;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * width + x) * 3;
            const bool border = framed && (x == 0 || y == 0 || x + 1 == width || y + 1 == height);
            if (border) {
                pixel[0] = 255;
                pixel[1] = 255;
                pixel[2] = 255;
                continue;
            }
            pixel[0] = static_cast<std::uint8_t>((x * 13U + y * 7U) & 255U);
            pixel[1] = static_cast<std::uint8_t>((x * 3U + y * 17U) & 255U);
            pixel[2] = static_cast<std::uint8_t>((((x ^ y) * 11U) + 40U) & 255U);
        }
    }
}

int channel_delta(std::uint8_t left, std::uint8_t right) noexcept
{
    const int a = left;
    const int b = right;
    return a > b ? a - b : b - a;
}

}

Image::Image(Image&& other) noexcept
    : width_{other.width_}
    , height_{other.height_}
    , pixels_{std::move(other.pixels_)}
{
    other.width_ = 0;
    other.height_ = 0;
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other) {
        width_ = other.width_;
        height_ = other.height_;
        pixels_ = std::move(other.pixels_);
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

std::uint32_t Image::width() const noexcept
{
    return width_;
}

std::uint32_t Image::height() const noexcept
{
    return height_;
}

std::span<const std::uint8_t> Image::pixels() const noexcept
{
    return pixels_;
}

std::span<std::uint8_t> Image::pixels() noexcept
{
    return pixels_;
}

bool Image::reset(std::uint32_t width, std::uint32_t height)
{
    if (!extents_fit(width, height)) {
        return false;
    }
    const auto count = static_cast<std::size_t>(
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 3ULL);
    std::vector<std::uint8_t> next(count);
    pixels_.swap(next);
    width_ = width;
    height_ = height;
    return true;
}

void Image::clear() noexcept
{
    width_ = 0;
    height_ = 0;
    pixels_.clear();
}

ImageCapture::~ImageCapture() = default;

TestPatternCapture::TestPatternCapture(std::uint32_t width, std::uint32_t height) noexcept
    : width_{width}
    , height_{height}
{
}

bool TestPatternCapture::capture(Image& destination) const
{
    if (width_ == 0 || height_ == 0 || !extents_fit(width_, height_)) {
        destination.clear();
        return false;
    }
    if (!destination.reset(width_, height_)) {
        destination.clear();
        return false;
    }
    paint_test_pattern(width_, height_, destination.pixels().data());
    return true;
}

std::uint32_t TestPatternCapture::width() const noexcept
{
    return width_;
}

std::uint32_t TestPatternCapture::height() const noexcept
{
    return height_;
}

bool write_ppm(const Image& image, const std::filesystem::path& path)
{
    if (!consistent(image)) {
        return false;
    }
    const std::string header = "P6\n" + std::to_string(image.width()) + " "
        + std::to_string(image.height()) + "\n255\n";
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    const auto pixels = image.pixels();
    if (!pixels.empty()) {
        output.write(reinterpret_cast<const char*>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
    }
    output.close();
    if (!output) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return false;
    }
    return true;
}

bool read_ppm(const std::filesystem::path& path, Image& destination)
{
    auto fail = [&destination]() {
        destination.clear();
        return false;
    };

    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    const std::uint64_t max_file = ppm_header_budget
        + static_cast<std::uint64_t>(image_harness_max_extent)
            * static_cast<std::uint64_t>(image_harness_max_extent) * 3ULL;
    if (error || file_size > max_file) {
        return fail();
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return fail();
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return fail();
        }
    }

    if (bytes.size() < 2 || bytes[0] != 'P' || bytes[1] != '6') {
        return fail();
    }

    std::size_t index = 2;
    auto skip_space = [&bytes, &index]() -> bool {
        const std::size_t start = index;
        while (index < bytes.size() && is_header_space(bytes[index])) {
            ++index;
        }
        if (index < bytes.size() && bytes[index] == '#') {
            return false;
        }
        return index > start;
    };
    auto parse_u32 = [&bytes, &index](std::uint32_t& value) -> bool {
        if (index >= bytes.size() || bytes[index] < '0' || bytes[index] > '9') {
            return false;
        }
        std::uint64_t parsed = 0;
        while (index < bytes.size() && bytes[index] >= '0' && bytes[index] <= '9') {
            parsed = parsed * 10ULL + static_cast<unsigned>(bytes[index] - '0');
            if (parsed > 0xFFFFFFFFULL) {
                return false;
            }
            ++index;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    };

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t maxval = 0;
    if (!skip_space() || !parse_u32(width) || !skip_space() || !parse_u32(height)
        || !skip_space() || !parse_u32(maxval) || maxval != 255) {
        return fail();
    }
    if (index >= bytes.size() || !is_header_space(bytes[index])) {
        return fail();
    }
    ++index;
    if (width > image_harness_max_extent || height > image_harness_max_extent) {
        return fail();
    }
    const std::uint64_t count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 3ULL;
    if (static_cast<std::uint64_t>(bytes.size() - index) != count) {
        return fail();
    }
    if (!destination.reset(width, height)) {
        return fail();
    }
    if (count != 0) {
        auto pixels = destination.pixels();
        for (std::uint64_t offset = 0; offset < count; ++offset) {
            pixels[static_cast<std::size_t>(offset)] = bytes[index + static_cast<std::size_t>(offset)];
        }
    }
    return true;
}

ImageCompareResult compare_images(
    const Image& expected,
    const Image& actual,
    std::uint8_t tolerance) noexcept
{
    ImageCompareResult result;
    if (!consistent(expected) || !consistent(actual)
        || expected.width() != actual.width()
        || expected.height() != actual.height()) {
        result.size_mismatch = true;
        return result;
    }

    const auto left = expected.pixels();
    const auto right = actual.pixels();
    const std::uint32_t width = expected.width();
    const std::size_t pixel_count = left.size() / 3;
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        bool mismatched = false;
        for (int channel = 0; channel < 3; ++channel) {
            const std::size_t offset = pixel * 3 + static_cast<std::size_t>(channel);
            if (channel_delta(left[offset], right[offset]) > static_cast<int>(tolerance)) {
                mismatched = true;
            }
        }
        if (!mismatched) {
            continue;
        }
        if (result.mismatched_pixels == 0 && width != 0) {
            result.first_x = static_cast<std::uint32_t>(pixel % width);
            result.first_y = static_cast<std::uint32_t>(pixel / width);
        }
        ++result.mismatched_pixels;
    }
    result.matches = result.mismatched_pixels == 0;
    return result;
}

bool make_diff_image(
    const Image& expected,
    const Image& actual,
    std::uint8_t tolerance,
    Image& diff)
{
    if (!consistent(expected) || !consistent(actual)
        || expected.width() != actual.width()
        || expected.height() != actual.height()) {
        diff.clear();
        return false;
    }
    if (!diff.reset(expected.width(), expected.height())) {
        diff.clear();
        return false;
    }
    const auto left = expected.pixels();
    const auto right = actual.pixels();
    auto out = diff.pixels();
    for (std::size_t index = 0; index < left.size(); ++index) {
        const int delta = channel_delta(left[index], right[index]);
        out[index] = delta <= static_cast<int>(tolerance)
            ? static_cast<std::uint8_t>(0)
            : static_cast<std::uint8_t>(delta);
    }
    return true;
}

}
