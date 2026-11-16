#include <lykoware/save.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
}
int main()
{
    try {
        using namespace lykoware;
        const save::Limits limits{4096, 16, 8, 32};
        streaming::RegionEdits edits(16, 8);
        std::string error;
        require(edits.record({65, -65}, "terrain", {std::byte{9}, std::byte{0}}, error), "terrain edit");
        require(edits.record({-65, 65}, "empty", {}, error), "empty edit");
        const auto encoded = save::encode(edits, limits, error);
        require(encoded && error.empty() && *encoded == *save::encode(edits, limits, error), "stable encoded bytes");
        const auto decoded = save::decode(*encoded, limits, error);
        require(decoded && error.empty() && decoded->world_schema == save::world_version
                && decoded->edits == edits.snapshot(), "current version round-trip");
        streaming::RegionEdits restored(16, 8);
        for (const auto& edit : decoded->edits) {
            require(restored.record(edit.region, edit.asset, edit.bytes, error), "restore journal");
        }
        streaming::RegionData region{{{"terrain", {std::byte{1}}, {}}}};
        require(restored.apply({65, -65}, region, error) && region.assets[0].bytes[0] == std::byte{9}, "saved edit applied");
        auto future = *encoded;
        future[4] = static_cast<std::byte>(save::format_version + 1);
        require(!save::decode(future, limits, error) && !error.empty(), "future version refused");
        future = *encoded;
        future[8] = std::byte{2};
        require(!save::decode(future, limits, error), "future world schema refused");
        auto trailing = *encoded;
        trailing.push_back(std::byte{0});
        require(!save::decode(trailing, limits, error), "trailing bytes refused");
        for (std::size_t n = 0; n < encoded->size(); ++n) {
            require(!save::decode(std::span(*encoded).first(n), limits, error), "every truncation refused");
        }
        for (const save::Limits small : {save::Limits{15, 16, 8, 32}, {4096, 1, 8, 32},
                                        {4096, 16, 1, 32}, {4096, 16, 8, 2}, {0, 16, 8, 32}}) {
            require(!save::encode(edits, small, error) && !save::decode(*encoded, small, error), "explicit bounds enforced");
        }
        streaming::RegionEdits empty(16, 8);
        const auto empty_save = save::encode(empty, limits, error);
        require(empty_save && save::decode(*empty_save, limits, error)->edits.empty(), "empty world round-trip");
        std::cout << "versioned save passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
