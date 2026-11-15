#include <lykoware/streaming_systems.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { throw std::runtime_error(message); }
}
std::vector<int> replay()
{
    using namespace lykoware;
    using namespace streaming;
    ecs::World world;
    RegionSystems systems({{0, 0}, 1, 512, 4}, world);
#if defined(LYKOWARE_STREAMING_REAL_PHYSICS)
    std::string physics_error;
    auto physics_world = physics::World::create({}, physics_error);
    require(physics_world != nullptr, "real physics world");
    const auto collision = physics_chunk_collision(*physics_world);
    const auto body_count = [&] { return static_cast<int>(physics_world->body_count()); };
    int previous_x = -1;
#else
    int bodies = 0;
    std::uint32_t next_body = 0;
    ChunkCollision collision{
        [&](const physics::BodyDesc&, std::string&) { ++bodies; return std::optional{physics::BodyId{++next_body, 1, 1}}; },
        [&](physics::BodyId) { --bodies; }
    };
    const auto body_count = [&] { return bodies; };
#endif
    std::vector<int> observations;
    std::string error;
    {
        ChunkStreamer streamer(collision, 2, 1, systems.lifecycle());
        std::weak_ptr<const audio::Sound> previous_bank;
        ecs::Entity previous_entity;
        for (int x : {0, 1, 0, 1}) {
            RegionContent content;
            const double offset = static_cast<double>(x) * 256;
            content.solids = {{{offset + 3, 0}, {offset + 4, 4}}};
            const std::vector<float> samples{0.25F, -0.25F};
            content.bank = audio::Sound::create(samples, 1, audio::output_rate, error);
            content.entities = {{offset + 1, 0, 1}};
            const auto bank = std::weak_ptr<const audio::Sound>(content.bank);
            require(systems.stage({x, 0}, std::move(content), error), "stage subsystem content");
            ChunkDefinition chunk;
            chunk.region = {x, 0};
            chunk.pose = {offset, 0, 0};
            chunk.coarse_asset = "c";
            chunk.fine_asset = "f";
            chunk.data.assets = {{"c", {std::byte{1}}, {}}, {"f", {std::byte{2}}, {}}};
            physics::BodyDesc ground;
            ground.motion = physics::MotionType::Static;
            ground.position = {offset + 5, -0.5, 1.5};
            ground.shape = physics::BoxShape{{1, 0.5F, 1}};
            chunk.collision = {ground};
            require(streamer.stream({offset, 0, 1}, std::move(chunk), error), "stream subsystem region");
            require(previous_bank.expired() && !world.alive(previous_entity), "eviction releases bank and previous entity");
            require(!bank.expired() && systems.bank({x, 0}) && world.size() == 1 && body_count() == 1, "resident subsystem resources");
            const auto route = systems.navigation().query({1.5, 1.5}, {6.5, 1.5}, 0);
            require(route.status == (x == 0 ? navigation::Status::unreachable : navigation::Status::reached),
                    "evicted navigation no longer blocks query");
            require(!systems.stage({x, 0}, {}, error), "active content cannot be replaced");
            world.each<StreamedEntity>([&](ecs::Entity entity, StreamedEntity& value) {
                previous_entity = entity;
                require(value.region == Region{x, 0} && value.authored.x == offset + 1, "authored entity position");
                require(!systems.activate({9, 0}, error), "locked ECS activation refused");
            });
            observations.push_back(static_cast<int>(route.status));
            observations.push_back(static_cast<int>(world.size()));
            observations.push_back(body_count());
#if defined(LYKOWARE_STREAMING_REAL_PHYSICS)
            require(physics_world->cast_ray({offset + 5, 10, 1.5}, {0, -20, 0}).has_value(), "new collision hit");
            if (previous_x >= 0) {
                require(!physics_world->cast_ray({static_cast<double>(previous_x) * 256 + 5, 10, 1.5}, {0, -20, 0}),
                        "evicted collision cannot be hit during replay");
            }
            previous_x = x;
#endif
            previous_bank = bank;
        }
        require(!systems.activate({99, 0}, error), "missing staged content refused");
        RegionContent invalid;
        invalid.entities = {{std::numeric_limits<double>::infinity(), 0, 0}};
        require(!systems.stage({99, 0}, invalid, error), "invalid entity position refused");
        invalid.entities.clear();
        invalid.solids = {{{5, 5}, {1, 1}}};
        require(systems.stage({99, 0}, invalid, error) && !systems.activate({99, 0}, error)
                && world.size() == 1 && body_count() == 1, "invalid nav activation atomic");
        require(systems.stage({2, 0}, invalid, error), "stage invalid incoming region");
        ChunkDefinition refused;
        refused.region = {2, 0};
        refused.coarse_asset = "c";
        refused.fine_asset = "f";
        refused.data.assets = {{"c", {std::byte{1}}, {}}, {"f", {std::byte{2}}, {}}};
        physics::BodyDesc incoming_body;
        incoming_body.motion = physics::MotionType::Static;
        refused.collision = {incoming_body};
        require(!streamer.stream({512, 0, 1}, std::move(refused), error)
                && world.size() == 1 && body_count() == 1 && streamer.query({1, 0}),
                "service activation failure rolls back collision and preserves old residency");
    }
    require(world.size() == 0 && body_count() == 0 && !systems.bank({1, 0}), "streamer destruction unloads all subsystems");
    return observations;
}
}
int main()
{
    try {
        require(replay() == replay(), "boundary replay matches second run");
        std::cout << "region systems replay passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
