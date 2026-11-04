#include <lykoware/scene.hpp>
#include <lykoware/clock.hpp>
#include <lykoware/transform.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace scene = lykoware::scene;
void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}
std::string read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "fixture cannot be read");
    return {std::istreambuf_iterator<char>(file), {}};
}

std::vector<std::uint64_t> replay(const scene::Description& description, const std::vector<scene::Command>& commands,
                                  int rate, bool sample, bool irregular)
{
    std::string error;
    auto world = scene::Scene::create(description, error);
    require(static_cast<bool>(world), "scene operation failed");
    auto clock = lykoware::FixedStep::create({}, error);
    std::vector<std::uint64_t> hashes;
    std::size_t next = 0;
    std::int64_t elapsed = 0;
    for (std::int64_t frame = 1; world->tick() < 180; ++frame) {
        const auto end = frame * 1'000'000'000 / rate;
        const auto duration = irregular ? (frame % 3 == 0 ? 40'000'000 : 7'000'000) : end - elapsed;
        elapsed = end;
        const auto steps = clock->advance(duration);
        require(steps.dropped_ns == 0, "replay fixture must not drop simulation time");
        for (std::uint32_t i = 0; i < steps.ticks && world->tick() < 180; ++i) {
            require(world->tick() == steps.first_tick + i, "clock and scene ticks agree");
            while (next < commands.size() && commands[next].tick == world->tick()) {
                require(world->dispatch(commands[next++], error), "scene operation failed");
            }
            require(world->advance_tick(error), "scene operation failed");
            hashes.push_back(world->state_hash());
        }
        if (sample) {
            const auto before = world->state_hash();
            const auto display = world->presentation(steps.alpha);
            require(display.size() == world->entities().size(), "snapshot covers every active entity");
            require(before == world->state_hash(), "presentation sampling must not change simulation");
        }
    }
    require(next == commands.size(), "all recorded commands were executed");
    return hashes;
}

void malformed_and_ordering(const scene::Description& description, const std::vector<scene::Command>& commands)
{
    std::string error;
    const std::string canonical = scene::encode(description);
    scene::Description output = description;
    for (const std::string& text : {std::string("{}"), std::string("[]"),
             std::string("{\"version\":1,\"version\":1}"),
             canonical.substr(0, canonical.size()/2),
             std::string(16U*1024U*1024U + 1, ' ')}) {
        require(!scene::decode(text, output, error) && scene::encode(output) == canonical,
                "invalid scenes preserve output");
    }
    auto bad = description;
    bad.entities.push_back(bad.entities.front());
    try {
        static_cast<void>(scene::encode(bad));
        require(false, "duplicate authored IDs must fail");
    } catch (const std::invalid_argument&) {
    }
    bad = description;
    bad.entities[0].position.x = std::numeric_limits<double>::quiet_NaN();
    require(!scene::Scene::create(bad, error), "nonfinite in-memory scene is refused");
    const auto encoded_commands = scene::encode_commands(commands);
    std::vector<scene::Command> decoded;
    require(scene::decode_commands(encoded_commands, decoded, error)
                && scene::encode_commands(decoded) == encoded_commands, "canonical command round trip");
    auto duplicate = commands;
    duplicate.push_back(commands.front());
    try {
        static_cast<void>(scene::encode_commands(duplicate));
        require(false, "duplicate command sequence must fail");
    } catch (const std::invalid_argument&) {
    }
    require(!scene::decode_commands("{\"format\":\"lykoware.commands\",\"version\":2,\"commands\":[]}", decoded, error),
            "unknown command versions fail");
    auto world = scene::Scene::create(description, error);
    const auto before = world->state_hash();
    require(!world->dispatch({1, 1, scene::Destroy{"red"}}, error) && world->state_hash() == before,
            "future-tick commands cannot run early");
    require(!world->dispatch({0, 1, scene::Destroy{"missing"}}, error) && world->state_hash() == before,
            "missing IDs fail without changing cursor or simulation");
    require(world->dispatch({0, 1, scene::Destroy{"red"}}, error), "same sequence is accepted after failed dispatch");
    const auto destroyed = world->state_hash();
    require(!world->dispatch({0, 1, scene::Destroy{"green"}}, error) && world->state_hash() == destroyed,
            "duplicate in-tick sequence fails without mutation");
    scene::Box reused;
    reused.id = "red";
    require(!world->dispatch({0, 2, reused}, error), "destroyed authored ID cannot alias a new entity");
    require(world->dispatch({0, 2, scene::Destroy{"green"}}, error), "failed spawn preserves ordering cursor");
}
}

int main(int argc, char** argv)
{
    require(argc == 3, "expected scene and commands paths");
    std::string error;
    scene::Description description;
    std::vector<scene::Command> commands;
    require(scene::decode(read(argv[1]), description, error), "scene operation failed");
    require(scene::decode_commands(read(argv[2]), commands, error), "scene operation failed");
    const auto canonical = scene::encode(description);
    scene::Description copy;
    require(scene::decode(canonical, copy, error) && scene::encode(copy) == canonical, "canonical scene round trip");
    malformed_and_ordering(description, commands);
    const auto reference = replay(description, commands, 60, false, false);
    for (int rate : {30, 60, 144}) {
        require(replay(description, commands, rate, true, false) == reference, "every tick matches at different frame rates");
    }
    require(replay(description, commands, 60, true, true) == reference, "irregular frames have identical simulation");
    auto distant = description;
    for (auto& entity : distant.entities) {
        entity.position.x += 64000;
        entity.position.z -= 64000;
    }
    distant.camera.eye.x += 64000;
    distant.camera.eye.z -= 64000;
    distant.camera.target.x += 64000;
    distant.camera.target.z -= 64000;
    auto distant_commands = commands;
    for (auto& command : distant_commands) {
        if (auto* box = std::get_if<scene::Box>(&command.action)) {
            box->position.x += 64000;
            box->position.z -= 64000;
        }
    }
    const auto far_reference = replay(distant, distant_commands, 60, false, false);
    require(replay(distant, distant_commands, 144, true, false) == far_reference,
            "far-origin replay is also independent of presentation/frame rate");
    std::cout << "scene replay passed: 180 per-tick hashes match at 30/60/144 Hz and irregular frames\n";
}
