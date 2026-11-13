// physics.body_pose (ADR 0078): a body's mass, and moving a dynamic or kinematic body to a pose,
// through the Lykoware physics API only. physics.world still checks the golden state hash, which
// shows worlds that do not use these calls are unchanged.

#include <lykoware/physics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace {
namespace physics = lykoware::physics;
namespace math = lykoware::math;
using physics::DVec3;
using physics::Quat;
using physics::Vec3;

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}

constexpr float tick = 1.F / 60.F;
constexpr double reach = 1e-4;  // metres from the pose after the step
constexpr float turn = 1e-3F;   // radians from the pose after the step

std::unique_ptr<physics::World> make_world(physics::WorldConfig config = {})
{
    std::string error;
    auto world = physics::World::create(config, error);
    if (!world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

std::optional<physics::BodyId> try_body(physics::World& world, physics::MotionType motion, DVec3 position,
                                        physics::Shape shape, float mass, std::string& error)
{
    physics::BodyDesc desc;
    desc.shape = std::move(shape);
    desc.motion = motion;
    desc.position = position;
    desc.mass = mass;
    return world.create_body(desc, error);
}

physics::BodyId body(physics::World& world, physics::MotionType motion, DVec3 position, physics::Shape shape,
                     float mass = 0.F)
{
    std::string error;
    const auto made = try_body(world, motion, position, std::move(shape), mass, error);
    if (!made) {
        std::cerr << "FATAL: body: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *made;
}

double distance(DVec3 a, DVec3 b)
{
    const double x = a.x - b.x;
    const double y = a.y - b.y;
    const double z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

float angle_between(Quat a, Quat b)
{
    const float dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.F * std::acos(std::min(1.F, dot));
}

void test_mass()
{
    auto world = make_world();
    const physics::BoxShape cube{{.5F, .5F, .5F}}; // one cubic metre
    const auto plain = body(*world, physics::MotionType::Dynamic, {0, 5, 0}, cube);
    const auto heavy = body(*world, physics::MotionType::Dynamic, {3, 5, 0}, cube, 80.F);
    const auto ground = body(*world, physics::MotionType::Static, {0, -1, 0}, physics::BoxShape{{10, 1, 10}});
    const auto door = body(*world, physics::MotionType::Kinematic, {6, 5, 0}, cube);

    check(world->mass(plain) && std::abs(*world->mass(plain) - 1000.F) < .5F, "mass 0 is the shape's, at 1000 kg/m^3");
    check(world->mass(heavy) && std::abs(*world->mass(heavy) - 80.F) < 1e-3F, "an explicit mass is kept");
    check(!world->mass(ground) && !world->mass(door), "static and kinematic bodies report no mass");
    world->destroy_body(heavy);
    check(!world->mass(heavy), "a destroyed body reports no mass");

    std::string error;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {-1.F, nan, inf}) {
        check(!try_body(*world, physics::MotionType::Dynamic, {}, cube, bad, error) && !error.empty(),
              "a negative or non-finite mass is refused");
    }
    check(!try_body(*world, physics::MotionType::Static, {}, cube, 5.F, error) && !error.empty(),
          "a mass on a static body is refused");
    check(!try_body(*world, physics::MotionType::Kinematic, {}, cube, 5.F, error) && !error.empty(),
          "a mass on a kinematic body is refused");

    // The mass is used: two equal spheres meet head on; the heavier one keeps going forward.
    physics::WorldConfig weightless;
    weightless.gravity = {0, 0, 0};
    auto space = make_world(weightless);
    physics::BodyDesc desc;
    desc.shape = physics::SphereShape{.5F};
    desc.restitution = .5F;
    desc.position = {-2, 0, 0};
    desc.linear_velocity = {2, 0, 0};
    desc.mass = 50.F;
    const auto big = *space->create_body(desc, error);
    desc.position = {2, 0, 0};
    desc.linear_velocity = {-2, 0, 0};
    desc.mass = 5.F;
    const auto small = *space->create_body(desc, error);
    for (int i = 0; i < 120; ++i) {
        space->step(tick);
    }
    const auto big_after = space->state(big);
    const auto small_after = space->state(small);
    check(big_after && small_after && big_after->linear_velocity.x > 0.F && small_after->linear_velocity.x > 0.F,
          "the heavier sphere pushes the lighter one back");
}

void test_pose()
{
    auto world = make_world();
    const physics::BoxShape box{{.2F, .4F, .1F}};
    const auto dynamic = body(*world, physics::MotionType::Dynamic, {0, 3, 0}, box, 4.F);
    const auto kinematic = body(*world, physics::MotionType::Kinematic, {5, 3, 0}, box);

    // One step to a pose 0.5 m away and a fifth of a radian round, in the air under gravity. Jolt
    // caps a body's spin near 47 rad/s, so a larger turn takes more than one 60 Hz step.
    const Quat quarter = math::from_axis_angle({0, 1, 0}, .2F);
    const DVec3 there{.3, 3.4, .2};
    check(world->drive_to_pose(dynamic, there, quarter, tick), "a dynamic body takes a pose");
    check(world->drive_to_pose(kinematic, {5.3, 3.4, .2}, quarter, tick), "a kinematic body takes a pose");
    world->step(tick);
    const auto moved = *world->state(dynamic);
    const auto slid = *world->state(kinematic);
    std::cout << "dynamic after one step: " << distance(moved.position, there) << " m, "
              << angle_between(moved.rotation, quarter) << " rad from the pose\n";
    check(distance(moved.position, there) < reach, "the dynamic body reaches the position in one step");
    check(angle_between(moved.rotation, quarter) < turn, "the dynamic body reaches the rotation in one step");
    check(distance(slid.position, {5.3, 3.4, .2}) < reach && angle_between(slid.rotation, quarter) < turn,
          "the kinematic body reaches the pose in one step");

    // Following a moving pose for two seconds: gravity never pulls it off.
    double worst = 0.0;
    float worst_turn = 0.F;
    for (int i = 1; i <= 120; ++i) {
        const float t = static_cast<float>(i) * tick;
        const DVec3 target{.3 + .5 * std::sin(t), 3.4 + .2 * std::cos(2.F * t), .2};
        const Quat facing = math::from_axis_angle({0, 1, 0}, .2F + t);
        world->drive_to_pose(dynamic, target, facing, tick);
        world->step(tick);
        const auto now = *world->state(dynamic);
        worst = std::max(worst, distance(now.position, target));
        worst_turn = std::max(worst_turn, angle_between(now.rotation, facing));
    }
    std::cout << "following a pose: worst " << worst << " m, " << worst_turn << " rad\n";
    check(worst < reach && worst_turn < turn, "a body driven every step follows a moving pose");

    // A wall stops it: contacts still act on a dynamic body. The pose walks through the wall at
    // 3 m/s; the body stays on the near side.
    const auto wall = body(*world, physics::MotionType::Static, {2, 3.4, .2}, physics::BoxShape{{.1F, 2, 2}});
    const double start = world->state(dynamic)->position.x;
    for (int i = 1; i <= 120; ++i) {
        world->drive_to_pose(dynamic, {std::min(4.0, start + 3.0 * i * tick), 3.4, .2}, quarter, tick);
        world->step(tick);
    }
    check(world->state(dynamic)->position.x < 2.0, "a wall stops a driven dynamic body short of the pose");
    world->destroy_body(wall);

    // Refusals change nothing.
    const auto before = *world->state(dynamic);
    const auto ground = body(*world, physics::MotionType::Static, {0, -1, 0}, physics::BoxShape{{10, 1, 10}});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(!world->drive_to_pose(ground, {}, {}, tick), "a static body cannot be driven");
    check(!world->drive_to_pose(dynamic, {nan, 0, 0}, {}, tick), "a non-finite position is refused");
    check(!world->drive_to_pose(dynamic, {}, Quat{0, 0, 0, 0}, tick), "a zero rotation is refused");
    check(!world->drive_to_pose(dynamic, {}, Quat{nan, 0, 0, 1}, tick), "a non-finite rotation is refused");
    check(!world->drive_to_pose(dynamic, {}, {}, 0.F) && !world->drive_to_pose(dynamic, {}, {}, -tick)
              && !world->drive_to_pose(dynamic, {}, {}, nan),
          "seconds must be finite and above 0");
    const auto after = *world->state(dynamic);
    check(after.linear_velocity.x == before.linear_velocity.x && after.linear_velocity.y == before.linear_velocity.y
              && after.angular_velocity.y == before.angular_velocity.y,
          "a refused drive leaves the velocities alone");
    const auto gone = body(*world, physics::MotionType::Dynamic, {9, 9, 9}, box);
    world->destroy_body(gone);
    check(!world->drive_to_pose(gone, {}, {}, tick), "a destroyed body cannot be driven");
    auto other = make_world();
    check(!other->drive_to_pose(dynamic, {}, {}, tick) && !other->mass(dynamic), "a foreign handle is refused");
}

// Deliberate break: the naive drive, velocity = distance / time, misses by the step's gravity, so
// the reach check above can fail.
void test_naive_drive_misses()
{
    auto world = make_world();
    const auto box = body(*world, physics::MotionType::Dynamic, {0, 3, 0}, physics::BoxShape{{.2F, .2F, .2F}});
    const DVec3 there{0, 3.5, 0};
    world->set_velocity(box, {0, static_cast<float>((there.y - 3.0) / tick), 0}, {});
    world->step(tick);
    const double miss = distance(world->state(box)->position, there);
    std::cout << "naive drive misses by " << miss << " m\n";
    check(miss > reach, "the naive drive would fail the reach check");
}

std::uint64_t replay()
{
    auto world = make_world();
    const auto box = body(*world, physics::MotionType::Dynamic, {0, 3, 0}, physics::BoxShape{{.2F, .3F, .1F}}, 7.F);
    body(*world, physics::MotionType::Static, {0, -1, 0}, physics::BoxShape{{10, 1, 10}});
    for (int i = 0; i < 90; ++i) {
        const float t = static_cast<float>(i) * tick;
        if (i < 45) {
            world->drive_to_pose(box, {std::sin(t), 2.0, 0}, math::from_axis_angle({1, 0, 0}, t), tick);
        }
        world->step(tick);
    }
    return world->state_hash();
}

} // namespace

int main()
{
    test_mass();
    test_pose();
    test_naive_drive_misses();
    check(replay() == replay(), "masses and drives replay bit for bit");
    if (failures == 0) {
        std::cout << "physics.body_pose: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
