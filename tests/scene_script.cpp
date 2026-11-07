// scene.script (ADR 0042): a Lua script drives a scene through recorded commands.
//
// Usage: lykoware_scene_script_test SCENE.json COMMANDS.json SCRIPT.lua
// The script and the command file are the falling-crates samples, which must give the same scene.

#include <lykoware/scene.hpp>
#include <lykoware/scene_script.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace scene = lykoware::scene;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

std::string read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

// Hashes after every tick of a run driven by commands.
std::vector<std::uint64_t> replay(const scene::Description& description, const std::vector<scene::Command>& commands,
                                  std::uint64_t ticks)
{
    std::string error;
    auto world = scene::Scene::create(description, error);
    std::vector<std::uint64_t> hashes;
    if (!world) {
        expect(false, "the scene is created: " + error);
        return hashes;
    }
    std::size_t next = 0;
    while (world->tick() < ticks) {
        while (next < commands.size() && commands[next].tick == world->tick()) {
            expect(world->dispatch(commands[next++], error), "a recorded command replays: " + error);
        }
        expect(world->advance_tick(error), "the scene advances");
        hashes.push_back(world->state_hash());
    }
    expect(next == commands.size(), "every command is replayed");
    return hashes;
}

// Hashes after every tick of a run driven by a script, and the commands it recorded.
std::vector<std::uint64_t> scripted(const scene::Description& description, std::string_view source, std::uint64_t ticks,
                                    std::vector<scene::Command>* recorded = nullptr)
{
    std::string error;
    auto world = scene::Scene::create(description, error);
    std::vector<std::uint64_t> hashes;
    auto driver = world ? scene::ScriptDriver::create(*world, source, "script.lua", {}, error) : nullptr;
    if (!driver) {
        expect(false, "the script starts: " + error);
        return hashes;
    }
    while (world->tick() < ticks) {
        expect(driver->run_tick(error), "on_tick runs: " + error);
        expect(world->advance_tick(error), "the scene advances");
        hashes.push_back(world->state_hash());
    }
    if (recorded != nullptr) {
        *recorded = driver->commands();
    }
    return hashes;
}

void test_script_matches_command_file(const scene::Description& description,
                                      const std::vector<scene::Command>& file_commands, const std::string& source)
{
    std::vector<scene::Command> recorded;
    const auto by_script = scripted(description, source, 120, &recorded);
    const auto by_file = replay(description, file_commands, 120);
    expect(!by_script.empty() && by_script == by_file, "the script gives the command file's scene at every tick");
    expect(scene::encode_commands(recorded) == scene::encode_commands(file_commands),
           "the script records exactly the command file's commands");
    expect(replay(description, recorded, 120) == by_script, "replaying the recording gives the same scene");
    expect(scripted(description, source, 120) == by_script, "a second run of the script is identical");
}

void test_reading_and_reacting(const scene::Description& description)
{
    // Bounce: whenever green has fallen below 1 m, send it back up; record where it was.
    const std::string source = R"lua(
bounces = 0
function on_tick(tick)
  local x, y, z = position("green")
  assert(x == 0 and z == 0, "green falls straight down")
  if y < 1 then
    set_velocity("green", {0, 6, 0})
    bounces = bounces + 1
  end
  assert(position("nobody") == nil, "an unknown id has no position")
end
)lua";
    std::vector<scene::Command> recorded;
    const auto hashes = scripted(description, source, 240, &recorded);
    expect(hashes.size() == 240, "the reacting script runs 240 ticks");
    expect(recorded.size() >= 2, "green bounces more than once: " + std::to_string(recorded.size()));
    bool rising = true;
    for (std::size_t i = 0; i < recorded.size(); ++i) {
        rising = rising && recorded[i].sequence == i + 1 && std::holds_alternative<scene::SetVelocity>(recorded[i].action)
            && (i == 0 || recorded[i].tick > recorded[i - 1].tick);
    }
    expect(rising, "commands are numbered 1, 2, 3... in tick order");
    expect(replay(description, recorded, 240) == hashes, "a reacting script replays from its recording");
}

void test_errors(const scene::Description& description)
{
    std::string error;
    auto world = scene::Scene::create(description, error);
    if (!world) {
        return;
    }
    const auto before = world->state_hash();
    auto driver = scene::ScriptDriver::create(*world, R"lua(
function on_tick(tick)
  if tick == 0 then
    local ok, message = pcall(destroy, "nobody")
    assert(not ok and message:find("does not exist"), "an unknown id is refused: " .. tostring(message))
    ok, message = pcall(spawn, {id = "red"})
    assert(not ok and message:find("already used"), "a used id is refused: " .. tostring(message))
    ok, message = pcall(spawn, {id = "x", motion = "floating"})
    assert(not ok and message:find("motion"), "an unknown motion is refused")
    ok, message = pcall(set_velocity, "red", "fast")
    assert(not ok and message:find("velocity must be a table"), "a velocity must be a table")
    ok, message = pcall(set_velocity, "ground", {1, 0, 0})
    assert(not ok and message:find("dynamic"), "a static body has no velocity")
    ok, message = pcall(spawn, "box")
    assert(not ok and message:find("spawn takes a table"), "spawn needs a table")
  elseif tick == 1 then
    spawn{id = "late", position = {5, 5, 5}}
    error("stop here")
  end
end
)lua",
                                              "errors.lua", {}, error);
    expect(driver != nullptr, "the error script starts: " + error);
    if (!driver) {
        return;
    }
    expect(driver->run_tick(error), "refused commands are catchable: " + error);
    expect(driver->commands().empty() && world->state_hash() == before, "refused commands change nothing");
    expect(world->advance_tick(error), "the scene advances");
    expect(!driver->run_tick(error) && contains(error, "errors.lua:") && contains(error, "stop here"),
           "a failing on_tick reports its line: " + error);
    expect(driver->commands().size() == 1 && world->position("late").has_value(),
           "a command issued before the failure stays applied and recorded");

    const std::string where = [&] {
        std::string message;
        auto other = scene::Scene::create(description, message);
        auto failing = other ? scene::ScriptDriver::create(*other, "\n\ndestroy('nobody')", "top.lua", {}, message) : nullptr;
        return failing ? std::string{} : message;
    }();
    expect(contains(where, "top.lua:3:") && contains(where, "does not exist"),
           "a refused command is reported at the script's line, not inside the library: " + where);

    auto endless_world = scene::Scene::create(description, error);
    lykoware::script::ScriptConfig short_count;
    short_count.instruction_limit = 50'000;
    auto endless = endless_world
        ? scene::ScriptDriver::create(*endless_world, "function on_tick() while true do end end", "endless.lua",
                                      short_count, error)
        : nullptr;
    expect(endless && !endless->run_tick(error) && contains(error, "instruction limit"),
           "an endless on_tick is stopped: " + error);
    auto silent_world = scene::Scene::create(description, error);
    auto silent = silent_world ? scene::ScriptDriver::create(*silent_world, "x = 1", "silent.lua", {}, error) : nullptr;
    expect(silent && silent->run_tick(error), "a script without on_tick is fine");
    auto gone_world = scene::Scene::create(description, error);
    auto gone = gone_world ? scene::ScriptDriver::create(*gone_world, R"lua(
assert(__scene_spawn == nil and __scene_destroy == nil and __scene_set_velocity == nil and __scene_position == nil,
       "the raw host functions are hidden")
assert(position("red") ~= nil)
destroy("red")
assert(position("red") == nil, "a destroyed entity has no position")
)lua",
                                                             "gone.lua", {}, error)
                           : nullptr;
    expect(gone != nullptr, "a destroyed entity has no position, and the host functions are hidden: " + error);
    auto bad_world = scene::Scene::create(description, error);
    expect(bad_world && !scene::ScriptDriver::create(*bad_world, "this is not lua", "bad.lua", {}, error)
               && contains(error, "bad.lua:1:"),
           "a syntax error is reported at create: " + error);
}

}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "usage: lykoware_scene_script_test SCENE.json COMMANDS.json SCRIPT.lua\n";
        return EXIT_FAILURE;
    }
    std::string error;
    scene::Description description;
    std::vector<scene::Command> commands;
    if (!scene::decode(read(argv[1]), description, error) || !scene::decode_commands(read(argv[2]), commands, error)) {
        std::cerr << "FAIL: the samples decode: " << error << '\n';
        return EXIT_FAILURE;
    }
    test_script_matches_command_file(description, commands, read(argv[3]));
    test_reading_and_reacting(description);
    test_errors(description);
    if (g_failures != 0) {
        std::cerr << g_failures << " scene script assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "scene script passed\n";
    return EXIT_SUCCESS;
}
