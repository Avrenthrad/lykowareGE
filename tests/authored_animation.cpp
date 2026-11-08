#include "animated_mannequin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <lykoware/animation_graph.hpp>
#include <lykoware/player.hpp>
#include <lykoware/retarget.hpp>
#include <new>
#include <numbers>
#include <stdexcept>
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
    using namespace lykoware;
    namespace a = animation;
    int failures = 0;
    auto check = [&](bool b, const char* why) {
        if (!b) {
            std::cerr << "FAIL: " << why << '\n';
            ++failures;
        }
    };
    std::string error;
    a::Skeleton skeleton;
    a::Joint root;
    root.name = "root";
    a::Joint hand;
    hand.name = "hand";
    hand.parent = std::uint16_t{0};
    hand.rest.translation = {0, 1, 0};
    hand.inverse_bind = math::translation({0, -100, 0});
    skeleton.joints = {root, hand};
    a::Clip idle;
    idle.name = "idle";
    a::Channel move;
    move.joint = 0;
    move.times = {0, 1};
    move.values = {{0, 0, 0, 0}, {2, 0, 0, 0}};
    idle.channels = {move};
    a::Clip walk = idle;
    walk.name = "walk";
    walk.channels[0].values[1].x = 4;
    a::Graph graph;
    const std::string text =
        R"({"version":1,"parameters":[{"name":"speed"}],"states":[{"name":"idle","clip":"idle","events":[{"name":"entry","seconds":0},{"name":"step","seconds":0.5},{"name":"end","seconds":1}]},{"name":"walk","clip":"walk","fade":0.25}],"edges":[{"from":0,"to":1,"conditions":[{"parameter":0,"op":">","value":1}]},{"from":1,"to":0,"conditions":[{"parameter":0,"op":"<","value":0.5}]}],"sockets":[{"name":"weapon","joint":"hand","translation":[1,0,0]}]})";
    check(a::decode_graph(text, graph, error), "authored graph decode");
    auto player = a::GraphPlayer::create(skeleton, {idle, walk}, graph, error);
    if (!player) {
        std::cerr << error;
        return 1;
    }
    std::array<float, 1> params{{0}};
    check(player->step(.5F, params, error), "first half step");
    check(player->events().size() == 2 && player->events()[0].event == 0 && player->events()[1].event == 1,
          "entry zero and half event");
    check(std::abs(player->pose()[0].translation.x - 1) < 1e-6, "sampled pose");
    check(std::abs(player->sockets()[0].m[3][0] - 2) < 1e-6 && player->sockets()[0].m[3][1] == 1,
          "socket uses posed global joint, not inverse bind");
    check(player->step(.5F, params, error), "exact wrap step");
    check(player->events().size() == 2, "endpoint and next-cycle zero both emitted at wrap");
    check(player->step(0, params, error) && player->events().empty(), "no duplicate events on zero dt");
    params[0] = 2;
    check(player->step(.125F, params, error) && player->state() == 1, "authored transition");
    check(std::abs(player->pose()[0].translation.x - .25F) < 1e-6,
          "crossfade advances half way from visible wrap pose");
    const auto held = player->pose()[0].translation;
    params[0] = 0;
    check(player->step(0, params, error) && player->pose()[0].translation == held,
          "interruption captures visible pose");
    const auto age = player->age();
    const auto state = player->state();
    params[0] = std::numeric_limits<float>::quiet_NaN();
    check(!player->step(.1F, params, error) && player->age() == age && player->state() == state &&
              player->pose()[0].translation == held,
          "bad step preserves state and pose");
    params[0] = 0;
    graph.states[0].rate = 16;
    auto overflow = a::GraphPlayer::create(skeleton, {idle, walk}, graph, error);
    check(overflow && !overflow->step(60, params, error) && overflow->age() == 0,
          "event overflow refuses before advancing state");
    graph.states[0].rate = 1;
    auto bad = graph;
    bad.states[0].clip = "missing";
    check(!a::GraphPlayer::create(skeleton, {idle, walk}, bad, error), "missing clip refused");
    bad = graph;
    bad.edges[0].conditions[0].parameter = 99;
    check(!a::GraphPlayer::create(skeleton, {idle, walk}, bad, error), "bad parameter refused");
    bad = graph;
    bad.sockets[0].joint = "absent";
    check(!a::GraphPlayer::create(skeleton, {idle, walk}, bad, error), "missing socket joint refused");
    a::Graph unchanged = graph;
    check(!a::decode_graph(R"({"version":1,"version":1,"states":[]})", unchanged, error) &&
              unchanged.states.size() == 2,
          "duplicate JSON keys refused transactionally");
    check(
        !a::decode_graph(R"({"version":1,"states":[{"name":"x","clip":"idle","oops":1}]})", unchanged, error),
        "unknown graph field refused");
    a::Clip scaled = idle;
    scaled.channels[0].target = a::Target::Scale;
    scaled.channels[0].values = {{1, 1, 1, 0}, {2, 2, 2, 0}};
    auto scale_player = a::GraphPlayer::create(skeleton, {scaled, walk}, graph, error);
    check(scale_player && !scale_player->step(.5F, params, error) && scale_player->age() == 0,
          "unsupported animated scale rejected transactionally");
    a::PoseCrossfade fade;
    auto pose = a::rest_pose(skeleton);
    check(fade.reset(pose, error), "fade setup");
    auto invalid = pose;
    invalid[0].scale = {-1, -1, -1};
    auto output = pose;
    check(!fade.retarget(invalid, .5F, 1.F, output, error) && output[0].scale == pose[0].scale &&
              fade.progress() == 1,
          "atomic restart rejects zero intermediate scale");
    auto target = skeleton;
    target.joints[0].rest.rotation = math::from_axis_angle({0, 1, 0}, std::numbers::pi_v<float> * .5F);
    target.joints[0].rest.translation = {10, 0, 0};
    target.joints[1].rest.translation = {0, 2, 0};
    auto plan = a::Retarget::create(skeleton, target, 2, error);
    if (!plan) {
        std::cerr << error;
        return 1;
    }
    pose[0].translation = {1, 0, 0};
    pose[1].rotation = math::from_axis_angle({1, 0, 0}, .4F);
    output.resize(2);
    check(plan->apply(pose, output, error), "supported retarget transfer");
    check(std::abs(output[0].translation.z + 2) < 1e-5 && std::abs(output[0].translation.x - 10) < 1e-5 &&
              output[1].translation.y == 2,
          "root basis/scale and target bone length");
    check(std::abs(output[1].rotation.x - pose[1].rotation.x) < 1e-6, "local rotation transferred");
    const auto saved = output[0].translation;
    pose[1].translation.x = 1;
    check(!plan->apply(pose, output, error) && output[0].translation == saved,
          "dynamic bone stretch refused without output change");
    pose[1].translation.x = 0;
    auto incompatible = target;
    incompatible.joints[1].rest.translation = {2, 0, 0};
    check(!a::Retarget::create(skeleton, incompatible, 1, error), "different rest direction refused");
    incompatible = target;
    incompatible.joints[1].name = "unknown";
    check(!a::Retarget::create(skeleton, incompatible, 1, error), "unmapped bones refused");
    check(!a::Retarget::create(skeleton, target, 0, error), "zero root scale refused");
    auto aliased = pose;
    check(plan->apply(aliased, aliased, error) && aliased[1].translation.y == 2,
          "retarget input/output aliases safely");
    std::vector<math::Mat4> socket_output(graph.sockets.size());
    check(a::evaluate_sockets(target, output, graph.sockets, socket_output, error),
          "socket evaluation follows retargeted skeleton");
    const auto far_position =
        math::DVec3{64000, 0, -64000} +
        math::Vec3{socket_output[0].m[3][0], socket_output[0].m[3][1], socket_output[0].m[3][2]};
    check(std::abs((far_position.x - 64000) - socket_output[0].m[3][0]) < 1e-9,
          "socket composition keeps double actor origin");
    auto overlap = pose;
    overlap.push_back({});
    check(plan->apply(std::span{overlap.data(), 2}, std::span{overlap.data() + 1, 2}, error) &&
              overlap[2].translation.y == 2,
          "partial retarget overlap commits safely");
    a::Skeleton broad = skeleton;
    broad.joints.push_back(broad.joints[1]);
    broad.joints[2].name = "other";
    broad.joints[2].rest.translation = {1, 0, 0};
    auto reordered = broad;
    std::swap(reordered.joints[1], reordered.joints[2]);
    reordered.joints[1].rest.translation = {2, 0, 0};
    reordered.joints[2].rest.translation = {0, 2, 0};
    auto reordered_plan = a::Retarget::create(broad, reordered, 1, error);
    auto reordered_pose = a::rest_pose(broad), reordered_out = a::rest_pose(reordered);
    reordered_pose[1].rotation = math::from_axis_angle({1, 0, 0}, .7F);
    check(reordered_plan && reordered_plan->apply(reordered_pose, reordered_out, error) &&
              std::abs(reordered_out[2].rotation.x - reordered_pose[1].rotation.x) < 1e-6,
          "retarget maps names across sibling index permutations");
    a::Skeleton maximum;
    for (std::size_t i = 0; i < a::max_joints; ++i) {
        a::Joint joint;
        joint.name = "bone_" + std::to_string(i);
        if (i) {
            joint.parent = static_cast<std::uint16_t>(i - 1);
            joint.rest.translation = {0, .01F, 0};
        }
        maximum.joints.push_back(joint);
    }
    auto maximum_target = maximum;
    for (std::size_t i = 1; i < maximum_target.joints.size(); ++i) {
        maximum_target.joints[i].rest.translation.y = .02F;
    }
    auto maximum_plan = a::Retarget::create(maximum, maximum_target, 1, error);
    auto maximum_pose = a::rest_pose(maximum), maximum_out = a::rest_pose(maximum_target);
    check(maximum_plan && maximum_plan->apply(maximum_pose, maximum_out, error) &&
              maximum_out.back().translation.y == .02F,
          "maximum 256-joint retarget keeps target lengths");
    auto priority = graph;
    priority.states.push_back(priority.states[1]);
    priority.states.back().name = "third";
    priority.edges.insert(priority.edges.begin() + 1, {0, 2, 0, {{0, a::Compare::Greater, 1}}});
    priority.edges[0].minimum_age = .5F;
    priority.edges[1].minimum_age = .5F;
    auto priority_player = a::GraphPlayer::create(skeleton, {idle, walk}, priority, error);
    params[0] = 2;
    check(priority_player && priority_player->step(.25F, params, error) && priority_player->state() == 0 &&
              priority_player->step(.25F, params, error) && priority_player->state() == 0 &&
              priority_player->step(0, params, error) && priority_player->state() == 1,
          "minimum age uses prior age; first eligible edge wins");
    params[0] = 0;
    auto partitioned = a::GraphPlayer::create(skeleton, {idle, walk}, graph, error);
    std::size_t partition_count = 0;
    for (int i = 0; i < 8; ++i) {
        check(partitioned->step(.125F, params, error), "partitioned step");
        partition_count += partitioned->events().size();
    }
    check(partition_count == 4, "event counts remain identical across exact time partitions");
    auto stopped = graph;
    stopped.states[0].loop = false;
    auto stopped_player = a::GraphPlayer::create(skeleton, {idle, walk}, stopped, error);
    check(stopped_player && stopped_player->step(1, params, error) && stopped_player->events().size() == 3 &&
              stopped_player->step(1, params, error) && stopped_player->events().empty(),
          "non-loop events fire once and final pose holds");
    skeleton.joints[1].name = "mutated";
    target.joints.clear();
    check(plan->apply(pose, output, error), "prepared retarget owns source and target snapshots");
    allocations = 0;
    tracking = true;
    auto allocation_control = maximum_pose;
    tracking = false;
    check(allocations > 0 && allocation_control.size() == a::max_joints,
          "allocation counter observes control copy");
    allocations = 0;
    tracking = true;
    bool success = true;
    for (int i = 0; i < 1000; ++i) {
        params[0] = (i % 2) ? 2.F : 0.F;
        success = player->step(.01F, params, error) && plan->apply(pose, output, error) &&
                  a::evaluate_sockets(maximum, maximum_pose, {}, {}, error) && success;
    }
    tracking = false;
    check(success && allocations == 0,
          "1000 graph/events/socket/retarget updates allocate no ordinary C++ storage");
    std::cout << "runtime_updates=1000 allocations=" << allocations << '\n';
    auto asset = training_mannequin::make();
    a::Graph sample_graph;
    sample_graph.parameters = {{"speed", 0}};
    sample_graph.states = {{"idle", asset.idle.name, 1, .14F, true, {}},
                           {"walk", asset.walk.name, 1, .14F, true, {}}};
    sample_graph.edges = {{0, 1, 0, {{0, a::Compare::Greater, 1}}}};
    asset.graph = std::make_shared<a::Graph>(sample_graph);
    asset.graph_clips = {asset.idle, asset.walk};
    training_mannequin::Playback playback;
    physics::CharacterState observation;
    observation.ground = physics::GroundState::OnGround;
    observation.velocity = {2, 0, 0};
    playback.advance_character(asset, .1F, observation);
    check(playback.graph_player && playback.graph_player->state() == 1 && playback.matrices.size() == 7,
          "actual character adapter consumes authored graph and skinning");
    auto replaced = std::make_shared<a::Graph>(sample_graph);
    replaced->parameters.resize(33);
    asset.graph = replaced;
    playback.advance_character(asset, .01F, observation);
    check(playback.graph_player->graph().parameters.size() == 1 && playback.graph_player->state() == 1,
          "replacement graph cannot bypass the owned parameter bound");
    playback.reset();
    bool rejected_replacement = false;
    try {
        playback.advance_character(asset, .01F, observation);
    } catch (const std::invalid_argument&) {
        rejected_replacement = true;
    }
    check(rejected_replacement, "reset validates replacement graph before binding parameters");
    asset.graph = std::make_shared<a::Graph>(sample_graph);
    if (argc != 2) {
        std::cerr << "Expected example graph path";
        return 1;
    }
    a::Graph example;
    check(a::load_graph(argv[1], example, error), "checked-in six-state graph parses");
    auto example_player = a::GraphPlayer::create(
        asset.skeleton, {asset.idle, asset.walk, asset.run, asset.jump, asset.fall, asset.land}, example,
        error);
    if (!example_player) {
        std::cerr << error;
        return 1;
    }
    std::array<float, 3> inputs{{0, 1, 0}};
    std::array<bool, 6> visited{};
    for (int i = 0; i < 360; ++i) {
        const int segment = i / 60;
        inputs = {segment == 0   ? 0.F
                  : segment == 1 ? 2.F
                                 : 6.F,
                  segment < 3 || segment >= 5 ? 1.F : 0.F, segment == 3 ? 4.F : -4.F};
        check(example_player->step(1.F / 60.F, inputs, error), "authored six-state sample step");
        visited[example_player->state()] = true;
    }
    check(std::all_of(visited.begin(), visited.end(), [](bool v) { return v; }),
          "authored example visits all six states");
    auto world = physics::World::create({}, error);
    if (!world) {
        std::cerr << error;
        return 1;
    }
    physics::BodyDesc floor;
    floor.motion = physics::MotionType::Static;
    floor.shape = physics::BoxShape{{10, .5F, 10}};
    floor.position = {0, -.5, 0};
    if (!world->create_body(floor, error)) {
        std::cerr << error;
        return 1;
    }
    auto controller = PlayerCharacter::create(*world, {}, error);
    if (!controller) {
        std::cerr << error;
        return 1;
    }
    for (int i = 0; i < 120; ++i) {
        controller->steer(PlayerIntent{.forward = i > 60 ? 1.F : 0.F});
        controller->before_step(1.F / 60.F);
        check(world->step(1.F / 60.F), "physics step");
    }
    const auto physics_hash = world->state_hash();
    playback.advance_character(asset, .01F, controller->state());
    check(world->state_hash() == physics_hash,
          "authored graph skinning from real controller observations preserves physics");
    return failures ? 1 : 0;
}
