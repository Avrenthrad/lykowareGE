#include <lykoware/streaming_chunks.hpp>
#include <lykoware/streaming_edits.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
lykoware::streaming::ChunkDefinition authored(int x)
{
    using namespace lykoware::streaming;
    return {{x, 0}, {static_cast<double>(x) * 256, 0, 0},
            {{{"terrain", {std::byte{1}}, {}}}}, "terrain", "terrain", {}};
}
}
int main()
{
    try {
        using namespace lykoware;
        using namespace lykoware::streaming;
        std::string error;
        RegionEdits edits(3, 2);
        ChunkStreamer stream({[](const physics::BodyDesc&, std::string&) {
            return std::optional{physics::BodyId{1, 1, 1}};
        }, [](physics::BodyId) {}}, 3, 1);
        require(stream.stream({-256, 0, 0}, authored(-1), error)
                && stream.query({-1, 0})->fine[0] == std::byte{1}, "initial authored region");
        require(edits.record({-1, 0}, "terrain", {std::byte{9}}, error), "record edit");
        require(stream.stream({0, 0, 0}, authored(0), error)
                && !stream.query({-1, 0}), "evict edited region before reload");
        const auto load = [&](int x) {
            auto chunk = authored(x);
            require(edits.apply(chunk.region, chunk.data, error), "apply journal before admission");
            const auto point = chunk.pose;
            require(stream.stream(point, std::move(chunk), error), "stream journalled chunk");
        };
        for (int repeat = 0; repeat < 3; ++repeat) {
            load(-1);
            require(stream.query({-1, 0})->fine[0] == std::byte{9}, "edited bytes survive reload");
            load(0);
            require(!stream.query({-1, 0}) && stream.query({0, 0})->fine[0] == std::byte{1},
                    "eviction and unedited authored reload");
        }
        require(!edits.record({-1, 0}, "terrain", std::vector<std::byte>(4), error)
                && !error.empty() && edits.bytes() == 1, "oversize replacement preserves edit");
        require(edits.record({-1, 0}, "terrain", std::vector<std::byte>(3), error)
                && edits.bytes() == 3 && edits.size() == 1, "replacement credits prior bytes");
        require(!edits.record({0, 0}, "", {}, error), "empty ID refused");
        require(edits.record({-1, 0}, "missing", {}, error), "empty payload counts as entry");
        require(!edits.record({0, 0}, "terrain", {}, error) && edits.size() == 2, "entry limit");
        auto stale = authored(-1).data;
        require(!edits.apply({-1, 0}, stale, error) && stale.assets[0].bytes[0] == std::byte{1},
                "missing ID refuses all edits atomically");
        edits.clear();
        require(edits.size() == 0 && edits.bytes() == 0, "clear accounting");
        require(edits.record({-1, 0}, "terrain", {}, error), "empty replacement");
        auto duplicate = authored(-1).data;
        duplicate.assets.push_back(duplicate.assets.front());
        require(!edits.apply({-1, 0}, duplicate, error)
                && duplicate.assets[0].bytes[0] == std::byte{1}, "ambiguous ID refused atomically");
        auto empty = authored(-1).data;
        require(edits.apply({-1, 0}, empty, error) && empty.assets[0].bytes.empty(), "empty replacement applied");
        bool rejected = false;
        try { RegionEdits invalid(0, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "explicit positive limits");
        std::cout << "unloaded region edits passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
