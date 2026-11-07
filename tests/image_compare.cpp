// Compares a captured PPM with a golden PPM using the image harness (ADR 0007, ADR 0043).
//
// Usage: lykoware_image_compare EXPECTED.ppm ACTUAL.ppm TOLERANCE MAX_MISMATCHED_PIXELS DIFF.ppm
// A pixel matches when each channel differs by at most TOLERANCE. The images pass when they have
// the same size and at most MAX_MISMATCHED_PIXELS pixels do not match. On failure the diff image
// is written. Exit codes: 0 pass, 1 fail, 2 usage or unreadable input.

#include <lykoware/image.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

bool parse(std::string_view text, std::uint64_t maximum, std::uint64_t& value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value <= maximum;
}

}

int main(int argc, char* argv[])
{
    std::uint64_t tolerance = 0;
    std::uint64_t allowed = 0;
    if (argc != 6 || !parse(argv[3], 255, tolerance) || !parse(argv[4], UINT64_MAX, allowed)) {
        std::cerr << "usage: lykoware_image_compare EXPECTED.ppm ACTUAL.ppm TOLERANCE MAX_MISMATCHED_PIXELS DIFF.ppm\n";
        return 2;
    }
    lykoware::Image expected;
    lykoware::Image actual;
    if (!lykoware::read_ppm(argv[1], expected) || !lykoware::read_ppm(argv[2], actual)) {
        std::cerr << "cannot read " << argv[1] << " or " << argv[2] << '\n';
        return 2;
    }
    const auto result = lykoware::compare_images(expected, actual, static_cast<std::uint8_t>(tolerance));
    if (result.size_mismatch) {
        std::cerr << "FAIL: sizes differ: expected " << expected.width() << 'x' << expected.height() << ", got "
                  << actual.width() << 'x' << actual.height() << '\n';
        return 1;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(expected.width()) * expected.height();
    std::cout << result.mismatched_pixels << " of " << pixels << " pixels differ by more than " << tolerance
              << " (at most " << allowed << " allowed)\n";
    if (result.mismatched_pixels <= allowed) {
        return 0;
    }
    lykoware::Image diff;
    if (lykoware::make_diff_image(expected, actual, static_cast<std::uint8_t>(tolerance), diff)
        && lykoware::write_ppm(diff, argv[5])) {
        std::cerr << "wrote the diff to " << argv[5] << '\n';
    }
    std::cerr << "FAIL: first mismatch at (" << result.first_x << ", " << result.first_y << ")\n";
    return 1;
}
