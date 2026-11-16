#include <lykoware/streaming_chunks.hpp>

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
        using namespace streaming;
        std::string error;
        auto world = physics::World::create({}, error);
        require(world != nullptr, "physics world");
        const auto fixture = [](int x) {
            ChunkDefinition chunk;
            chunk.region = {x, 0};
            chunk.pose = {static_cast<double>(x) * 256, 0, 0};
            chunk.coarse_asset = "terrain/coarse";
            chunk.fine_asset = "terrain/fine";
            chunk.data.assets = {{"terrain/coarse", {std::byte{1}}, {}}, {"terrain/fine", {std::byte{2}}, {}}};
            physics::BodyDesc ground;
            ground.motion = physics::MotionType::Static;
            ground.shape = physics::BoxShape{{10, 0.5F, 10}};
            ground.position = {chunk.pose.x + 5, -0.5, 5};
            chunk.collision.push_back(ground);
            return chunk;
        };
        {
            ChunkStreamer stream(physics_chunk_collision(*world), 2, 1);
            require(stream.stream({255, 0, 1}, fixture(0), error), "first region installed");
            require(world->cast_ray({5, 10, 5}, {0, -20, 0}).has_value(), "resident ground hit");
            require(stream.stream({256, 0, 1}, fixture(1), error), "cross region boundary");
            require(!stream.query({0, 0}) && stream.query({1, 0}) && world->body_count() == 1, "residency follows LRU");
            require(!world->cast_ray({5, 10, 5}, {0, -20, 0}), "unloaded collision cannot be hit");
            require(world->cast_ray({261, 10, 5}, {0, -20, 0}).has_value(), "new region collision hit");
            auto invalid = fixture(2);
            invalid.collision.push_back(invalid.collision.front());
            invalid.collision.back().shape = physics::BoxShape{{-1, 1, 1}};
            require(!stream.stream({512, 0, 1}, invalid, error) && world->body_count() == 1
                    && stream.query({1, 0}), "real physics partial failure rolled back");
        }
        require(world->body_count() == 0, "streamer destroys physics resources");
        std::cout << "chunk physics integration passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
