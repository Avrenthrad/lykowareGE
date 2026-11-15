#include <lykoware/streaming_chunks.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
lykoware::streaming::ChunkDefinition fixture(int x)
{
    using namespace lykoware;
    streaming::ChunkDefinition result;
    result.region = {x, 0};
    result.pose = {static_cast<double>(x) * 256, 0, 0};
    result.coarse_asset = "coarse";
    result.fine_asset = "fine";
    result.data.assets = {{"coarse", std::vector<std::byte>(4, std::byte{1}), {}},
                          {"fine", std::vector<std::byte>(x % 2 == 0 ? 4 : 8, std::byte{2}), {"coarse"}}};
    physics::BodyDesc ground;
    ground.motion = physics::MotionType::Static;
    ground.shape = physics::BoxShape{{1, 0.5F, 1}};
    ground.position = {result.pose.x + 5, -0.5, 5};
    result.collision.push_back(ground);
    return result;
}
}
int main()
{
    try {
        using namespace lykoware;
        using namespace streaming;
        std::string error;
#if defined(LYKOWARE_STREAMING_REAL_PHYSICS)
        auto world = physics::World::create({}, error);
        require(world != nullptr, "real physics world");
        const auto collision = physics_chunk_collision(*world);
        const auto count = [&] { return world->body_count(); };
        const auto hit = [&](int x) {
            return world->cast_ray({static_cast<double>(x) * 256 + 5, 10, 5}, {0, -20, 0}).has_value();
        };
#else
        std::map<std::uint32_t, double> bodies;
        std::uint32_t sequence = 0;
        const ChunkCollision collision{
            [&](const physics::BodyDesc& body, std::string&) {
                bodies.emplace(++sequence, body.position.x);
                return std::optional{physics::BodyId{sequence, 1, 1}};
            },
            [&](physics::BodyId body) { bodies.erase(body.index); }
        };
        const auto count = [&] { return bodies.size(); };
        const auto hit = [&](int x) {
            return std::any_of(bodies.begin(), bodies.end(), [&](const auto& body) {
                return body.second == static_cast<double>(x) * 256 + 5;
            });
        };
#endif
        std::vector<int> visited;
        {
            constexpr std::size_t byte_limit = 12;
            ChunkStreamer stream(collision, byte_limit, 1);
            const auto visit = [&](int x, Position point) {
                require(stream.stream(point, fixture(x), error), "rapid travel admission");
                require(stream.size() == 1 && stream.bytes() == (x % 2 == 0 ? 8U : 12U)
                        && stream.bytes() <= byte_limit && count() == 1, "resident payload and collision bounded per crossing");
                require(stream.query({x, 0}).has_value() && hit(x), "current chunk query and collision");
                if (!visited.empty() && visited.back() != x) {
                    require(!stream.query({visited.back(), 0}) && !hit(visited.back()), "evicted collision absent after travel");
                }
                visited.push_back(x);
            };
            for (int i = 0; i < 128; ++i) {
                const int x = (i * 7919) % 4096 - 2048;
                visit(x, {static_cast<double>(x) * 256, 0, 0});
            }
            for (int i = 0; i < 256; ++i) {
                const int x = i % 2;
                visit(x, {x == 0 ? 255.999 : 256, 0, 0});
            }
            require(stream.pin({1, 0}, true), "pin current region");
            require(!stream.stream({512, 0, 0}, fixture(2), error)
                    && stream.bytes() == 12 && hit(1) && !hit(2), "pin pressure refuses without leaking collision");
            require(stream.pin({1, 0}, false), "unpin");
            auto oversized = fixture(2);
            oversized.data.assets[0].bytes.resize(13);
            require(!stream.stream({512, 0, 0}, std::move(oversized), error)
                    && stream.bytes() == 12 && count() == 1 && hit(1), "oversize admission preserves bounded state");
            visit(-65, {-65 * 256, 0, 0});
            for (const int x : visited) {
                if (x != -65) { require(!hit(x), "no visited evicted region retains collision"); }
            }
            require(stream.bytes() == 12 && stream.size() == 1, "post-traversal resident payload returns under limit");
        }
        require(count() == 0, "destruction releases last collision");
        std::cout << "rapid travel and pressure passed: " << visited.size()
                  << " crossings; resident payload <= 12 bytes, one region/body\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
