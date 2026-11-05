// assets.image: PNG and JPEG decoding through Wuffs (ADR 0036). Every image is encoded here,
// by a minimal PNG writer (uncompressed "stored" zlib blocks) and a minimal baseline JPEG
// writer (blocks of one flat level, which a JPEG decoder reproduces exactly), so the
// expected pixels are known and no image files are committed.

#include <lykoware/image_decode.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using lykoware::RgbaImage;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

using Bytes = std::vector<std::byte>;

void put(Bytes& out, std::initializer_list<unsigned> values)
{
    for (const unsigned v : values) {
        out.push_back(static_cast<std::byte>(v & 0xFFU));
    }
}

void put32be(Bytes& out, std::uint32_t v)
{
    put(out, {v >> 24U, v >> 16U, v >> 8U, v});
}

// --- PNG ------------------------------------------------------------------------------------

std::uint32_t crc32(const Bytes& data, std::size_t from, std::size_t to)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = from; i < to; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return ~crc;
}

void chunk(Bytes& out, const char (&type)[5], const std::vector<std::uint8_t>& data)
{
    put32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>(type[i]));
    }
    for (const std::uint8_t b : data) {
        out.push_back(static_cast<std::byte>(b));
    }
    put32be(out, crc32(out, start, out.size()));
}

// Rows already carry their filter byte. Extra chunks (PLTE, tRNS) go before IDAT.
Bytes png(std::uint32_t width, std::uint32_t height, unsigned depth, unsigned colour, const std::vector<std::uint8_t>& rows,
          const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& extra = {})
{
    Bytes out;
    put(out, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    chunk(out, "IHDR", {static_cast<std::uint8_t>(width >> 24U), static_cast<std::uint8_t>(width >> 16U),
                        static_cast<std::uint8_t>(width >> 8U), static_cast<std::uint8_t>(width),
                        static_cast<std::uint8_t>(height >> 24U), static_cast<std::uint8_t>(height >> 16U),
                        static_cast<std::uint8_t>(height >> 8U), static_cast<std::uint8_t>(height),
                        static_cast<std::uint8_t>(depth), static_cast<std::uint8_t>(colour), 0, 0, 0});
    for (const auto& [type, data] : extra) {
        char name[5] = {};
        std::memcpy(name, type.data(), 4);
        chunk(out, *reinterpret_cast<const char(*)[5]>(name), data);
    }
    // zlib: header, one final stored block, Adler-32 of the raw data.
    std::vector<std::uint8_t> z = {0x78, 0x01, 0x01, static_cast<std::uint8_t>(rows.size() & 0xFFU),
                                   static_cast<std::uint8_t>(rows.size() >> 8U), static_cast<std::uint8_t>(~rows.size() & 0xFFU),
                                   static_cast<std::uint8_t>((~rows.size() >> 8U) & 0xFFU)};
    z.insert(z.end(), rows.begin(), rows.end());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : rows) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    const std::uint32_t adler = (b << 16U) | a;
    z.insert(z.end(), {static_cast<std::uint8_t>(adler >> 24U), static_cast<std::uint8_t>(adler >> 16U),
                       static_cast<std::uint8_t>(adler >> 8U), static_cast<std::uint8_t>(adler)});
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

std::array<int, 4> pixel(const RgbaImage& image, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

void test_png()
{
    std::string error;
    // RGBA, 2 x 2, filter 0 (none): exact values, alpha not premultiplied.
    auto image = lykoware::decode_image(png(2, 2, 8, 6, {0, 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 0, 255, 0, 10, 20, 30, 40}), error);
    expect(image && image->width == 2 && image->height == 2 && image->pixels.size() == 16, "a 2x2 RGBA PNG decodes");
    expect(image && pixel(*image, 0, 0) == std::array<int, 4>{255, 0, 0, 255} && pixel(*image, 1, 0) == std::array<int, 4>{0, 255, 0, 128}
               && pixel(*image, 0, 1) == std::array<int, 4>{0, 0, 255, 0} && pixel(*image, 1, 1) == std::array<int, 4>{10, 20, 30, 40},
           "RGBA pixels come out exactly, rows top to bottom, alpha not premultiplied");

    image = lykoware::decode_image(png(3, 1, 8, 2, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), error);
    expect(image && pixel(*image, 2, 0) == std::array<int, 4>{7, 8, 9, 255}, "RGB becomes opaque RGBA");

    image = lykoware::decode_image(png(2, 1, 8, 0, {0, 0, 200}), error);
    expect(image && pixel(*image, 1, 0) == std::array<int, 4>{200, 200, 200, 255}, "greyscale spreads to R, G and B");

    image = lykoware::decode_image(png(1, 1, 8, 4, {0, 90, 60}), error);
    expect(image && pixel(*image, 0, 0) == std::array<int, 4>{90, 90, 90, 60}, "greyscale with alpha keeps its alpha");

    // Palette with a transparency entry for colour 1.
    image = lykoware::decode_image(png(2, 1, 8, 3, {0, 0, 1}, {{"PLTE", {10, 20, 30, 40, 50, 60}}, {"tRNS", {255, 77}}}), error);
    expect(image && pixel(*image, 0, 0) == std::array<int, 4>{10, 20, 30, 255} && pixel(*image, 1, 0) == std::array<int, 4>{40, 50, 60, 77},
           "a palette image takes its colours and transparency from PLTE and tRNS");

    // 16 bits per channel: the high byte is kept.
    image = lykoware::decode_image(png(1, 1, 16, 2, {0, 0xAB, 0xCD, 0x12, 0x34, 0xFF, 0xFF}), error);
    expect(image && pixel(*image, 0, 0) == std::array<int, 4>{0xAB, 0x12, 0xFF, 255}, "16-bit channels become their high byte");

    // Row filters: Sub (1) on the first row, Up (2) on the second.
    image = lykoware::decode_image(png(2, 2, 8, 2, {1, 10, 20, 30, 5, 5, 5, 2, 1, 1, 1, 2, 2, 2}), error);
    expect(image && pixel(*image, 1, 0) == std::array<int, 4>{15, 25, 35, 255} && pixel(*image, 1, 1) == std::array<int, 4>{17, 27, 37, 255},
           "Sub and Up row filters are undone");
}

// --- JPEG -----------------------------------------------------------------------------------

class BitWriter {
public:
    explicit BitWriter(Bytes& out)
        : out_(out)
    {
    }
    void bits(std::uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i) {
            current_ = static_cast<std::uint8_t>((current_ << 1U) | ((value >> static_cast<unsigned>(i)) & 1U));
            if (++filled_ == 8) {
                flush_byte();
            }
        }
    }
    void finish()
    {
        while (filled_ != 0) {
            bits(1, 1); // pad with ones
        }
    }

private:
    void flush_byte()
    {
        out_.push_back(static_cast<std::byte>(current_));
        if (current_ == 0xFF) {
            out_.push_back(std::byte{0}); // byte stuffing
        }
        current_ = 0;
        filled_ = 0;
    }
    Bytes& out_;
    std::uint8_t current_ = 0;
    int filled_ = 0;
};

// The standard luminance DC table (ITU T.81 annex K): categories 0..11.
constexpr std::array<std::uint8_t, 16> dc_bits = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};

// A baseline JPEG whose 8x8 blocks are each one flat level per component (quantisation 1, no
// subsampling), so decoding gives the levels back exactly. levels[block][component].
Bytes jpeg(std::uint32_t width, std::uint32_t height, int components, const std::vector<std::array<int, 3>>& levels)
{
    Bytes out;
    put(out, {0xFF, 0xD8});
    put(out, {0xFF, 0xDB, 0, 67, 0});
    for (int i = 0; i < 64; ++i) {
        put(out, {1});
    }
    put(out, {0xFF, 0xC0, 0, static_cast<unsigned>(8 + 3 * components), 8, height >> 8U, height, width >> 8U, width,
              static_cast<unsigned>(components)});
    for (int c = 0; c < components; ++c) {
        put(out, {static_cast<unsigned>(c + 1), 0x11, 0});
    }
    // DC table 0: the standard one. AC table 0: one symbol, end-of-block, code "0".
    put(out, {0xFF, 0xC4, 0, 31, 0x00});
    for (const std::uint8_t b : dc_bits) {
        put(out, {b});
    }
    for (unsigned v = 0; v < 12; ++v) {
        put(out, {v});
    }
    put(out, {0xFF, 0xC4, 0, 20, 0x10, 1});
    for (int i = 0; i < 15; ++i) {
        put(out, {0});
    }
    put(out, {0x00});
    put(out, {0xFF, 0xDA, 0, static_cast<unsigned>(6 + 2 * components), static_cast<unsigned>(components)});
    for (int c = 0; c < components; ++c) {
        put(out, {static_cast<unsigned>(c + 1), 0x00});
    }
    put(out, {0, 63, 0});

    // Canonical Huffman codes for the DC categories.
    std::array<std::uint32_t, 12> code{};
    std::array<int, 12> length{};
    std::uint32_t next = 0;
    int symbol = 0;
    for (int len = 1; len <= 16; ++len) {
        for (int i = 0; i < dc_bits[static_cast<std::size_t>(len - 1)]; ++i) {
            code[static_cast<std::size_t>(symbol)] = next++;
            length[static_cast<std::size_t>(symbol)] = len;
            ++symbol;
        }
        next <<= 1U;
    }
    BitWriter writer(out);
    std::array<int, 3> previous{};
    for (const std::array<int, 3>& block : levels) {
        for (int c = 0; c < components; ++c) {
            // A flat block's DC coefficient is 8 x (level - 128).
            const int dc = 8 * (block[static_cast<std::size_t>(c)] - 128);
            const int diff = dc - previous[static_cast<std::size_t>(c)];
            previous[static_cast<std::size_t>(c)] = dc;
            int category = 0;
            for (int magnitude = diff < 0 ? -diff : diff; magnitude != 0; magnitude >>= 1) {
                ++category;
            }
            writer.bits(code[static_cast<std::size_t>(category)], length[static_cast<std::size_t>(category)]);
            if (category != 0) {
                const int value = diff >= 0 ? diff : diff + (1 << category) - 1;
                writer.bits(static_cast<std::uint32_t>(value), category);
            }
            writer.bits(0, 1); // end of block: every AC coefficient is zero
        }
    }
    writer.finish();
    put(out, {0xFF, 0xD9});
    return out;
}

bool near(const std::array<int, 4>& got, std::array<int, 4> want, int tolerance)
{
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(got[i] - want[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

void test_jpeg()
{
    std::string error;
    // Greyscale, two blocks side by side.
    auto image = lykoware::decode_image(jpeg(16, 8, 1, {{50, 0, 0}, {200, 0, 0}}), error);
    expect(image && image->width == 16 && image->height == 8, "a 16x8 greyscale JPEG decodes: " + error);
    expect(image && pixel(*image, 0, 0) == std::array<int, 4>{50, 50, 50, 255} && pixel(*image, 15, 7) == std::array<int, 4>{200, 200, 200, 255},
           "flat greyscale blocks decode to their exact levels");

    // A size that is not a whole number of blocks is cropped.
    image = lykoware::decode_image(jpeg(5, 3, 1, {{77, 0, 0}}), error);
    expect(image && image->width == 5 && image->height == 3 && pixel(*image, 4, 2) == std::array<int, 4>{77, 77, 77, 255},
           "a 5x3 JPEG is cropped from its 8x8 block");

    // Colour: neutral chroma gives exact grey; a coloured block converts from YCbCr.
    image = lykoware::decode_image(jpeg(8, 8, 3, {{120, 128, 128}}), error);
    expect(image && pixel(*image, 3, 3) == std::array<int, 4>{120, 120, 120, 255}, "neutral YCbCr decodes to exact grey");
    image = lykoware::decode_image(jpeg(8, 8, 3, {{150, 100, 200}}), error);
    // R = Y + 1.402 (Cr - 128); G = Y - 0.344 (Cb - 128) - 0.714 (Cr - 128); B = Y + 1.772 (Cb - 128).
    expect(image && near(pixel(*image, 0, 0), {251, 108, 100, 255}, 2), "colour JPEG converts YCbCr to RGB");
}

// --- Refusals -------------------------------------------------------------------------------

void test_refusals()
{
    std::string error;
    auto refused = [&](const Bytes& file, std::string_view what, std::string_view needle = {}) {
        error.clear();
        const bool ok = !lykoware::decode_image(file, error) && !error.empty()
            && (needle.empty() || error.find(needle) != std::string::npos);
        expect(ok, std::string{what} + " (error: " + error + ")");
    };
    refused({}, "empty input is refused");
    Bytes gif;
    put(gif, {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0});
    refused(gif, "a GIF is refused", "not a PNG or JPEG");
    Bytes bmp;
    put(bmp, {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0});
    refused(bmp, "a BMP is refused", "not a PNG or JPEG");

    const Bytes good = png(2, 1, 8, 2, {0, 1, 2, 3, 4, 5, 6});
    Bytes truncated(good.begin(), good.end() - 20);
    refused(truncated, "a truncated PNG is refused, not returned half-decoded");
    Bytes bad_crc = good;
    bad_crc[29] ^= std::byte{1}; // the last byte of IHDR's CRC
    refused(bad_crc, "a PNG chunk with a wrong CRC is refused");
    Bytes bad_adler = good;
    bad_adler[bad_adler.size() - 17] ^= std::byte{1}; // the last byte of the zlib Adler-32
    refused(bad_adler, "PNG data with a wrong Adler-32 is refused");
    refused(png(0, 1, 8, 2, {0}), "a zero-width PNG is refused");
    refused(png(1, 1, 7, 2, {0, 1, 2, 3}), "an invalid bit depth is refused");

    // Over the limits: refused from the header alone, before any pixel memory is allocated.
    refused(png(20000, 1, 8, 2, {}), "a PNG wider than 16384 pixels is refused", "16384");
    refused(png(16384, 4097, 8, 2, {}), "a PNG with more than 2^26 pixels is refused", "67108864");

    Bytes jpeg_truncated = jpeg(16, 8, 1, {{50, 0, 0}, {200, 0, 0}});
    jpeg_truncated.resize(jpeg_truncated.size() - 4);
    refused(jpeg_truncated, "a truncated JPEG is refused");
}

}

int main()
{
    test_png();
    test_jpeg();
    test_refusals();
    if (g_failures != 0) {
        std::cerr << g_failures << " image assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "image decoding passed\n";
    return EXIT_SUCCESS;
}
