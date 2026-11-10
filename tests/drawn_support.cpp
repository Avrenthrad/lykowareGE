// animation.drawn_support (ADR 0069): feet stay still on a moving support as it is drawn.
// Supports are drawn between their last two physics steps; the foot is drawn with the actor, also
// between steps. A planted anchor resolved through the support's latest physics state would lead
// the drawn support by up to one step of its motion, so the foot would slide back and forth on it
// from frame to frame. Resolving through the drawn pose the caller supplies keeps it still.

#include "support/stance_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <lykoware/foot_plant.hpp>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

namespace {
namespace a = lykoware::animation;
namespace m = lykoware::math;
namespace p = lykoware::physics;
using V = m::DVec3;
using foot_placement::norm;
constexpr float tick = 1.F / 60.F;
constexpr std::array<float, 4> alphas{.2F, .45F, .7F, .95F};

int failures = 0;
void check(bool ok, const std::string& why)
{
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        ++failures;
    }
}
std::string text(V v)
{
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

// The renderer's blend between two physics states (ADR 0048): linear position, normalised linear
// rotation along the shorter way.
a::DrawnBody blend(p::BodyId body, const p::BodyState& before, const p::BodyState& after, float t)
{
    const auto& q = before.rotation;
    auto r = after.rotation;
    if (q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w < 0.F) {
        r = {-r.x, -r.y, -r.z, -r.w};
    }
    const auto s = static_cast<double>(t);
    return {body,
            {before.position.x + (after.position.x - before.position.x) * s,
             before.position.y + (after.position.y - before.position.y) * s,
             before.position.z + (after.position.z - before.position.z) * s},
            m::normalize(m::Quat{q.x + (r.x - q.x) * t, q.y + (r.y - q.y) * t, q.z + (r.z - q.z) * t,
                                 q.w + (r.w - q.w) * t})};
}
// A point relative to a drawn support, in the support's coordinates.
V on(const a::DrawnBody& support, V point)
{
    return foot_placement::rotate(m::conjugate(support.rotation), point - support.position);
}

std::unique_ptr<p::World> make_world()
{
    std::string error;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}
p::BodyId add_box(p::World& world, p::MotionType motion, V position, m::Quat rotation = {})
{
    std::string error;
    p::BodyDesc box;
    box.motion = motion;
    box.shape = p::BoxShape{{5, .5F, 5}};
    box.position = position;
    box.rotation = rotation;
    const auto id = world.create_body(box, error);
    if (!id) {
        std::cerr << "FATAL: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return *id;
}

void test_drawn_hit()
{
    // A tilted support whose drawn pose is moved and turned a little from its physics pose. The
    // drawn hit must match a real probe of the same box placed at the drawn pose.
    const auto physical_rotation = m::from_axis_angle({1, 0, 0}, .2F);
    auto world = make_world();
    const auto id = add_box(*world, p::MotionType::Kinematic, {0, -.5, 0}, physical_rotation);
    const a::DrawnBody drawn{id, {.05, -.52, .03}, m::from_axis_angle({0, 1, 0}, .05F) * m::from_axis_angle({1, 0, 0}, .18F)};
    auto oracle = make_world();
    add_box(*oracle, p::MotionType::Static, drawn.position, drawn.rotation);
    const V from{.3, .1, .4};
    const auto hit = world->cast_ray(from + V{0, .35, 0}, {0, -1, 0});
    const auto expected = oracle->cast_ray(from + V{0, .35, 0}, {0, -1, 0});
    const auto state = world->state(id);
    check(hit && expected && state, "the support and its drawn twin are probed");
    if (!hit || !expected || !state) {
        return;
    }
    const auto moved = a::drawn_hit(*hit, *state, drawn, from);
    check(moved && norm(moved->point - expected->point) < 1e-5 && m::length(moved->normal - expected->normal) < 1e-5F &&
              moved->body == id,
          "a drawn hit matches a probe of the support placed where it is drawn: " +
              (moved ? text(moved->point) : std::string{"none"}) + " vs " + text(expected->point));
    check(moved && std::abs(moved->point.x - from.x) < 1e-12 && std::abs(moved->point.z - from.z) < 1e-12,
          "the drawn hit lies straight below the probe");

    // Drawn where it is simulated, the hit is unchanged.
    const auto same = a::drawn_hit(*hit, *state, a::DrawnBody{id, state->position, state->rotation}, from);
    check(same && norm(same->point - hit->point) < 1e-6 && m::length(same->normal - hit->normal) < 1e-6F,
          "a support drawn at its physics pose keeps its hit");

    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    check(!a::drawn_hit(*hit, *state, a::DrawnBody{id, {nan, 0, 0}, {}}, from), "a non-finite drawn position is refused");
    check(!a::drawn_hit(*hit, *state, a::DrawnBody{id, {}, {0, 0, 0, 0}}, from), "a zero drawn rotation is refused");
    check(!a::drawn_hit(*hit, *state, a::DrawnBody{id, {}, {inf, 0, 0, 1}}, from), "a non-finite drawn rotation is refused");
    check(!a::drawn_hit(*hit, *state, drawn, {nan, 0, 0}), "a non-finite probe origin is refused");

    const std::array<a::DrawnBody, 3> list{{{p::BodyId{9, 9, 9}, {}, {}}, {id, {1, 0, 0}, {}}, {id, {2, 0, 0}, {}}}};
    const auto* found = a::find_drawn(list, id);
    check(found != nullptr && found->position.x == 1, "the first entry for a body is used");
    check(a::find_drawn(list, p::BodyId{1, 2, 3}) == nullptr && a::find_drawn({}, id) == nullptr,
          "an unlisted body has no drawn pose");
}

// A support moving at `linear` and `angular`, stepped at 60 Hz and drawn at four alphas per step.
// Each frame `frame(drawn support, with drawn list)` is called after the plant has been given the
// frame's drawn pose.
struct Ride {
    std::unique_ptr<p::World> world = make_world();
    p::BodyId id = add_box(*world, p::MotionType::Kinematic, {0, -.5, 0});
};

void test_held_anchor()
{
    std::string error;
    // A platform moving sideways at 1.5 m/s and one rising at 0.6 m/s.
    for (const m::Vec3 velocity : {m::Vec3{1.5F, 0, 0}, m::Vec3{0, .6F, 0}}) {
        Ride ride;
        check(ride.world->set_velocity(ride.id, velocity, {}), "the support moves");
        auto drawn_plant = a::FootPlant::create({}, error);
        auto latest_plant = a::FootPlant::create({}, error);
        const V start_foot{.2, .03, .1};
        std::optional<V> held;
        double spread = 0;
        double control_low = std::numeric_limits<double>::max();
        double control_high = std::numeric_limits<double>::lowest();
        bool resolved = true;
        bool always_drawn = true;
        a::DrawnBody first{};
        for (int step = 0; step < 6; ++step) {
            const auto before = *ride.world->state(ride.id);
            resolved = ride.world->step(tick) && resolved;
            const auto after = *ride.world->state(ride.id);
            for (const float alpha : alphas) {
                const auto support = blend(ride.id, before, after, alpha);
                if (!held) {
                    first = support;
                }
                // The foot is drawn with the actor, which rides the drawn support.
                const V foot = start_foot + (support.position - first.position);
                const std::array<a::DrawnBody, 1> list{support};
                a::FootPlantResult result;
                a::FootPlantResult latest;
                resolved = drawn_plant->update(*ride.world, foot, true, result, error, list) && result.locked() &&
                           latest_plant->update(*ride.world, foot, true, latest, error) && latest.locked() && resolved;
                always_drawn = always_drawn && result.drawn && !latest.drawn;
                const V local = on(support, result.target);
                if (!held) {
                    held = local;
                    check(std::abs(result.target.x - foot.x) < 1e-9 && std::abs(result.target.z - foot.z) < 1e-9 &&
                              std::abs(result.target.y - (support.position.y + .5 + .03)) < 1e-5,
                          "a fresh plant lands straight below the foot on the support as drawn: " + text(result.target));
                }
                spread = std::max(spread, norm(local - *held));
                const V control = on(support, latest.target);
                const double along = velocity.x != 0 ? control.x : control.y;
                control_low = std::min(control_low, along);
                control_high = std::max(control_high, along);
            }
        }
        const double one_step = static_cast<double>(m::length(velocity) * tick);
        check(resolved && always_drawn, "every frame resolves, against the drawn pose only when one is listed");
        check(spread < 1e-6, "the planted foot stays still on the support as drawn: moved " + std::to_string(spread));
        // Positive control: through the latest physics state the foot slides on the drawn support by
        // most of a step's motion (alphas 0.2 to 0.95 cover three quarters of it).
        check(control_high - control_low > .7 * one_step,
              "without drawn poses the foot slides on the drawn support: " + std::to_string(control_high - control_low));
    }
}

void test_turning_support()
{
    std::string error;
    Ride ride;
    check(ride.world->set_velocity(ride.id, {}, {0, 2, 0}), "the support turns");
    auto plant = a::FootPlant::create({.03, 1, 35}, error);
    std::optional<a::DrawnBody> first;
    bool followed = true;
    double worst = 0;
    for (int step = 0; step < 6; ++step) {
        const auto before = *ride.world->state(ride.id);
        followed = ride.world->step(tick) && followed;
        const auto after = *ride.world->state(ride.id);
        for (const float alpha : alphas) {
            const auto support = blend(ride.id, before, after, alpha);
            if (!first) {
                first = support;
            }
            const std::array<a::DrawnBody, 1> list{support};
            // The foot rides the drawn support, which turns about its centre half a metre below the top.
            const V foot = first->position + foot_placement::rotate(support.rotation * m::conjugate(first->rotation), V{.4, .53, 0});
            a::FootPlantResult result;
            followed = plant->update(*ride.world, foot, m::Vec3{0, 0, -1}, true, result, error, list) && result.heading &&
                       followed;
            const auto turn = support.rotation * m::conjugate(first->rotation);
            if (result.heading) {
                worst = std::max(worst, static_cast<double>(m::length(*result.heading - m::rotate(turn, m::Vec3{0, 0, -1}))));
                worst = std::max(worst, static_cast<double>(m::length(result.normal - m::rotate(support.rotation, m::Vec3{0, 1, 0}))));
                worst = std::max(worst, norm(on(support, result.target) - on(*first, V{.4, .03, 0})));
            }
        }
    }
    check(followed && worst < 1e-5,
          "on a turning support the foot's place, normal and heading turn with it as drawn: " + std::to_string(worst));
}

void test_compatibility_and_refusals()
{
    std::string error;
    Ride ride;
    check(ride.world->set_velocity(ride.id, {1, 0, 0}, {0, .5F, 0}), "the support moves and turns");
    auto plain = a::FootPlant::create({}, error);
    auto unlisted = a::FootPlant::create({}, error);
    auto empty = a::FootPlant::create({}, error);
    const std::array<a::DrawnBody, 1> other{{{p::BodyId{77, 1, 1}, {}, {0, 0, 0, 0}}}};
    bool same = true;
    for (int i = 0; i < 10; ++i) {
        same = ride.world->step(tick) && same;
        a::FootPlantResult x;
        a::FootPlantResult y;
        a::FootPlantResult z;
        same = plain->update(*ride.world, {.1, .03, 0}, true, x, error) &&
               unlisted->update(*ride.world, {.1, .03, 0}, true, y, error, other) &&
               empty->update(*ride.world, {.1, .03, 0}, true, z, error, {}) && same;
        same = same && x.target.x == y.target.x && x.target.y == y.target.y && x.target.z == y.target.z &&
               x.target.x == z.target.x && x.target.y == z.target.y && x.target.z == z.target.z &&
               x.normal.x == y.normal.x && x.normal.y == y.normal.y && !y.drawn && !z.drawn;
    }
    check(same, "unlisted supports, unusable entries for other bodies and an empty list behave exactly as before");

    // A listed pose that is used must be usable; refusal keeps state and output.
    const auto state = *ride.world->state(ride.id);
    const std::array<a::DrawnBody, 1> good{{{ride.id, state.position, state.rotation}}};
    const std::array<a::DrawnBody, 1> bad{{{ride.id, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, state.rotation}}};
    const std::array<a::DrawnBody, 1> zero{{{ride.id, state.position, {0, 0, 0, 0}}}};
    auto fresh = a::FootPlant::create({}, error);
    a::FootPlantResult sentinel;
    sentinel.target = {7, 7, 7};
    auto out = sentinel;
    check(!fresh->update(*ride.world, {.1, .03, 0}, true, out, error, zero) && !error.empty() && out.target.x == 7,
          "a fresh plant on a listed support with a zero rotation is refused, output kept");
    check(fresh->update(*ride.world, {.1, .03, 0}, true, out, error, good) && out.acquired && out.drawn,
          "after the refusal the foot still plants (state kept)");
    out = sentinel;
    check(!fresh->update(*ride.world, {.1, .03, 0}, true, out, error, bad) && out.target.x == 7 && fresh->locked(),
          "a held support with a non-finite drawn position is refused, output and anchor kept");
    check(fresh->update(*ride.world, {.1, .03, 0}, true, out, error, good) && out.locked() && !out.acquired,
          "the anchor is still held after the refusal");

    // The physics world still decides whether a support exists.
    check(ride.world->destroy_body(ride.id), "the support is destroyed");
    check(fresh->update(*ride.world, {.1, .03, 0}, true, out, error, good) && out.released &&
              out.status == a::FootPlantStatus::SupportLost,
          "a destroyed support is lost even while it is still listed as drawn");
}

void test_allocations()
{
    std::string error;
    Ride ride;
    check(ride.world->set_velocity(ride.id, {.5F, 0, 0}, {}), "the support moves");
    auto plant = a::FootPlant::create({}, error);
    std::array<a::DrawnBody, 8> list{};
    for (std::uint32_t i = 0; i < list.size(); ++i) {
        list[i] = {p::BodyId{100 + i, 1, 1}, {}, {}};
    }
    a::FootPlantResult result;
    const auto start = *ride.world->state(ride.id);
    list.back() = {ride.id, start.position, start.rotation};
    check(plant->update(*ride.world, {0, .03, 0}, m::Vec3{1, 0, 0}, true, result, error, list) && result.drawn,
          "the allocation fixture plants");
    bool ok = true;
    allocations = 0;
    tracking = true;
    for (int i = 0; i < 1000; ++i) {
        list.back().position.x = start.position.x + i * 1e-4;
        ok = plant->update(*ride.world, {i * 1e-4, .03, 0}, m::Vec3{1, 0, 0}, true, result, error, list) && result.drawn &&
             ok;
    }
    tracking = false;
    check(ok && allocations == 0, "1,000 drawn-support updates allocate nothing: " + std::to_string(allocations));
}

// The ankle of each leg in world space.
std::array<V, 2> ankles(const a::Skeleton& skeleton, const a::Pose& pose, const foot_placement::Placement& actor)
{
    std::vector<m::Mat4> fk;
    a::global_transforms(skeleton, pose, fk);
    return {foot_placement::to_world(actor, foot_placement::position(fk[3])),
            foot_placement::to_world(actor, foot_placement::position(fk[6]))};
}

void test_controller()
{
    std::string error;
    const auto asset = stance_fixture::make();
    const auto rest = a::rest_pose(asset.skeleton);
    const rig_roles::Indices roles{{0, 1, 2, 3, 4, 5, 6}};

    // Standing: a lift whose physics top is 5 cm above where it is drawn. Listed as drawn, the
    // feet stand exactly as on a still floor at the drawn height.
    {
        auto lift = make_world();
        const auto lift_id = add_box(*lift, p::MotionType::Kinematic, {0, -.45, 0});
        auto still = make_world();
        add_box(*still, p::MotionType::Static, {0, -.5, 0});
        const std::array<a::DrawnBody, 1> list{{{lift_id, {0, -.5, 0}, {}}}};
        foot_placement::Controller on_lift;
        foot_placement::Controller on_floor;
        foot_placement::Controller latest;
        check(on_lift.configure(asset.skeleton, roles, error) && on_floor.configure(asset.skeleton, roles, error) &&
                  latest.configure(asset.skeleton, roles, error),
              "stance controllers prepare");
        a::Pose lifted;
        a::Pose floored;
        a::Pose ahead;
        double worst = 0;
        double control = 0;
        bool ran = true;
        for (int i = 0; i < 30; ++i) {
            ran = on_lift.update(rest, {}, *lift, .02F, true, lifted, error, std::nullopt, list) &&
                  on_floor.update(rest, {}, *still, .02F, true, floored, error) &&
                  latest.update(rest, {}, *lift, .02F, true, ahead, error) && ran;
            const auto x = ankles(asset.skeleton, lifted, {});
            const auto y = ankles(asset.skeleton, floored, {});
            const auto z = ankles(asset.skeleton, ahead, {});
            worst = std::max({worst, norm(x[0] - y[0]), norm(x[1] - y[1])});
            control = std::max({control, norm(z[0] - y[0]), norm(z[1] - y[1])});
        }
        check(ran && worst < 1e-5, "standing feet meet the lift where it is drawn: " + std::to_string(worst));
        check(control > .02, "positive control: through its physics state the feet stand on the lift's later top: " +
                                  std::to_string(control));
        check(on_lift.stats().drawn >= 60 && on_floor.stats().drawn == 0 && latest.stats().drawn == 0,
              "drawn-support probes are counted");
    }

    // Walking: both feet planted on a platform moving at 1.5 m/s, the actor riding it as drawn.
    lykoware::character::FootSettings settings;
    settings.locking = true;
    settings.max_plant_distance = 1;
    const foot_placement::PlantPhases both{{true, true}};
    for (const bool listed : {true, false}) {
        Ride ride;
        check(ride.world->set_velocity(ride.id, {1.5F, 0, 0}, {}), "the platform moves");
        foot_placement::Controller walker;
        check(walker.configure(asset.skeleton, roles, error, .03, settings), "walking controller prepares: " + error);
        std::optional<a::DrawnBody> first;
        std::array<V, 2> held{};
        double low = std::numeric_limits<double>::max();
        double high = std::numeric_limits<double>::lowest();
        bool ran = true;
        a::Pose output;
        for (int step = 0; step < 6; ++step) {
            const auto before = *ride.world->state(ride.id);
            ran = ride.world->step(tick) && ran;
            const auto after = *ride.world->state(ride.id);
            for (const float alpha : alphas) {
                const auto support = blend(ride.id, before, after, alpha);
                if (!first) {
                    first = support;
                }
                foot_placement::Placement actor;
                actor.feet = support.position - first->position;
                const std::array<a::DrawnBody, 1> list{support};
                ran = walker.update(rest, actor, *ride.world, tick / 4, true, output, error, both,
                                    listed ? std::span<const a::DrawnBody>{list} : std::span<const a::DrawnBody>{}) &&
                      ran;
                const auto feet = ankles(asset.skeleton, output, actor);
                for (std::size_t side = 0; side < 2; ++side) {
                    const V local = on(support, feet[side]);
                    if (step == 0 && alpha == alphas[0]) {
                        held[side] = local;
                    }
                    low = std::min(low, local.x - held[side].x);
                    high = std::max(high, local.x - held[side].x);
                }
            }
        }
        check(ran && walker.stats().planted == 2, "both feet plant on the moving platform");
        if (listed) {
            check(high - low < 1e-4 && walker.stats().drawn >= 48,
                  "planted feet stay still on the platform as drawn: " + std::to_string(high - low));
        } else {
            check(high - low > .7 * 1.5 * static_cast<double>(tick),
                  "positive control: without drawn poses the planted feet slide on it: " + std::to_string(high - low));
        }
    }
}
} // namespace

int main()
{
    test_drawn_hit();
    test_held_anchor();
    test_turning_support();
    test_compatibility_and_refusals();
    test_allocations();
    test_controller();
    if (failures != 0) {
        std::cerr << failures << " drawn support assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "drawn support passed\n";
    return EXIT_SUCCESS;
}
