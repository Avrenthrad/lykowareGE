// physics.joints (ADR 0073): fixed, hinge and swing-twist joints between bodies, through the
// Lykoware physics API only.

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
#include <vector>

namespace {
namespace physics = lykoware::physics;
namespace math = lykoware::math;
using physics::DVec3;
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
constexpr float pi = 3.14159265F;

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
physics::BodyId body(physics::World& world, physics::MotionType motion, DVec3 position, physics::Shape shape,
                     Vec3 velocity = {}, Vec3 spin = {})
{
    std::string error;
    physics::BodyDesc desc;
    desc.shape = std::move(shape);
    desc.motion = motion;
    desc.position = position;
    desc.linear_velocity = velocity;
    desc.angular_velocity = spin;
    const auto made = world.create_body(desc, error);
    if (!made) {
        std::cerr << "FATAL: body: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *made;
}
double distance(DVec3 a, DVec3 b)
{
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
// The world direction of a body's local axis.
Vec3 turned(const physics::BodyState& state, Vec3 local)
{
    return math::rotate(state.rotation, local);
}
float angle_between(Vec3 a, Vec3 b)
{
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
    return std::acos(std::clamp(dot / (math::length(a) * math::length(b)), -1.F, 1.F));
}
// The signed angle a body has turned about the world Z axis, from its local +X.
float turn_about_z(const physics::BodyState& state)
{
    const Vec3 x = turned(state, {1, 0, 0});
    return std::atan2(x.y, x.x);
}

const physics::Shape small_box = physics::BoxShape{{.1F, .1F, .1F}};

void test_hinge()
{
    // A weight on a 1 m arm hangs from a fixed post by a hinge about Z, and is let go sideways.
    auto world = make_world();
    const auto post = body(*world, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto weight = body(*world, physics::MotionType::Dynamic, {1, 5, 0}, small_box);
    std::string error;
    physics::JointDesc desc;
    desc.parent = post;
    desc.child = weight;
    desc.kind = physics::HingeJoint{};
    desc.anchor = {0, 5, 0};
    desc.axis = {0, 0, 3}; // any length
    const auto hinge = world->create_joint(desc, error);
    check(hinge.has_value() && world->is_valid(*hinge) && world->joint_count() == 1, "a hinge is made: " + error);
    double worst_arm = 0, worst_plane = 0, lowest = 5;
    float most_off_axis = 0;
    bool ok = true;
    for (int i = 0; i < 240; ++i) {
        ok = world->step(tick) && ok;
        const auto state = *world->state(weight);
        worst_arm = std::max(worst_arm, std::abs(distance(state.position, {0, 5, 0}) - 1.0));
        worst_plane = std::max(worst_plane, std::abs(state.position.z));
        lowest = std::min(lowest, state.position.y);
        most_off_axis = std::max(most_off_axis, angle_between(turned(state, {0, 0, 1}), {0, 0, 1}));
    }
    check(ok && worst_arm < .01, "a hinged weight stays on its arm: " + std::to_string(worst_arm));
    check(worst_plane < .005 && most_off_axis < .01F, "a hinged weight stays in the hinge's plane: " +
                                                          std::to_string(worst_plane) + ", " + std::to_string(most_off_axis));
    check(lowest < 4.1, "the weight swings down: " + std::to_string(lowest));

    // Limits: the same weight may only turn between -0.5 and +0.25 rad. Falling turns it negative.
    auto limited = make_world();
    const auto post2 = body(*limited, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto weight2 = body(*limited, physics::MotionType::Dynamic, {1, 5, 0}, small_box);
    desc.parent = post2;
    desc.child = weight2;
    desc.kind = physics::HingeJoint{-.5F, .25F};
    check(limited->create_joint(desc, error).has_value(), "a limited hinge is made: " + error);
    // A limit is soft for a moment when struck at speed: the weight may overshoot briefly, then
    // comes to rest on the limit.
    float lowest_turn = 0, highest_turn = 0;
    for (int i = 0; i < 600; ++i) {
        limited->step(tick);
        lowest_turn = std::min(lowest_turn, turn_about_z(*limited->state(weight2)));
    }
    const float rest = turn_about_z(*limited->state(weight2));
    check(lowest_turn > -.65F && std::abs(rest + .5F) < .03F,
          "a hinge stops at its lower limit: overshoot to " + std::to_string(lowest_turn) + ", rest at " + std::to_string(rest));
    // Thrown upward instead, it stops at the upper limit.
    auto upward = make_world();
    const auto post3 = body(*upward, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto weight3 = body(*upward, physics::MotionType::Dynamic, {1, 5, 0}, small_box, {0, 6, 0});
    desc.parent = post3;
    desc.child = weight3;
    check(upward->create_joint(desc, error).has_value(), "a second limited hinge is made: " + error);
    highest_turn = 0;
    for (int i = 0; i < 30; ++i) {
        upward->step(tick);
        highest_turn = std::max(highest_turn, turn_about_z(*upward->state(weight3)));
    }
    check(highest_turn > .2F && highest_turn < .4F, "a hinge stops at its upper limit: " + std::to_string(highest_turn));
    // Positive control: the same throw without limits goes much further.
    auto free_swing = make_world();
    const auto post4 = body(*free_swing, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto weight4 = body(*free_swing, physics::MotionType::Dynamic, {1, 5, 0}, small_box, {0, 6, 0});
    desc.parent = post4;
    desc.child = weight4;
    desc.kind = physics::HingeJoint{};
    check(free_swing->create_joint(desc, error).has_value(), "an unlimited hinge is made: " + error);
    highest_turn = 0;
    for (int i = 0; i < 30; ++i) {
        free_swing->step(tick);
        highest_turn = std::max(highest_turn, turn_about_z(*free_swing->state(weight4)));
    }
    check(highest_turn > 1.F, "without limits the same throw turns further: " + std::to_string(highest_turn));
}

void test_swing_twist()
{
    // An arm hangs below a fixed shoulder; the joint's axis points down the arm. Thrown sideways
    // and spun, it may swing 0.4 rad from straight down and twist 0.2 rad either way.
    auto world = make_world();
    const auto shoulder = body(*world, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto arm = body(*world, physics::MotionType::Dynamic, {0, 4, 0}, physics::CapsuleShape{.3F, .1F}, {6, 0, 3},
                          {0, 8, 0});
    std::string error;
    physics::JointDesc desc;
    desc.parent = shoulder;
    desc.child = arm;
    desc.kind = physics::SwingTwistJoint{.4F, -.2F, .2F};
    desc.anchor = {0, 5, 0};
    desc.axis = {0, -1, 0};
    check(world->create_joint(desc, error).has_value(), "a swing-twist joint is made: " + error);
    float widest = 0, most_twist = 0;
    double worst_arm = 0;
    for (int i = 0; i < 240; ++i) {
        world->step(tick);
        const auto state = *world->state(arm);
        const Vec3 down = turned(state, {0, -1, 0});
        widest = std::max(widest, angle_between(down, {0, -1, 0}));
        worst_arm = std::max(worst_arm, std::abs(distance(state.position, {0, 5, 0}) - 1.0));
        // Twist: how far the arm's local X has turned about its own axis, seen from above while
        // the arm is near vertical.
        if (angle_between(down, {0, -1, 0}) < .05F) {
            const Vec3 x = turned(state, {1, 0, 0});
            most_twist = std::max(most_twist, std::abs(std::atan2(-x.z, x.x)));
        }
    }
    check(widest > .35F && widest < .55F, "the arm swings to its cone and little further: " + std::to_string(widest));
    check(most_twist < .25F, "the arm twists no further than its limit: " + std::to_string(most_twist));
    check(worst_arm < .01, "the arm stays on its shoulder: " + std::to_string(worst_arm));

    // Positive control: with a wide cone the same throw swings much further.
    auto loose = make_world();
    const auto shoulder2 = body(*loose, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto arm2 = body(*loose, physics::MotionType::Dynamic, {0, 4, 0}, physics::CapsuleShape{.3F, .1F}, {6, 0, 3});
    desc.parent = shoulder2;
    desc.child = arm2;
    desc.kind = physics::SwingTwistJoint{2.5F, -.2F, .2F};
    check(loose->create_joint(desc, error).has_value(), "a loose swing-twist joint is made: " + error);
    widest = 0;
    for (int i = 0; i < 240; ++i) {
        loose->step(tick);
        widest = std::max(widest, angle_between(turned(*loose->state(arm2), {0, -1, 0}), {0, -1, 0}));
    }
    check(widest > 1.F, "a wide cone lets the same throw swing further: " + std::to_string(widest));
}

void test_fixed()
{
    // A box fixed to a falling, spinning box keeps its place on it.
    auto world = make_world();
    const auto a = body(*world, physics::MotionType::Dynamic, {0, 10, 0}, small_box, {1, 0, 0}, {0, 0, 3});
    const auto b = body(*world, physics::MotionType::Dynamic, {.5, 10, 0}, small_box);
    std::string error;
    physics::JointDesc desc;
    desc.parent = a;
    desc.child = b;
    desc.anchor = {.25, 10, 0};
    const auto joint = world->create_joint(desc, error);
    check(joint.has_value(), "a fixed joint is made: " + error);
    double worst = 0;
    float worst_turn = 0;
    for (int i = 0; i < 60; ++i) {
        world->step(tick);
        const auto sa = *world->state(a), sb = *world->state(b);
        worst = std::max(worst, std::abs(distance(sa.position, sb.position) - .5));
        worst_turn = std::max(worst_turn, angle_between(turned(sa, {1, 0, 0}), turned(sb, {1, 0, 0})));
    }
    check(worst < .005 && worst_turn < .02F, "fixed bodies keep their relative pose: " + std::to_string(worst) + ", " +
                                                 std::to_string(worst_turn));
    const auto before = *world->state(b);
    check(std::abs(before.position.y - 10) > 1, "the fixed pair fell together");
    // Without the joint they come apart.
    check(world->destroy_joint(*joint) && !world->is_valid(*joint) && world->joint_count() == 0, "the joint is removed");
    check(!world->destroy_joint(*joint), "a removed joint cannot be removed again");
    world->set_velocity(b, {5, 0, 0}, {});
    for (int i = 0; i < 30; ++i) {
        world->step(tick);
    }
    check(distance(world->state(a)->position, world->state(b)->position) > 1.5, "without the joint the bodies part");
}

// A weight on a hinged arm swings down toward a block that is the joint's own parent. Returns
// the furthest left the weight gets: through the block, or stopped by it.
double swing_into_parent(bool collide)
{
    auto world = make_world();
    const auto block = body(*world, physics::MotionType::Static, {0, 4, 0}, physics::BoxShape{{.3F, .3F, .3F}});
    const auto weight = body(*world, physics::MotionType::Dynamic, {1, 5, 0}, small_box);
    std::string error;
    physics::JointDesc desc;
    desc.parent = block;
    desc.child = weight;
    desc.kind = physics::HingeJoint{};
    desc.anchor = {0, 5, 0};
    desc.axis = {0, 0, 1};
    desc.collide = collide;
    check(world->create_joint(desc, error).has_value(), "a hinge to a block is made: " + error);
    double leftmost = 1;
    for (int i = 0; i < 180; ++i) {
        world->step(tick);
        leftmost = std::min(leftmost, world->state(weight)->position.x);
    }
    return leftmost;
}

void test_collision_between_jointed_bodies()
{
    const double through = swing_into_parent(false), stopped = swing_into_parent(true);
    check(through < -.5, "jointed bodies pass through each other by default: " + std::to_string(through));
    check(stopped > .3, "with collide set, a jointed body is stopped by its partner: " + std::to_string(stopped));

    // Two joints on one pair: removing one leaves the pair still not colliding; removing both
    // lets them collide again. A third body always collides with both.
    physics::WorldConfig config;
    config.gravity = {0, 0, 0};
    auto world = make_world(config);
    const auto a = body(*world, physics::MotionType::Dynamic, {0, 0, 0}, small_box);
    const auto b = body(*world, physics::MotionType::Dynamic, {.05, 0, 0}, small_box);
    std::string error;
    physics::JointDesc desc;
    desc.parent = a;
    desc.child = b;
    desc.anchor = {.025, 0, 0};
    const auto first = world->create_joint(desc, error);
    const auto second = world->create_joint(desc, error);
    check(first && second && world->joint_count() == 2, "two joints hold one pair: " + error);
    world->destroy_joint(*first);
    for (int i = 0; i < 30; ++i) {
        world->step(tick);
    }
    check(distance(world->state(a)->position, world->state(b)->position) < .06, "one of two joints still holds the pair quietly");
    world->destroy_joint(*second);
    for (int i = 0; i < 60; ++i) {
        world->step(tick);
    }
    check(distance(world->state(a)->position, world->state(b)->position) > .15, "with both joints gone the overlapping bodies push apart");

    auto other = make_world(config);
    const auto p = body(*other, physics::MotionType::Dynamic, {0, 0, 0}, small_box);
    const auto q = body(*other, physics::MotionType::Dynamic, {.5, 0, 0}, small_box);
    const auto stranger = body(*other, physics::MotionType::Dynamic, {0, 2, 0}, small_box, {0, -4, 0});
    desc.parent = p;
    desc.child = q;
    desc.anchor = {.25, 0, 0};
    check(other->create_joint(desc, error).has_value(), "a pair is jointed: " + error);
    for (int i = 0; i < 60; ++i) {
        other->step(tick);
    }
    // It strikes at 4 m/s and shares its momentum with the pair: the pair moves, it slows.
    check(other->state(p)->position.y < -.01 && other->state(stranger)->linear_velocity.y > -3.F,
          "a third body still hits a jointed pair: pair at " + std::to_string(other->state(p)->position.y) +
              ", striker at " + std::to_string(other->state(stranger)->linear_velocity.y) + " m/s");
}

void test_lifetime_and_refusals()
{
    auto world = make_world();
    const auto anchor = body(*world, physics::MotionType::Static, {0, 5, 0}, small_box);
    const auto wall = body(*world, physics::MotionType::Static, {3, 5, 0}, small_box);
    const auto a = body(*world, physics::MotionType::Dynamic, {0, 4, 0}, small_box);
    const auto b = body(*world, physics::MotionType::Dynamic, {0, 3, 0}, small_box);
    std::string error;
    physics::JointDesc desc;
    desc.parent = anchor;
    desc.child = a;
    desc.anchor = {0, 4.5, 0};
    const auto upper = world->create_joint(desc, error);
    desc.parent = a;
    desc.child = b;
    desc.anchor = {0, 3.5, 0};
    const auto lower = world->create_joint(desc, error);
    check(upper && lower && world->joint_count() == 2, "a chain of two joints is made: " + error);
    // The chain hangs still and goes to sleep; destroying the middle body takes both joints
    // with it and the lower body falls.
    for (int i = 0; i < 180; ++i) {
        world->step(tick);
    }
    check(std::abs(world->state(b)->position.y - 3) < .01, "the chain hangs");
    check(world->destroy_body(a) && !world->is_valid(*upper) && !world->is_valid(*lower) && world->joint_count() == 0,
          "destroying a body destroys its joints");
    for (int i = 0; i < 30; ++i) {
        world->step(tick);
    }
    check(world->state(b)->position.y < 2.5, "the freed body falls: " + std::to_string(world->state(b)->position.y));

    const auto refused = [&](physics::JointDesc bad, const std::string& what) {
        std::string why;
        check(!world->create_joint(bad, why).has_value() && !why.empty() && world->joint_count() == 0, what + " is refused");
    };
    physics::JointDesc good;
    good.parent = anchor;
    good.child = b;
    good.kind = physics::HingeJoint{};
    good.anchor = {0, 5, 0};
    auto bad = good;
    bad.child = a; // destroyed
    refused(bad, "a destroyed body");
    bad = good;
    bad.child = physics::BodyId{};
    refused(bad, "a default handle");
    bad = good;
    bad.parent = b;
    refused(bad, "the same body twice");
    bad = good;
    bad.child = wall;
    refused(bad, "two static bodies");
    bad = good;
    bad.anchor.x = std::numeric_limits<double>::quiet_NaN();
    refused(bad, "a non-finite anchor");
    bad = good;
    bad.axis = {0, 0, 0};
    refused(bad, "a zero axis");
    bad = good;
    bad.axis = {0, std::numeric_limits<float>::infinity(), 0};
    refused(bad, "a non-finite axis");
    bad = good;
    bad.kind = physics::HingeJoint{.1F, 1.F};
    refused(bad, "a hinge whose lower limit is above zero");
    bad = good;
    bad.kind = physics::HingeJoint{-1.F, 4.F};
    refused(bad, "a hinge limit beyond pi");
    bad = good;
    bad.kind = physics::SwingTwistJoint{-.1F, -.2F, .2F};
    refused(bad, "a negative swing cone");
    bad = good;
    bad.kind = physics::SwingTwistJoint{.4F, .1F, .2F};
    refused(bad, "a twist minimum above zero");
    bad = good;
    bad.kind = physics::SwingTwistJoint{.4F, -.2F, std::numeric_limits<float>::quiet_NaN()};
    refused(bad, "a twist limit that is not a number");
    auto foreign_world = make_world();
    const auto foreign = body(*foreign_world, physics::MotionType::Dynamic, {0, 0, 0}, small_box);
    bad = good;
    bad.child = foreign;
    refused(bad, "another world's body");
    // A fixed joint ignores its axis, so a zero axis is fine there.
    auto fixed = good;
    fixed.kind = physics::FixedJoint{};
    fixed.axis = {0, 0, 0};
    const auto made = world->create_joint(fixed, error);
    check(made.has_value(), "a fixed joint needs no axis: " + error);
    check(!foreign_world->is_valid(*made) && !foreign_world->destroy_joint(*made), "a joint handle means nothing to another world");
    check(!world->is_valid(physics::JointId{}) && !world->destroy_joint(physics::JointId{}), "the default joint handle is never valid");

    // The joint limit.
    physics::WorldConfig config;
    config.max_joints = 2;
    auto small = make_world(config);
    const auto s0 = body(*small, physics::MotionType::Static, {0, 0, 0}, small_box);
    const auto s1 = body(*small, physics::MotionType::Dynamic, {1, 0, 0}, small_box);
    physics::JointDesc link;
    link.parent = s0;
    link.child = s1;
    const auto one = small->create_joint(link, error);
    const auto two = small->create_joint(link, error);
    check(one && two && !small->create_joint(link, error).has_value() && !error.empty(), "a world refuses more than max_joints");
    check(small->destroy_joint(*one) && small->create_joint(link, error).has_value(), "a freed joint makes room for another");
    check(!small->is_valid(*one), "a reused joint slot does not revive the old handle");
    config.max_joints = 0;
    check(physics::World::create(config, error) == nullptr && !error.empty(), "a world with no room for joints is refused");
}

std::uint64_t run(bool jointed, bool limited = false)
{
    auto world = make_world();
    body(*world, physics::MotionType::Static, {0, -1, 0}, physics::BoxShape{{10, 1, 10}});
    const auto post = body(*world, physics::MotionType::Static, {0, 5, 0}, small_box);
    std::vector<physics::BodyId> links;
    for (int i = 0; i < 6; ++i) {
        links.push_back(body(*world, physics::MotionType::Dynamic, {.5 * (i + 1), 5, 0}, physics::CapsuleShape{.15F, .08F}));
    }
    if (jointed) {
        std::string error;
        for (std::size_t i = 0; i < links.size(); ++i) {
            physics::JointDesc desc;
            desc.parent = i == 0 ? post : links[i - 1];
            desc.child = links[i];
            desc.kind = limited ? physics::JointKind{physics::HingeJoint{-.5F, .5F}}
                                : physics::JointKind{physics::SwingTwistJoint{1.F, -.3F, .3F}};
            desc.anchor = {.5 * static_cast<double>(i) + .25, 5, 0};
            desc.axis = limited ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
            if (!world->create_joint(desc, error)) {
                check(false, "a chain link is made: " + error);
            }
        }
    }
    for (int i = 0; i < 300; ++i) {
        world->step(tick);
    }
    return world->state_hash();
}

void test_determinism()
{
    const auto chain = run(true);
    check(chain == run(true), "a jointed chain repeats bit for bit");
    check(chain != run(false), "joints change the simulation");
    check(chain != run(true, true), "the kind of joint changes the simulation");
    // The hash covers which bodies a joint holds, even before a step.
    auto a = make_world(), b = make_world();
    for (auto* world : {a.get(), b.get()}) {
        const auto p = body(*world, physics::MotionType::Static, {0, 0, 0}, small_box);
        const auto q = body(*world, physics::MotionType::Dynamic, {1, 0, 0}, small_box);
        if (world == a.get()) {
            std::string error;
            physics::JointDesc desc;
            desc.parent = p;
            desc.child = q;
            check(world->create_joint(desc, error).has_value(), "a joint is made: " + error);
        }
    }
    check(a->state_hash() != b->state_hash(), "the state hash tells a jointed world from an unjointed one");
}
} // namespace

int main()
{
    test_hinge();
    test_swing_twist();
    test_fixed();
    test_collision_between_jointed_bodies();
    test_lifetime_and_refusals();
    test_determinism();
    if (failures != 0) {
        std::cerr << failures << " physics joint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "physics joints passed\n";
    return EXIT_SUCCESS;
}
