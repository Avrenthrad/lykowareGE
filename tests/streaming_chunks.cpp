#include <lykoware/streaming_chunks.hpp>

#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
lykoware::streaming::ChunkDefinition chunk(int x)
{
    using namespace lykoware;
    streaming::ChunkDefinition result;
    result.region = {x, 0};
    result.pose = {static_cast<double>(x) * 256, 0, 0};
    result.coarse_asset = "coarse";
    result.fine_asset = "fine";
    result.data.assets = {{"coarse", {std::byte{1}}, {}}, {"fine", {std::byte{2}, std::byte{3}}, {"coarse"}}};
    physics::BodyDesc body;
    body.motion = physics::MotionType::Static;
    result.collision = {body};
    return result;
}
}
int main()
{
    try {
        using namespace lykoware;
        using namespace lykoware::streaming;
        std::set<std::uint32_t> live;
        std::uint32_t sequence = 0;
        int fail_after = -1;
        ChunkCollision backend{
            [&](const physics::BodyDesc&, std::string& error) -> std::optional<physics::BodyId> {
                if (fail_after == 0) { error = "deliberate backend failure"; return {}; }
                if (fail_after > 0) { --fail_after; }
                live.insert(++sequence);
                return physics::BodyId{sequence, 1, 1};
            },
            [&](physics::BodyId body) { live.erase(body.index); }
        };
        std::string error;
        {
            ChunkStreamer stream(backend, 3, 1);
            require(stream.stream({255.5, 0, 1}, chunk(0), error), "initial chunk");
            auto view = stream.query({0, 0});
            require(view && view->id == "r:0:0" && view->pose.x == 0 && view->coarse[0] == std::byte{1}
                    && view->fine.size() == 2 && live.size() == 1, "renderer payload query");
            require(stream.stream({255.9, 0, 1}, chunk(0), error) && sequence == 1, "resident reuse");
            require(stream.stream({256, 0, 1}, chunk(1), error) && !stream.query({0, 0})
                    && stream.query({1, 0})->pose.x == 256 && live.size() == 1 && !live.contains(1), "boundary evicts payload and collision");
            require(stream.pin({1, 0}, true) && !stream.stream({512, 0, 1}, chunk(2), error)
                    && live.size() == 1 && stream.bytes() == 3, "pin pressure preserves collision");
            require(stream.pin({1, 0}, false), "unpin");
            auto incoming = chunk(2);
            incoming.collision.push_back(incoming.collision.front());
            fail_after = 1;
            require(!stream.stream({512, 0, 1}, incoming, error) && live.size() == 1 && stream.query({1, 0}), "partial create rolled back");
            fail_after = -1;
            incoming.coarse_asset = "missing";
            require(!stream.stream({512, 0, 1}, incoming, error), "missing render payload refused");
            incoming = chunk(2);
            incoming.collision[0].motion = physics::MotionType::Dynamic;
            require(!stream.stream({512, 0, 1}, incoming, error), "dynamic streamed collision refused");
            require(!stream.stream({512, 0, 1}, chunk(0), error), "wrong authored region refused");
        }
        require(live.empty(), "destruction releases every collision");
        std::cout << "chunk transactions passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
