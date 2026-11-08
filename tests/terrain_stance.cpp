#include "foot_placement.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
int main()
{
    namespace a = lykoware::animation;
    namespace m = lykoware::math;
    namespace p = lykoware::physics;
    using V = m::DVec3;
    int failures = 0;
    auto check = [&](bool ok, const char* why) {
        if (!ok) {
            std::cerr << "FAIL: " << why << '\n';
            ++failures;
        }
    };
    std::string error;
    a::Skeleton rig;
    const std::array<m::Vec3, 7> offsets{
        {{0, 1.6F, 0}, {-.2F, 0, 0}, {0, -.8F, 0}, {0, -.8F, 0}, {.2F, 0, 0}, {0, -.8F, 0}, {0, -.8F, 0}}};
    const std::array<std::uint16_t, 7> parents{{0, 0, 1, 2, 0, 4, 5}};
    for (std::size_t i = 0; i < 7; ++i) {
        a::Joint joint;
        joint.name = rig_roles::names[i];
        joint.rest.translation = offsets[i];
        if (i) {
            joint.parent = parents[i];
        }
        rig.joints.push_back(joint);
    }
    const rig_roles::Indices roles{{0, 1, 2, 3, 4, 5, 6}};
    const auto rest = a::rest_pose(rig);
    lykoware::character::FootSettings settings;
    settings.align_normals = true;
    settings.pelvis = true;
    auto world = p::World::create({}, error);
    if (!world) {
        std::cerr << error;
        return 1;
    }
    const float slope = 20.F * std::numbers::pi_v<float> / 180.F;
    p::BodyDesc ground;
    ground.motion = p::MotionType::Static;
    ground.shape = p::BoxShape{{5, .5F, 5}};
    ground.rotation = m::from_axis_angle({0, 0, 1}, slope);
    ground.position = {0, -.5 / std::cos(slope), 0};
    auto body = world->create_body(ground, error);
    if (!body) {
        std::cerr << error;
        return 1;
    }
    const auto ray = world->cast_ray({0, 2, 0}, {0, -4, 0});
    const auto normal = m::rotate(ground.rotation, m::Vec3{0, 1, 0});
    check(ray && m::length(ray->normal - normal) < 1e-5F && std::abs(m::length(ray->normal) - 1) < 1e-5F,
          "read-only Jolt ray exposes rotated face normal");
    check(!world->cast_ray({0, 2, 0}, {0, std::numeric_limits<float>::infinity(), 0}),
          "nonfinite ray direction refused");
    foot_placement::Controller controller;
    check(controller.configure(rig, roles, error, .03, settings),
          "stance controller prepares both ankle axes");
    a::Pose output;
    const auto hash = world->state_hash();
    for (int i = 0; i < 120; ++i) {
        check(controller.update(rest, {}, *world, 1.F / 60.F, true, output, error), "slope stance update");
    }
    std::vector<m::Mat4> fk;
    a::global_transforms(rig, output, fk);
    for (std::size_t foot : {std::size_t{3}, std::size_t{6}}) {
        const m::Vec3 up{fk[foot].m[1][0], fk[foot].m[1][1], fk[foot].m[1][2]};
        check(m::length(m::normalize(up) - normal) < .005F, "both ankle axes align with real slope normal");
        const double expected = std::tan(slope) * fk[foot].m[3][0] + .03;
        check(std::abs(fk[foot].m[3][1] - expected) < .002,
              "position IK retains slope clearance after ankle rotation");
    }
    check(controller.stats().oriented == 240 && controller.stats().pelvis_frames > 0 &&
              world->state_hash() == hash,
          "orientation/pelvis counters are real and physics stays unchanged");
    foot_placement::Controller at30;
    check(at30.configure(rig, roles, error, .03, settings), "30 Hz stance setup");
    a::Pose pose30;
    for (int i = 0; i < 60; ++i) {
        check(at30.update(rest, {}, *world, 1.F / 30.F, true, pose30, error), "30 Hz stance solve");
    }
    std::vector<m::Mat4> fk30;
    a::global_transforms(rig, pose30, fk30);
    check(std::abs(at30.pelvis_offset() - controller.pelvis_offset()) < 1e-5 &&
              std::abs(fk30[3].m[1][0] - fk[3].m[1][0]) < .002,
          "equal-time stance smoothing converges at 30/60 Hz");
    const auto held = output;
    const auto saved_stats = controller.stats();
    const double saved_pelvis = controller.pelvis_offset();
    auto invalid = rest;
    invalid[6].translation.y = -.5F;
    check(!controller.update(invalid, {}, *world, 1.F / 60.F, true, output, error) &&
              controller.stats().contacts == saved_stats.contacts &&
              controller.pelvis_offset() == saved_pelvis && output[3].rotation.w == held[3].rotation.w,
          "second-leg refusal preserves bilateral output and all persistent state");
    check(controller.update(rest, {}, *world, .01F, false, output, error) &&
              controller.pelvis_offset() == 0 && output[3].rotation.w == rest[3].rotation.w,
          "inactive state resets orientation and pelvis");
    check(world->destroy_body(*body), "slope body removed");
    ground.rotation = {};
    ground.position = {0, -.63, 0};
    body = world->create_body(ground, error);
    if (!body) {
        std::cerr << error;
        return 1;
    }
    foot_placement::Controller dropped, legacy;
    check(dropped.configure(rig, roles, error, .03, settings) && legacy.configure(rig, roles, error),
          "depressed-ground controllers prepare");
    a::Pose legacy_pose;
    for (int i = 0; i < 90; ++i) {
        check(dropped.update(rest, {}, *world, 1.F / 60.F, true, output, error) &&
                  legacy.update(rest, {}, *world, 1.F / 60.F, true, legacy_pose, error),
              "depressed-ground solves");
    }
    a::global_transforms(rig, output, fk);
    a::global_transforms(rig, legacy_pose, fk30);
    check(std::abs(dropped.pelvis_offset() + .1) < .001 && std::abs(fk[3].m[3][1] + .1F) < .001 &&
              std::abs(fk30[3].m[3][1]) < .001,
          "bounded pelvis drop reaches lower ground without stretching unlike straight-leg clamping");
    auto bounded = settings;
    bounded.max_pelvis_drop = .04;
    foot_placement::Controller limited;
    check(limited.configure(rig, roles, error, .03, bounded), "explicit drop cap prepares");
    for (int i = 0; i < 90; ++i) {
        check(limited.update(rest, {}, *world, 1.F / 60.F, true, output, error), "bounded drop solve");
    }
    check(limited.pelvis_offset() >= -.040001 && limited.stats().clamped > 0,
          "pelvis never exceeds authored bound; unreachable targets report clamp");
    dropped.reset();
    foot_placement::Placement edge;
    edge.feet.x = 4.9;
    check(dropped.update(rest, edge, *world, .01F, true, output, error) && dropped.pelvis_offset() == 0,
          "missing one contact clears body compensation");
    check(world->destroy_body(*body), "depression removed");
    ground.rotation = m::from_axis_angle({1, 0, 0}, std::numbers::pi_v<float> / 3);
    ground.position = {0, -.5 / std::cos(std::numbers::pi_v<float> / 3), 0};
    body = world->create_body(ground, error);
    if (!body) {
        std::cerr << error;
        return 1;
    }
    foot_placement::Controller steep;
    check(steep.configure(rig, roles, error, .03, settings) &&
              steep.update(rest, {}, *world, .01F, true, output, error),
          "steep contact classified safely");
    check(steep.stats().slope_rejected == 2 && steep.stats().oriented == 0 && steep.pelvis_offset() == 0,
          "steep ground cannot tilt feet or lower pelvis");
    auto pelvis_only = settings;
    pelvis_only.align_normals = false;
    foot_placement::Controller steep_pelvis;
    check(steep_pelvis.configure(rig, roles, error, .03, pelvis_only) &&
              steep_pelvis.update(rest, {}, *world, .01F, true, output, error) &&
              steep_pelvis.stats().slope_rejected == 2,
          "pelvis-only mode still rejects unsupported surface slopes");
    check(world->destroy_body(*body), "steep body removed");
    ground.rotation = {};
    ground.position = {0, -.5, 0};
    body = world->create_body(ground, error);
    if (!body) {
        return 1;
    }
    p::BodyDesc sphere;
    sphere.motion = p::MotionType::Static;
    sphere.shape = p::SphereShape{.2F};
    sphere.position = {2, 2, 2};
    const auto sphere_body = world->create_body(sphere, error);
    if (!sphere_body) {
        return 1;
    }
    const auto inside_sphere = world->cast_ray({2, 2, 2}, {1, 0, 0});
    const auto inside_box = world->cast_ray({0, -.5, 0}, {1, 0, 0});
    check(inside_sphere && inside_sphere->fraction == 0 &&
              std::abs(m::length(inside_sphere->normal) - 1) < 1e-5F && inside_box &&
              inside_box->fraction == 0,
          "inside-solid ray hit semantics remain intact with representative normals");
    foot_placement::Controller reused;
    check(reused.configure(rig, roles, error, .03, settings) &&
              reused.update(rest, {}, *world, .01F, true, output, error),
          "allocation fixture warmup");
    allocations = 0;
    tracking = true;
    auto control = rest;
    tracking = false;
    check(allocations > 0 && control.size() == 7, "allocation control observes setup");
    allocations = 0;
    tracking = true;
    bool success = true;
    for (int i = 0; i < 1000; ++i) {
        success = reused.update(rest, {}, *world, .01F, true, output, error) && success;
    }
    tracking = false;
    check(success && allocations == 0, "1000 complete stance updates reuse ordinary C++ storage");
    std::cout << "stance_updates=1000 allocations=" << allocations << '\n';
    auto terrain_world = p::World::create({}, error);
    if (!terrain_world) {
        return 1;
    }
    p::HeightFieldShape field;
    field.samples_per_side = 4;
    field.spacing = 1;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            field.heights.push_back(.1F * static_cast<float>(x) + .05F * static_cast<float>(z));
        }
    }
    p::BodyDesc terrain;
    terrain.motion = p::MotionType::Static;
    terrain.position = {-1.5, -.225, -1.5};
    terrain.shape = field;
    const auto terrain_body = terrain_world->create_body(terrain, error);
    if (!terrain_body) {
        std::cerr << error;
        return 1;
    }
    const auto terrain_hit = terrain_world->cast_ray({.2, 2, .1}, {0, -4, 0});
    const auto terrain_normal = m::normalize(m::Vec3{-.1F, 1, -.05F});
    const auto terrain_x = terrain_world->cast_ray({.22, 2, .1}, {0, -4, 0});
    const auto terrain_z = terrain_world->cast_ray({.2, 2, .12}, {0, -4, 0});
    bool geometry_matches = false;
    if (terrain_hit && terrain_x && terrain_z) {
        const auto tangent_x = terrain_x->point - terrain_hit->point,
                   tangent_z = terrain_z->point - terrain_hit->point;
        const auto geometric = foot_placement::cross(tangent_z, tangent_x);
        const auto unit = foot_placement::scale(geometric, 1.0 / foot_placement::norm(geometric));
        geometry_matches = foot_placement::norm(unit - V{terrain_hit->normal.x, terrain_hit->normal.y,
                                                         terrain_hit->normal.z}) < .002;
        if (!geometry_matches) {
            std::cerr << "heightfield normal=" << terrain_hit->normal.x << "," << terrain_hit->normal.y << ","
                      << terrain_hit->normal.z << " geometry=" << unit.x << "," << unit.y << "," << unit.z
                      << '\n';
        }
    }
    check(geometry_matches,
          "heightfield triangle normal agrees with independent rays on the actual compressed surface");
    foot_placement::Controller uneven;
    check(uneven.configure(rig, roles, error, .03, settings), "heightfield stance setup");
    for (int i = 0; i < 90; ++i) {
        check(uneven.update(rest, {}, *terrain_world, 1.F / 60.F, true, output, error),
              "heightfield stance solve");
    }
    a::global_transforms(rig, output, fk);
    check(m::length(m::normalize(m::Vec3{fk[3].m[1][0], fk[3].m[1][1], fk[3].m[1][2]}) - terrain_normal) <
              .01,
          "ankles align to real heightfield collision");
    auto parented = rig;
    a::Joint ancestor;
    ancestor.name = "ancestor";
    ancestor.rest.rotation = m::from_axis_angle({0, 0, 1}, .2F);
    parented.joints.insert(parented.joints.begin(), ancestor);
    for (std::size_t i = 1; i < parented.joints.size(); ++i) {
        if (parented.joints[i].parent) {
            parented.joints[i].parent = static_cast<std::uint16_t>(*parented.joints[i].parent + 1);
        }
    }
    parented.joints[1].parent = std::uint16_t{0};
    parented.joints[1].rest.translation =
        m::rotate(m::conjugate(ancestor.rest.rotation), m::Vec3{0, 1.6F, 0});
    parented.joints[1].rest.rotation = m::conjugate(ancestor.rest.rotation);
    const auto parented_rest = a::rest_pose(parented);
    const rig_roles::Indices parented_roles{{1, 2, 3, 4, 5, 6, 7}};
    foot_placement::Controller parented_controller;
    check(parented_controller.configure(parented, parented_roles, error, .03, settings),
          "pelvis under rotated ancestor prepares");
    check(world->destroy_body(*body), "flat surface removed for parented fixture");
    ground.position = {0, -.63, 0};
    body = world->create_body(ground, error);
    if (!body) {
        return 1;
    }
    for (int i = 0; i < 90; ++i) {
        check(parented_controller.update(parented_rest, {}, *world, 1.F / 60.F, true, output, error),
              "parent-local pelvis correction");
    }
    a::global_transforms(parented, output, fk);
    check(std::abs(fk[4].m[3][1] + .1F) < .002 &&
              std::abs(output[1].translation.x - parented_rest[1].translation.x) > .01F,
          "world-down pelvis offset is correctly converted through posed parent");
    auto maximum_drop = settings;
    maximum_drop.max_pelvis_drop = .3;
    foot_placement::Controller small;
    foot_placement::Placement small_actor;
    small_actor.uniform_scale = .1;
    check(small.configure(rig, roles, error, .03, maximum_drop), "small visual rig prepares");
    for (int i = 0; i < 90; ++i) {
        check(small.update(rest, small_actor, *world, 1.F / 60.F, true, output, error),
              "scaled reach bound update");
    }
    check(small.pelvis_offset() >= -.040001 && small.pelvis_offset() < -.039,
          "pelvis drop also respects calibrated world leg length");
    for (const float time : std::array<float, 3>{{-1.F, .3F, std::numeric_limits<float>::quiet_NaN()}}) {
        check(small.update(rest, small_actor, *world, time, true, output, error) &&
                  small.pelvis_offset() == 0,
              "invalid/long time clears all stance state");
    }
    small_actor.feet.x = 2;
    check(small.update(rest, small_actor, *world, .01F, true, output, error) && small.pelvis_offset() == 0,
          "teleport clears stance state without reusing old compensation");
    auto far_world = p::World::create({}, error);
    if (!far_world) {
        return 1;
    }
    ground.rotation = m::from_axis_angle({0, 0, 1}, slope);
    ground.position = {64000, -.5 / std::cos(slope), -64000};
    const auto far_body = far_world->create_body(ground, error);
    if (!far_body) {
        return 1;
    }
    foot_placement::Controller far;
    check(far.configure(rig, roles, error, .03, settings), "far/fitted rig prepares");
    const foot_placement::Placement actor{
        {64000, 0, -64000}, m::from_axis_angle({0, 1, 0}, .8F), 1.5, {.1, 0, 0}};
    for (int i = 0; i < 120; ++i) {
        check(far.update(rest, actor, *far_world, 1.F / 60.F, true, output, error),
              "far fitted/yawed stance update");
    }
    a::global_transforms(rig, output, fk);
    const auto oriented =
        foot_placement::rotate(actor.rotation, V{fk[3].m[1][0], fk[3].m[1][1], fk[3].m[1][2]});
    check(foot_placement::norm(oriented - V{normal.x, normal.y, normal.z}) < .01,
          "normal conversion ignores visual scale and respects yaw/far-origin placement");
    lykoware::character::Profile profile_;
    check(
        lykoware::character::decode_profile(
            R"({"version":1,"model":"hero.glb","bones":{"pelvis":"p","left_hip":"lh","left_knee":"lk","left_foot":"lf","right_hip":"rh","right_knee":"rk","right_foot":"rf"},"foot_ik":true,"foot_align_normals":true,"foot_max_slope_degrees":40,"foot_pelvis":true,"foot_max_pelvis_drop":0.1})",
            "", profile_, error),
        "authored stance settings decode");
    return failures ? 1 : 0;
}
