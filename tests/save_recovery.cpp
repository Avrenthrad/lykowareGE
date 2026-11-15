#include <lykoware/save.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
}
int main(int argc, char** argv)
{
    try {
        using namespace lykoware;
        require(argc == 2, "fixture directory required");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        const auto target = directory / "world.save";
        const auto pending = directory / "world.save.pending";
        const save::Limits limits{4096, 16, 8, 32};
        // Frozen v2 fixture: r:-1:2, asset "a", payload 0x09, no checksum.
        const std::vector<std::byte> legacy{
            std::byte{'L'}, std::byte{'Y'}, std::byte{'K'}, std::byte{'W'},
            std::byte{2}, {}, {}, {}, std::byte{1}, {}, {}, {}, std::byte{1}, {}, {}, {},
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
            std::byte{2}, {}, {}, {}, std::byte{1}, {}, {}, {}, std::byte{'a'},
            std::byte{1}, {}, {}, {}, {}, {}, {}, {}, std::byte{9}};
        std::string error;
        const auto migrated = save::decode(legacy, limits, error);
        require(migrated && migrated->edits.size() == 1
                && migrated->edits[0].region == streaming::Region{-1, 2}
                && migrated->edits[0].asset == "a"
                && migrated->edits[0].bytes == std::vector{std::byte{9}}, "frozen older version migrates");
        require(save::replace_file(target, legacy, limits, error), "migrate into last-good file");
        const auto good = streaming::load_region_bytes(target, limits.file_bytes);
        require(good.status == streaming::LoadStatus::ready
                && good.bytes[4] == static_cast<std::byte>(save::format_version)
                && save::read_file(target, limits, error)->edits == migrated->edits, "migration emits current format");
        require(good.bytes[good.bytes.size() - 4] == std::byte{0x49}
                && good.bytes[good.bytes.size() - 3] == std::byte{0x09}
                && good.bytes[good.bytes.size() - 2] == std::byte{0x62}
                && good.bytes.back() == std::byte{0x04}, "independent CRC32 golden value");
        for (std::size_t n = 0; n < good.bytes.size(); ++n) {
            require(!save::replace_file(target, std::span(good.bytes).first(n), limits, error), "truncated replacement refused");
            require(streaming::load_region_bytes(target, limits.file_bytes).bytes == good.bytes
                    && !std::filesystem::exists(pending), "last-good save untouched");
        }
        for (std::size_t i = 0; i < good.bytes.size(); ++i) {
            auto corrupt = good.bytes;
            corrupt[i] ^= std::byte{1};
            require(!save::replace_file(target, corrupt, limits, error), "single-byte corruption refused");
        }
        require(streaming::load_region_bytes(target, limits.file_bytes).bytes == good.bytes, "corruption preserves file");
        { std::ofstream sentinel(pending); sentinel << "interrupted save"; }
        require(!save::replace_file(target, good.bytes, limits, error)
                && std::filesystem::file_size(pending) == 16, "pre-existing pending file preserved");
        std::filesystem::remove(pending);
        streaming::RegionEdits next(16, 8);
        require(next.record({-1, 2}, "a", {std::byte{7}}, error), "replacement edit");
        const auto update = save::encode(next, limits, error);
        require(update && save::replace_file(target, *update, limits, error)
                && save::read_file(target, limits, error)->edits == next.snapshot(), "replace existing target");
        const auto blocked = directory / "blocked";
        std::filesystem::create_directories(blocked);
        require(!save::replace_file(blocked, *update, limits, error)
                && std::filesystem::is_directory(blocked)
                && !std::filesystem::exists(directory / "blocked.pending"), "rename failure cleans pending only");
        const auto broken = directory / "truncated.save";
        { std::ofstream output(broken, std::ios::binary); output << "LYKW"; }
        require(!save::read_file(broken, limits, error) && !error.empty(), "truncated file read refused");
        require(!save::read_file(directory / "missing.save", limits, error), "missing file refused");
        std::cout << "save migration and recovery passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
