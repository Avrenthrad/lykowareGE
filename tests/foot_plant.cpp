#include "player_replay.hpp"
#include "support/stance_fixture.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <lykoware/foot_plant.hpp>
#include <lykoware/player.hpp>
#include <new>
#include <numbers>
namespace {
thread_local bool tracking = false;
thread_local std::size_t allocations = 0;
} // namespace
void* operator new(std::size_t n)
{
    if (tracking) {
        ++allocations;
    }
    if (auto* p = std::malloc(n ? n : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
int main(int argc, char** argv)
{
    namespace a = lykoware::animation;
    namespace p = lykoware::physics;
    namespace m = lykoware::math;
    using V = m::DVec3;
    int failures = 0;
    auto check = [&](bool ok, const char* why) {
        if (!ok) {
            std::cerr << "FAIL: " << why << '\n';
            ++failures;
        }
    };
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << error;
        return 1;
    }
    p::BodyDesc floor;
    floor.motion = p::MotionType::Static;
    floor.shape = p::BoxShape{{5, .5F, 5}};
    floor.position = {0, -.5, 0};
    auto ground = world->create_body(floor, error);
    if (!ground) {
        std::cerr << error;
        return 1;
    }
    auto plant = a::FootPlant::create({}, error);
    if (!plant) {
        return 1;
    }
    a::FootPlantResult result;
    const auto hash = world->state_hash();
    check(plant->update(*world, {}, true, result, error) && result.locked() && result.acquired &&
              std::abs(result.target.y - .03) < 1e-6,
          "fresh plant acquires actual collision clearance");
    const auto anchor = result.target;
    check(plant->update(*world, {.1, 0, .1}, true, result, error) &&
              foot_placement::norm(result.target - anchor) < 1e-12 && !result.acquired,
          "held foot position is stable as animated foot moves");
    check(plant->update(*world, {.8, 0, 0}, true, result, error) && !result.locked() && result.released &&
              result.status == a::FootPlantStatus::TooFar,
          "excessive drift releases anchor");
    check(plant->update(*world, {}, true, result, error) && !result.locked() && !result.acquired,
          "failed held plant cannot silently reacquire");
    check(plant->update(*world, {}, false, result, error) && plant->update(*world, {}, true, result, error) &&
              result.acquired,
          "lift re-arms plant acquisition");
    auto sentinel = result;
    sentinel.target = {99, 98, 97};
    result = sentinel;
    check(!plant->update(*world, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, true, result, error) &&
              result.target == sentinel.target,
          "invalid call preserves output");
    check(plant->update(*world, {}, true, result, error) && result.locked() && !result.acquired &&
              foot_placement::norm(result.target - anchor) < 1e-12,
          "invalid call preserves existing anchor state");
    check(world->state_hash() == hash, "anchor queries and state transitions do not mutate physics");
    check(world->destroy_body(*ground), "support destroyed");
    check(plant->update(*world, {}, true, result, error) && result.released &&
              result.status == a::FootPlantStatus::SupportLost,
          "destroyed support releases safely");
    ground = world->create_body(floor, error);
    if (!ground) {
        return 1;
    }
    check(plant->update(*world, {}, true, result, error) && !result.locked(),
          "new generation cannot replace held support silently");
    check(plant->update(*world, {}, false, result, error) && plant->update(*world, {}, true, result, error) &&
              result.support == ground,
          "fresh cycle can acquire replacement body");
    auto other = p::World::create({}, error);
    if (!other) {
        return 1;
    }
    const auto other_ground = other->create_body(floor, error);
    if (!other_ground) {
        return 1;
    }
    check(plant->update(*other, {}, true, result, error) &&
              result.status == a::FootPlantStatus::SupportLost && !result.locked(),
          "cross-world handle is rejected without following another world's body");
    plant->reset();
    check(plant->update(*other, {}, true, result, error) && result.support == other_ground,
          "explicit reset starts an independent world binding");
    auto missing = a::FootPlant::create({}, error);
    check(missing->update(*world, {20, 0, 0}, true, result, error) &&
              result.status == a::FootPlantStatus::MissingSurface,
          "missing contact refuses acquisition");
    check(missing->update(*world, {}, true, result, error) && !result.locked(),
          "initial miss stays suppressed until lift");
    for (const auto policy :
         std::array<a::FootPlantPolicy, 4>{{{-.1, .35, 35},
                                            {0, .01, 35},
                                            {0, .35, 61},
                                            {0, std::numeric_limits<double>::quiet_NaN(), 35}}}) {
        check(!a::FootPlant::create(policy, error), "malformed plant policy refused");
    }
    auto moving = p::World::create({}, error);
    if (!moving) {
        return 1;
    }
    p::BodyDesc platform = floor;
    platform.motion = p::MotionType::Kinematic;
    const auto platform_id = moving->create_body(platform, error);
    if (!platform_id) {
        return 1;
    }
    auto transported = a::FootPlant::create({.03, 1, 35}, error);
    check(transported->update(*moving, {.2, 0, 0}, true, result, error) && result.locked(),
          "kinematic support acquired");
    const auto initial = moving->state(*platform_id);
    const V local = result.target - initial->position;
    check(moving->set_velocity(*platform_id, {.1F, 0, 0}, {0, 0, .2F}),
          "support linear/angular velocity set");
    for (int i = 0; i < 20; ++i) {
        check(moving->step(.02F), "real support advances");
        const auto before = moving->state_hash();
        const auto state = moving->state(*platform_id);
        const auto expected = state->position + foot_placement::rotate(state->rotation, local);
        const auto expected_normal = m::rotate(state->rotation, m::Vec3{0, 1, 0});
        check(transported->update(*moving, {.2, 0, 0}, true, result, error) && result.locked() &&
                  foot_placement::norm(result.target - expected) < 1e-6 &&
                  m::length(result.normal - expected_normal) < 1e-5F,
              "anchor follows actual support translation and rotation");
        check(moving->state_hash() == before, "support transport remains read only");
    }
    check(moving->set_velocity(*platform_id, {}, {0, 0, 2}), "support tilt velocity set");
    bool steep_released = false;
    for (int i = 0; i < 40 && !steep_released; ++i) {
        check(moving->step(.02F), "tilting platform advances");
        check(transported->update(*moving, {.2, 0, 0}, true, result, error), "tilting support resolves");
        if (result.released) {
            steep_released = result.status == a::FootPlantStatus::TooSteep;
        }
    }
    check(steep_released, "held support crossing slope limit releases anchor");
    auto orphan = a::FootPlant::create({}, error);
    {
        auto temporary = p::World::create({}, error);
        if (!temporary) {
            return 1;
        }
        const auto temporary_floor = temporary->create_body(floor, error);
        if (!temporary_floor) {
            return 1;
        }
        check(orphan->update(*temporary, {}, true, result, error) && result.locked(),
              "temporary world anchor acquired");
    }
    check(orphan->update(*other, {}, true, result, error) && result.status == a::FootPlantStatus::SupportLost,
          "plant survives prior world destruction without retaining a world pointer");
    auto far_world = p::World::create({}, error);
    if (!far_world) {
        return 1;
    }
    platform.position = {64000, -.5, -64000};
    const auto far_id = far_world->create_body(platform, error);
    if (!far_id) {
        return 1;
    }
    auto far = a::FootPlant::create({}, error);
    check(far->update(*far_world, {64000, 0, -64000}, true, result, error), "far-origin anchor acquired");
    check(far_world->set_velocity(*far_id, {.001F, 0, 0}, {}), "far support millimetre velocity");
    check(far_world->step(.1F) && far->update(*far_world, {64000, 0, -64000}, true, result, error) &&
              result.locked() && std::abs((result.target.x - 64000) - .0001) < 1e-7,
          "double-position anchor retains submillimetre support displacement at64km");
    auto steep_world = p::World::create({}, error);
    if (!steep_world) {
        return 1;
    }
    floor.rotation = m::from_axis_angle({0, 0, 1}, std::numbers::pi_v<float> / 3);
    floor.position = {0, -.5 / std::cos(std::numbers::pi_v<float> / 3), 0};
    const auto steep_ground = steep_world->create_body(floor, error);
    if (!steep_ground) {
        return 1;
    }
    auto steep = a::FootPlant::create({}, error);
    check(steep->update(*steep_world, {}, true, result, error) &&
              result.status == a::FootPlantStatus::TooSteep && !result.locked(),
          "steep plant acquisition refused");
    const auto asset = stance_fixture::make();
    const auto rest = a::rest_pose(asset.skeleton);
    const rig_roles::Indices roles{{0, 1, 2, 3, 4, 5, 6}};
    lykoware::character::FootSettings settings;
    settings.locking = true;
    settings.align_normals = true;
    settings.pelvis = true;
    settings.max_plant_distance = 1;
    foot_placement::Controller controller;
    check(controller.configure(asset.skeleton, roles, error, .03, settings), "walking controller prepares");
    a::Pose output;
    check(controller.update(rest, {}, *world, .01F, true, output, error,
                            foot_placement::PlantPhases{{true, false}}),
          "left foot plants while right swings");
    foot_placement::Placement actor;
    actor.feet.z = -.1;
    check(controller.update(rest, actor, *world, .01F, true, output, error,
                            foot_placement::PlantPhases{{true, false}}),
          "actor moves around held foot");
    std::vector<m::Mat4> fk;
    a::global_transforms(asset.skeleton, output, fk);
    const auto held = foot_placement::to_world(actor, foot_placement::position(fk[3]));
    const auto swing = foot_placement::to_world(actor, foot_placement::position(fk[6]));
    check(std::abs(held.z) < .001 && std::abs(swing.z + .1) < 1e-6,
          "held world target stays put and swing leg follows animation");
    controller.reset();
    check(controller.update(rest, {}, *world, .01F, true, output, error,
                            foot_placement::PlantPhases{{true, true}}),
          "bilateral plants acquire");
    actor.feet.z = -.8;
    check(controller.update(rest, actor, *world, .01F, true, output, error,
                            foot_placement::PlantPhases{{true, true}}) &&
              controller.stats().released == 2 && controller.pelvis_offset() == 0,
          "material reach failure releases both anchors within bounded retries");
    check(output[1].rotation.w == rest[1].rotation.w && output[4].rotation.w == rest[4].rotation.w,
          "reach release restores complete base leg pose");
    const auto count = controller.stats().planted;
    check(controller.update(rest, {}, *world, .01F, true, output, error,
                            foot_placement::PlantPhases{{true, true}}) &&
              controller.stats().planted == count,
          "reach-release suppresses hidden reacquisition");
    foot_placement::Controller rollback;
    check(rollback.configure(asset.skeleton, roles, error, .03, settings), "rollback fixture setup");
    auto invalid = rest;
    invalid[6].translation.y = -.5F;
    output = rest;
    const auto before = rollback.stats();
    check(!rollback.update(invalid, {}, *world, .01F, true, output, error,
                           foot_placement::PlantPhases{{true, true}}) &&
              rollback.stats().planted == before.planted && output[1].rotation.w == 1,
          "second-leg failure rolls back newly acquired anchor and output");
    actor.feet.z = -.1;
    check(rollback.update(rest, actor, *world, .01F, true, output, error,
                          foot_placement::PlantPhases{{true, true}}),
          "valid retry reacquires at current actor");
    a::global_transforms(asset.skeleton, output, fk);
    check(std::abs(foot_placement::to_world(actor, foot_placement::position(fk[3])).z + .1) < .001,
          "failed attempt did not persist the old left anchor");
    auto phased = asset;
    a::Graph graph;
    graph.parameters = {{"speed", 0}};
    a::Clip gait;
    gait.name = "gait";
    a::Channel silent;
    silent.joint = 0;
    silent.times = {0, 1};
    silent.values = {{0, 1.6F, 0, 0}, {0, 1.6F, 0, 0}};
    gait.channels = {silent};
    graph.states = {{"gait",
                     "gait",
                     1,
                     0,
                     true,
                     {{"left_plant", 0}, {"right_lift", 0}, {"left_lift", .5F}, {"right_plant", .5F}}},
                    {"air", "gait", 1, 0, true, {}}};
    graph.edges = {{0, 1, 0, {{0, a::Compare::Greater, 1}}}, {1, 0, 0, {{0, a::Compare::LessEqual, 1}}}};
    phased.graph = std::make_shared<a::Graph>(graph);
    phased.graph_clips = {gait};
    training_mannequin::Playback playback;
    playback.advance(phased, 0, 0);
    check(playback.planted[0] && !playback.planted[1],
          "time-zero entry markers are consumed before first render");
    for (int i = 0; i < 6; ++i) {
        playback.advance(phased, .1F, 0);
    }
    check(!playback.planted[0] && playback.planted[1],
          "authored lift/plant swap phases across clip midpoint");
    for (int i = 0; i < 5; ++i) {
        playback.advance(phased, .1F, 0);
    }
    check(playback.planted[0] && !playback.planted[1], "clip wrap consumes incoming zero markers");
    playback.advance(phased, .01F, 2);
    check(!playback.planted[0] && !playback.planted[1], "state transition clears phases without new markers");
    playback.reset();
    check(!playback.planted[0] && !playback.planted[1], "respawn reset clears old phases");
    playback.advance(phased, 0, 0);
    check(playback.planted[0] && !playback.planted[1], "reset replays time-zero plant markers");
    auto rapid_graph = graph;
    rapid_graph.edges.clear();
    rapid_graph.states.resize(1);
    rapid_graph.states[0].events = {
        {"left_plant", 0}, {"right_lift", 0}, {"left_lift", .01F}, {"left_plant", .02F}};
    auto rapid = phased;
    rapid.graph = std::make_shared<a::Graph>(rapid_graph);
    training_mannequin::Playback rapid_playback;
    rapid_playback.advance(rapid, 0, 0);
    foot_placement::Controller rapid_controller;
    check(rapid_controller.configure(asset.skeleton, roles, error, .03, settings),
          "rapid marker controller prepares");
    check(rapid_controller.update(
              rest, {}, *world, .01F, true, output, error,
              foot_placement::PlantPhases{rapid_playback.planted, rapid_playback.plant_cycles}),
          "initial authored plant acquires");
    rapid_playback.advance(rapid, .05F, 0);
    actor.feet.z = -.1;
    check(rapid_playback.planted[0] &&
              rapid_controller.update(
                  rest, actor, *world, .05F, true, output, error,
                  foot_placement::PlantPhases{rapid_playback.planted, rapid_playback.plant_cycles}),
          "lift and replant within one visible frame resolves");
    a::global_transforms(asset.skeleton, output, fk);
    check(std::abs(foot_placement::to_world(actor, foot_placement::position(fk[3])).z + .1) < .001 &&
              rapid_controller.stats().planted == 2 && rapid_controller.stats().released == 1,
          "plant cycle epochs prevent holding a stale anchor across hidden lift/replant markers");
    if (argc != 2) {
        std::cerr << "Expected replay fixture directory";
        return 1;
    }
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    const auto replay_file = directory / "input.json";
    const auto write = [&](const std::string& text) {
        std::ofstream file(replay_file, std::ios::binary);
        file << text;
    };
    write(R"({"version":1,"frames":[{"forward":0.1,"repeat":3},{"jump":true}]})");
    const auto intents = player_replay::load(replay_file);
    check(intents.size() == 4 && intents[2].forward == .1F && intents[3].jump,
          "bounded replay expands exact authored intents");
    for (const std::string& text : std::array<std::string, 9>{
             {R"({"version":1,"version":1,"frames":[{}]})", R"({"version":1,"frames":[]})",
              R"({"version":1,"frames":[{"repeat":6001}]})", R"({"version":1,"frames":[{"forward":2}]})",
              R"({"version":1,"frames":[{"jump":1}]})", R"({"version":1,"frames":[{"unknown":0}]})",
              R"({"version":1,"frames":[{"repeat":0}]})", R"({"version":1.5,"frames":[{}]})",
              std::string(65537, ' ')}}) {
        write(text);
        bool rejected = false;
        try {
            (void)player_replay::load(replay_file);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "malformed/oversized replay refused");
    }
    auto reused = a::FootPlant::create({}, error);
    check(reused->update(*world, {}, true, result, error), "allocation fixture warmup");
    allocations = 0;
    tracking = true;
    auto allocation_control = rest;
    tracking = false;
    check(allocations > 0 && allocation_control.size() == 7, "counter observes control allocation");
    allocations = 0;
    tracking = true;
    bool ok = true;
    for (int i = 0; i < 1000; ++i) {
        ok = reused->update(*world, {}, true, result, error) && ok;
    }
    tracking = false;
    check(ok && allocations == 0, "1000 anchor resolutions allocate no ordinary C++ storage");
    rollback.reset();
    check(rollback.update(rest, {}, *world, .01F, true, output, error,
                          foot_placement::PlantPhases{{true, false}}),
          "controller allocation warmup");
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        ok = rollback.update(rest, {}, *world, .01F, true, output, error,
                             foot_placement::PlantPhases{{true, false}}) &&
             ok;
    }
    tracking = false;
    check(ok && allocations == 0,
          "1000 complete planted controller updates allocate no ordinary C++ storage");
    std::cout << "plant_updates=1000 allocations=" << allocations << '\n';
    return failures ? 1 : 0;
}
