#include <lykoware/image.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path{std::u8string(text.begin(), text.end())};
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(output);
}

std::vector<unsigned char> bytes_from(std::string_view text)
{
    return {text.begin(), text.end()};
}

bool inside_directory(const std::filesystem::path& root, const std::filesystem::path& file)
{
    std::error_code error;
    const auto base = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const auto target = std::filesystem::weakly_canonical(file, error);
    if (error) {
        return false;
    }
    const auto relative = target.lexically_relative(base);
    if (relative.empty()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::vector<std::string> names_in(const std::filesystem::path& directory)
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::uint8_t pixel_channel(const lykoware::Image& image, std::uint32_t x, std::uint32_t y, int channel)
{
    const auto pixels = image.pixels();
    const std::size_t index = (static_cast<std::size_t>(y) * image.width() + x) * 3
        + static_cast<std::size_t>(channel);
    return pixels[index];
}

bool same_pixels(const lykoware::Image& left, const lykoware::Image& right)
{
    return left.width() == right.width()
        && left.height() == right.height()
        && std::equal(left.pixels().begin(), left.pixels().end(), right.pixels().begin(), right.pixels().end());
}

void test_buffer_ownership()
{
    lykoware::Image image;
    expect(image.width() == 0 && image.height() == 0 && image.pixels().empty(),
           "a new harness image is empty");
    expect(image.reset(0, 0), "0x0 reset is a valid empty buffer");
    expect(image.pixels().empty(), "0x0 buffer has no pixels");
    expect(image.reset(2, 2), "reset allocates RGB pixels");
    expect(image.pixels().size() == 12, "2x2 image is 12 bytes");
    expect(image.pixels()[0] == 0, "reset pixels start at zero");
    image.pixels()[0] = 9;
    expect(!image.reset(lykoware::image_harness_max_extent + 1, 1),
           "reset rejects an extent past the harness guard");
    expect(image.width() == 2 && image.height() == 2 && image.pixels()[0] == 9,
           "a rejected reset leaves the previous image");

    lykoware::Image copy = image;
    expect(same_pixels(copy, image), "copy keeps the same pixels");
    lykoware::Image moved = std::move(image);
    expect(moved.width() == 2 && moved.pixels()[0] == 9, "move transfers pixels");
    // The moved-from object is specified to be empty and reusable. These uses
    // are that contract, not an accidental read of a stale buffer.
    // NOLINTBEGIN(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
    expect(image.width() == 0 && image.height() == 0 && image.pixels().empty(),
           "moved-from harness image is empty");
    image = std::move(moved);
    expect(image.width() == 2 && moved.width() == 0, "move assignment empties the source");
    // NOLINTEND(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
    image.clear();
    expect(image.width() == 0 && image.pixels().empty(), "clear releases the buffer");
}

void test_pattern()
{
    lykoware::TestPatternCapture pattern{16, 16};
    lykoware::Image first;
    lykoware::Image second;
    expect(pattern.capture(first), "test pattern capture succeeds");
    expect(pattern.capture(second), "test pattern capture is repeatable");
    expect(same_pixels(first, second), "test pattern bytes are deterministic");
    expect(first.width() == 16 && first.height() == 16, "test pattern size");
    expect(pixel_channel(first, 0, 0, 0) == 255
               && pixel_channel(first, 0, 0, 1) == 255
               && pixel_channel(first, 0, 0, 2) == 255,
           "outer border is white");
    expect(pixel_channel(first, 15, 8, 0) == 255, "right border is white");
    expect(pixel_channel(first, 1, 1, 0) == 20
               && pixel_channel(first, 1, 1, 1) == 20
               && pixel_channel(first, 1, 1, 2) == 40,
           "interior pixel follows the published formula");
    expect(pixel_channel(first, 2, 3, 0) == 47
               && pixel_channel(first, 2, 3, 1) == 57
               && pixel_channel(first, 2, 3, 2) == 51,
           "pixel (2, 3) follows the published formula");

    lykoware::Image small;
    expect(lykoware::TestPatternCapture{3, 3}.capture(small), "small pattern captures");
    expect(pixel_channel(small, 0, 0, 0) == 0 && pixel_channel(small, 0, 0, 2) == 40,
           "images smaller than 4 have no forced border");

    expect(pattern.capture(small), "a later capture replaces the previous image");
    expect(small.width() == 16 && small.height() == 16, "replacement uses the new size");

    lykoware::Image filled;
    expect(filled.reset(2, 2), "prepare an image for a failed capture");
    expect(!lykoware::TestPatternCapture{0, 4}.capture(filled), "zero extent is rejected");
    expect(filled.width() == 0 && filled.pixels().empty(), "failed capture leaves the image empty");
    expect(!lykoware::TestPatternCapture{lykoware::image_harness_max_extent + 1, 1}.capture(filled),
           "oversize pattern is rejected");
}

void test_ppm_round_trip(const std::filesystem::path& output)
{
    lykoware::Image image;
    expect(image.reset(0, 0), "prepare an empty ppm image");
    const auto empty_path = output / "empty.ppm";
    expect(lykoware::write_ppm(image, empty_path), "write a 0x0 ppm");
    lykoware::Image loaded;
    expect(loaded.reset(1, 1), "destination starts non-empty");
    expect(lykoware::read_ppm(empty_path, loaded), "read a 0x0 ppm");
    expect(loaded.width() == 0 && loaded.height() == 0 && loaded.pixels().empty(), "0x0 round trip");
    expect(read_bytes(empty_path) == bytes_from("P6\n0 0\n255\n"), "canonical empty ppm");

    const std::vector<unsigned char> loose = {'P', '6', '\n', '1', ' ', ' ', '1', '\n', '2', '5', '5', '\n', 9, 8, 7};
    const auto loose_path = output / "loose.ppm";
    expect(write_bytes(loose_path, loose), "write a loose header");
    expect(lykoware::read_ppm(loose_path, loaded), "reader accepts header whitespace");
    expect(loaded.width() == 1 && pixel_channel(loaded, 0, 0, 0) == 9
               && pixel_channel(loaded, 0, 0, 1) == 8
               && pixel_channel(loaded, 0, 0, 2) == 7,
           "loose header keeps the pixel");

    const auto unicode_path = output / utf8_path("harness Ω.ppm");
    lykoware::Image pattern;
    expect(lykoware::TestPatternCapture{4, 4}.capture(pattern), "capture a 4x4 pattern");
    expect(lykoware::write_ppm(pattern, unicode_path), "write ppm to a unicode path");
    lykoware::Image reread;
    expect(lykoware::read_ppm(unicode_path, reread), "read ppm from a unicode path");
    expect(same_pixels(pattern, reread), "unicode path round trip");
    expect(inside_directory(output, unicode_path), "unicode ppm stays in the build output");

    const auto replaced = output / "replaced.ppm";
    expect(lykoware::write_ppm(pattern, replaced), "write a pattern ppm");
    expect(lykoware::write_ppm(loaded, replaced), "ppm write replaces an existing file");
    expect(lykoware::read_ppm(replaced, reread), "read the replacement");
    expect(same_pixels(loaded, reread), "replacement bytes are the new image");
}

void test_ppm_failures(const std::filesystem::path& output)
{
    lykoware::Image image;
    expect(image.reset(1, 1), "prepare a ppm destination");
    image.pixels()[0] = 4;

    const auto bad_magic = output / "bad-magic.ppm";
    expect(write_bytes(bad_magic, bytes_from("P3\n1 1\n255\nxyz")), "write a rejected magic");
    expect(!lykoware::read_ppm(bad_magic, image), "P3 is not the harness format");
    expect(image.width() == 0 && image.pixels().empty(), "a rejected read clears the destination");

    expect(image.reset(1, 1), "restore destination after a clear");
    const auto comment = output / "comment.ppm";
    expect(write_bytes(comment, bytes_from("P6\n# harness\n1 1\n255\nabc")), "write a comment ppm");
    expect(!lykoware::read_ppm(comment, image), "comments are rejected");

    const auto maxval = output / "maxval.ppm";
    expect(write_bytes(maxval, bytes_from("P6\n1 1\n15\nabc")), "write a low maxval");
    expect(!lykoware::read_ppm(maxval, image), "maxval other than 255 is rejected");

    const auto truncated = output / "truncated.ppm";
    expect(write_bytes(truncated, bytes_from("P6\n1 1\n255\nab")), "write a short payload");
    expect(!lykoware::read_ppm(truncated, image), "a short payload is rejected");

    const auto extra = output / "extra.ppm";
    expect(write_bytes(extra, bytes_from("P6\n1 1\n255\nabcd")), "write an extra payload byte");
    expect(!lykoware::read_ppm(extra, image), "an extra payload byte is rejected");

    const auto double_break = output / "double-break.ppm";
    expect(write_bytes(double_break, bytes_from("P6\n1 1\n255\n\nabc")), "write two bytes after maxval");
    expect(!lykoware::read_ppm(double_break, image), "exactly one whitespace byte follows maxval");

    const auto oversize = output / "oversize.ppm";
    expect(write_bytes(oversize, bytes_from("P6\n4097 1\n255\n")), "write an oversize header");
    expect(!lykoware::read_ppm(oversize, image), "extent above the harness guard is rejected");

    expect(!lykoware::read_ppm(output / "missing.ppm", image), "a missing ppm fails");
    expect(!lykoware::read_ppm(output, image), "a directory is not a ppm");

    lykoware::Image pattern;
    expect(lykoware::TestPatternCapture{2, 2}.capture(pattern), "capture for write failures");
    const auto sentinel_dir = output / "not-a-file";
    std::filesystem::create_directory(sentinel_dir);
    const auto sentinel = sentinel_dir / "keep.txt";
    expect(write_bytes(sentinel, bytes_from("keep")), "create a sentinel");
    expect(!lykoware::write_ppm(pattern, sentinel_dir), "a directory path is not a ppm file");
    expect(read_bytes(sentinel) == bytes_from("keep"), "failed open leaves the existing directory");

    const auto missing_parent = output / "missing-parent" / "image.ppm";
    expect(!lykoware::write_ppm(pattern, missing_parent), "missing parent directory fails");
    expect(!std::filesystem::exists(missing_parent), "failed write leaves no ppm");
    expect(lykoware::write_ppm(pattern, output / "recovered.ppm"), "a later write can still succeed");
}

void test_compare_and_diff(const std::filesystem::path& output)
{
    lykoware::Image expected;
    expect(lykoware::TestPatternCapture{8, 6}.capture(expected), "capture the comparison source");
    lykoware::Image actual = expected;
    auto exact = lykoware::compare_images(expected, actual, 0);
    expect(exact.matches && !exact.size_mismatch && exact.mismatched_pixels == 0,
           "identical images match at tolerance 0");

    actual.pixels()[(static_cast<std::size_t>(2) * 8 + 3) * 3 + 1] =
        static_cast<std::uint8_t>(pixel_channel(actual, 3, 2, 1) + 1);
    auto off_by_one = lykoware::compare_images(expected, actual, 0);
    expect(!off_by_one.matches && off_by_one.mismatched_pixels == 1, "tolerance 0 rejects one channel");
    expect(off_by_one.first_x == 3 && off_by_one.first_y == 2, "first mismatch is recorded");
    auto within = lykoware::compare_images(expected, actual, 1);
    expect(within.matches && within.mismatched_pixels == 0, "tolerance 1 accepts a delta of 1");
    auto swapped = lykoware::compare_images(actual, expected, 0);
    expect(!swapped.matches && swapped.first_x == 3 && swapped.first_y == 2,
           "absolute comparison is symmetric");

    actual.pixels()[(static_cast<std::size_t>(2) * 8 + 3) * 3 + 1] =
        static_cast<std::uint8_t>(pixel_channel(expected, 3, 2, 1) + 2);
    expect(!lykoware::compare_images(expected, actual, 1).matches, "tolerance 1 rejects a delta of 2");

    lykoware::Image diff;
    expect(lykoware::make_diff_image(expected, actual, 1, diff), "diff image is produced");
    expect(diff.width() == 8 && diff.height() == 6, "diff image keeps the source size");
    expect(pixel_channel(diff, 3, 2, 1) == 2, "diff stores the absolute channel delta");
    expect(pixel_channel(diff, 3, 2, 0) == 0 && pixel_channel(diff, 0, 0, 0) == 0,
           "channels inside tolerance stay black");
    lykoware::Image swallowed;
    expect(lykoware::make_diff_image(expected, actual, 2, swallowed), "in-tolerance diff is produced");
    expect(swallowed.pixels().end() == std::find_if(swallowed.pixels().begin(), swallowed.pixels().end(),
                                                    [](std::uint8_t value) { return value != 0; }),
           "a passing tolerance produces a black diff");

    lykoware::Image other_size;
    expect(other_size.reset(8, 5), "prepare a different size");
    auto mismatch = lykoware::compare_images(expected, other_size, 0);
    expect(!mismatch.matches && mismatch.size_mismatch && mismatch.mismatched_pixels == 0,
           "different sizes are a size mismatch");
    expect(diff.reset(4, 4), "prepare a diff that must be cleared");
    expect(!lykoware::make_diff_image(expected, other_size, 0, diff), "size mismatch rejects the diff");
    expect(diff.width() == 0 && diff.pixels().empty(), "rejected diff is empty");

    const auto diff_path = output / "api-diff.ppm";
    expect(lykoware::make_diff_image(expected, actual, 0, diff), "rebuild a visible diff");
    expect(lykoware::write_ppm(diff, diff_path), "write the diff ppm to the build output");
    expect(inside_directory(output, diff_path), "diff ppm stays in the build output");
    lykoware::Image stored;
    expect(lykoware::read_ppm(diff_path, stored) && same_pixels(diff, stored), "diff ppm round trip");
}

void test_api(const std::filesystem::path& golden, const std::filesystem::path& output)
{
    const auto before = names_in(golden);
    test_buffer_ownership();
    test_pattern();
    test_ppm_round_trip(output);
    test_ppm_failures(output);
    test_compare_and_diff(output);
    expect(names_in(golden) == before, "api checks do not write into the golden directory");
}

void test_pass(const std::filesystem::path& golden, const std::filesystem::path& output)
{
    const auto before = names_in(golden);
    const auto golden_file = golden / "test-pattern-16x16.ppm";
    lykoware::Image captured;
    lykoware::Image stored;
    expect(lykoware::TestPatternCapture{16, 16}.capture(captured), "capture the golden pattern");
    expect(lykoware::read_ppm(golden_file, stored), "read the committed golden ppm");
    const auto compared = lykoware::compare_images(stored, captured, 0);
    expect(compared.matches && compared.mismatched_pixels == 0,
           "captured pattern matches the golden at tolerance 0");

    lykoware::Image diff;
    expect(lykoware::make_diff_image(stored, captured, 0, diff), "matching images have a diff");
    expect(std::find_if(diff.pixels().begin(), diff.pixels().end(),
                        [](std::uint8_t value) { return value != 0; }) == diff.pixels().end(),
           "golden match diff is black");

    const auto copy_path = output / "roundtrip.ppm";
    const auto diff_path = output / "match-diff.ppm";
    expect(lykoware::write_ppm(captured, copy_path), "write the passing capture to the build output");
    expect(lykoware::write_ppm(diff, diff_path), "write the black diff to the build output");
    expect(read_bytes(copy_path) == read_bytes(golden_file), "writer bytes match the committed golden");
    expect(inside_directory(output, copy_path) && inside_directory(output, diff_path),
           "passing outputs stay in the build directory");
    expect(names_in(golden) == before, "passing comparison does not write into tests/golden");
}

void test_fail(const std::filesystem::path& golden, const std::filesystem::path& output)
{
    const auto before = names_in(golden);
    lykoware::Image expected;
    lykoware::Image actual;
    expect(lykoware::read_ppm(golden / "test-pattern-16x16.ppm", expected), "read the matching golden");
    expect(lykoware::read_ppm(golden / "test-pattern-16x16-mismatch.ppm", actual), "read the mismatch golden");

    const auto rejected = lykoware::compare_images(expected, actual, 0);
    expect(!rejected.matches && !rejected.size_mismatch, "mismatch golden fails an exact comparison");
    expect(rejected.mismatched_pixels == 1, "one committed pixel differs");
    expect(rejected.first_x == 2 && rejected.first_y == 3, "the committed mismatch is pixel (2, 3)");
    expect(pixel_channel(actual, 2, 3, 0) == 1
               && pixel_channel(actual, 2, 3, 1) == 2
               && pixel_channel(actual, 2, 3, 2) == 3,
           "mismatch pixel is RGB (1, 2, 3)");

    expect(!lykoware::compare_images(expected, actual, 54).matches,
           "tolerance 54 still rejects the committed delta");
    expect(lykoware::compare_images(expected, actual, 55).matches,
           "tolerance 55 accepts the committed delta");

    lykoware::Image diff;
    expect(lykoware::make_diff_image(expected, actual, 0, diff), "build the mismatch diff");
    expect(pixel_channel(diff, 2, 3, 0) == 46
               && pixel_channel(diff, 2, 3, 1) == 55
               && pixel_channel(diff, 2, 3, 2) == 48,
           "diff image stores the absolute delta at pixel (2, 3)");
    bool only_that_pixel = true;
    for (std::uint32_t y = 0; y < diff.height(); ++y) {
        for (std::uint32_t x = 0; x < diff.width(); ++x) {
            if (x == 2 && y == 3) {
                continue;
            }
            if (pixel_channel(diff, x, y, 0) != 0
                || pixel_channel(diff, x, y, 1) != 0
                || pixel_channel(diff, x, y, 2) != 0) {
                only_that_pixel = false;
            }
        }
    }
    expect(only_that_pixel, "every other diff pixel is black");

    const auto diff_path = output / "mismatch-diff.ppm";
    expect(lykoware::write_ppm(diff, diff_path), "write the mismatch diff to the build output");
    lykoware::Image stored;
    expect(lykoware::read_ppm(diff_path, stored) && same_pixels(diff, stored), "reload the mismatch diff");
    expect(inside_directory(output, diff_path), "failing diff stays in the build directory");
    expect(names_in(golden) == before, "failing comparison does not write into tests/golden");
}

}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: image-harness api|pass|fail GOLDEN_DIR OUTPUT_DIR\n"
                     "This is the GPU-free image harness, not a renderer screenshot.\n";
        return 2;
    }
    const std::string_view mode{argv[1]};
    const auto golden = utf8_path(argv[2]);
    const auto output = utf8_path(argv[3]);
    const auto golden_normal = std::filesystem::absolute(golden).lexically_normal();
    const auto output_normal = std::filesystem::absolute(output).lexically_normal();
    const auto relative_to_golden = output_normal.lexically_relative(golden_normal);
    bool output_in_golden = !relative_to_golden.empty();
    for (const auto& part : relative_to_golden) {
        if (part == "..") {
            output_in_golden = false;
            break;
        }
    }
    if (output_normal == golden_normal || output_in_golden) {
        std::cerr << "image harness output must stay out of the golden directory\n";
        return 2;
    }
    std::error_code error;
    std::filesystem::remove_all(output, error);
    std::filesystem::create_directories(output);

    if (mode == "api") {
        test_api(golden, output);
    } else if (mode == "pass") {
        test_pass(golden, output);
    } else if (mode == "fail") {
        test_fail(golden, output);
    } else {
        std::cerr << "unknown image harness mode\n";
        return 2;
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " image harness check(s) failed. Not a renderer screenshot.\n";
        return 1;
    }
    std::cout << "image harness " << mode << " checks passed. Not a renderer screenshot.\n";
    return 0;
}
