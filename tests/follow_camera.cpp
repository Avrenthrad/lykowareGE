// camera.follow (ADR 0044): the third-person camera never sees through or from inside walls.
// Physics only; no window or GPU.

#include <lykoware/follow_camera.hpp>
#include <lykoware/physics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace physics = lykoware::physics;
using lykoware::FollowCamera;
using lykoware::FollowCameraConfig;

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

struct Box {
    math::DVec3 centre;
    math::Vec3 half;
};

// Distance from a point to a box's surface; 0 inside.
double distance_to(const Box& box, math::DVec3 point)
{
    const double dx = std::max(0.0, std::abs(point.x - box.centre.x) - box.half.x);
    const double dy = std::max(0.0, std::abs(point.y - box.centre.y) - box.half.y);
    const double dz = std::max(0.0, std::abs(point.z - box.centre.z) - box.half.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Room {
    std::unique_ptr<physics::World> world;
    std::vector<Box> boxes;
    std::vector<physics::BodyId> bodies;
};

Room make_room(const std::vector<Box>& boxes)
{
    Room room;
    std::string error;
    room.world = physics::World::create({}, error);
    expect(room.world != nullptr, "the physics world is created: " + error);
    for (const Box& box : boxes) {
        physics::BodyDesc desc;
        desc.shape = physics::BoxShape{box.half};
        desc.motion = physics::MotionType::Static;
        desc.position = box.centre;
        const auto body = room.world ? room.world->create_body(desc, error) : std::nullopt;
        expect(body.has_value(), "a wall is created: " + error);
        if (body) {
            room.bodies.push_back(*body);
            room.boxes.push_back(box);
        }
    }
    return room;
}

const Box ground{{0.0, -0.5, 0.0}, {20.F, 0.5F, 20.F}};

void test_open_ground()
{
    Room room = make_room({ground});
    FollowCameraConfig config;
    FollowCamera camera{config};
    const math::DVec3 pivot{3.0, 1.6, -2.0};
    const auto view = camera.update(*room.world, pivot, 1.F / 60.F);
    const double dx = view.eye.x - pivot.x;
    const double dy = view.eye.y - pivot.y;
    const double dz = view.eye.z - pivot.z;
    expect(near(std::sqrt(dx * dx + dy * dy + dz * dz), config.distance, 1e-4), "with nothing in the way it keeps its distance");
    expect(near(camera.current_distance(), config.distance, 1e-6), "and reports it");
    expect(view.target.x == pivot.x && view.target.y == pivot.y && view.target.z == pivot.z, "it looks at the pivot");
    expect(dz > 0.0 && dy > 0.0, "yaw 0 puts it behind (+Z) and above the pivot");
    const math::Vec3 f = camera.forward();
    const math::Vec3 r = camera.right();
    expect(near(f.x, 0.0, 1e-6) && near(f.z, -1.0, 1e-6) && f.y == 0.F, "forward is -Z at yaw 0");
    expect(near(math::dot(f, r), 0.0, 1e-6) && r.y == 0.F && near(r.x, 1.0, 1e-6), "right is +X, across forward");
    expect(view.near == config.near && view.vertical_fov == config.vertical_fov, "the lens comes from the config");
}

void test_wall_behind()
{
    // A wall 2 m behind the pivot, between it and where the camera would be.
    const Box wall{{0.0, 2.0, 2.25}, {5.F, 3.F, 0.25F}};
    Room room = make_room({ground, wall});
    FollowCameraConfig config;
    FollowCamera camera{config};
    const math::DVec3 pivot{0.0, 1.6, 0.0};
    const auto view = camera.update(*room.world, pivot, 1.F / 60.F);
    // The wall's face is at z = 2, reached along the camera's line at 2 / cos(pitch).
    const float to_wall = 2.F / std::cos(camera.pitch());
    expect(camera.current_distance() <= to_wall - config.clearance + 1e-3F && camera.current_distance() > 1.F,
           "it pulls in to stop the clearance short of the wall: " + std::to_string(camera.current_distance()) + " of "
               + std::to_string(to_wall));
    expect(view.eye.z < 2.0 - config.near, "the eye stays on the pivot's side of the wall");
    expect(distance_to(wall, view.eye) >= config.near, "the near plane stays out of the wall");

    // The wall goes: the camera eases back out, no faster than the return speed.
    expect(room.world->destroy_body(room.bodies[1]), "the wall is removed");
    float previous = camera.current_distance();
    bool gradual = true;
    for (int frame = 0; frame < 120; ++frame) {
        (void)camera.update(*room.world, pivot, 1.F / 60.F);
        gradual = gradual && camera.current_distance() - previous <= config.return_speed / 60.F + 1e-5F
            && camera.current_distance() >= previous;
        previous = camera.current_distance();
    }
    expect(gradual, "it eases back out at no more than the return speed");
    expect(near(camera.current_distance(), config.distance, 1e-5), "and reaches its full distance again");
}

// The camera turned all the way round, and up and down, beside walls and in a corner.
void test_never_through_walls()
{
    const std::vector<Box> layout{
        ground,
        {{1.2, 2.0, 0.0}, {0.2F, 3.F, 6.F}},   // a wall 1 m to the right
        {{0.0, 2.0, -1.6}, {6.F, 3.F, 0.2F}},  // a wall 1.4 m ahead
        {{-2.5, 0.6, 2.0}, {0.6F, 0.6F, 0.6F}}, // a crate behind on the left
        {{0.0, 4.0, 0.0}, {6.F, 0.2F, 6.F}},   // a low ceiling 2.2 m above the pivot
    };
    Room room = make_room(layout);
    const math::DVec3 pivot{0.0, 1.6, 0.0};
    FollowCameraConfig config;
    FollowCamera camera{config};
    int views = 0;
    int pulled_in = 0;
    bool clear = true;
    bool outside = true;
    std::string first_failure;
    for (int pitch_step = 0; pitch_step < 7; ++pitch_step) {
        camera.turn(0.F, -10.F); // to the lowest pitch
        camera.turn(0.F, (config.max_pitch - config.min_pitch) * static_cast<float>(pitch_step) / 6.F);
        for (int yaw_step = 0; yaw_step < 72; ++yaw_step) {
            camera.turn(2.F * std::numbers::pi_v<float> / 72.F, 0.F);
            const auto view = camera.update(*room.world, pivot, 1.F / 60.F);
            ++views;
            pulled_in += camera.current_distance() < config.distance - 1e-3F ? 1 : 0;
            const math::Vec3 line{static_cast<float>(view.eye.x - pivot.x), static_cast<float>(view.eye.y - pivot.y),
                                  static_cast<float>(view.eye.z - pivot.z)};
            if (room.world->cast_ray(pivot, line).has_value()) {
                if (clear) {
                    first_failure = "a wall between pivot and eye at yaw " + std::to_string(camera.yaw()) + ", pitch "
                        + std::to_string(camera.pitch());
                }
                clear = false;
            }
            for (const Box& box : room.boxes) {
                if (distance_to(box, view.eye) < config.near) {
                    if (outside) {
                        first_failure = "the near plane in a wall at yaw " + std::to_string(camera.yaw()) + ", pitch "
                            + std::to_string(camera.pitch());
                    }
                    outside = false;
                }
            }
        }
    }
    expect(clear, "no wall is ever between the pivot and the eye: " + first_failure);
    expect(outside, "the eye never comes within the near plane of a wall: " + first_failure);
    expect(pulled_in > views / 4 && pulled_in < views, "the room makes it pull in for many views but not all: "
           + std::to_string(pulled_in) + " of " + std::to_string(views));
}

void test_wall_just_past_the_camera()
{
    // A wall 4.05 m behind a level camera that wants 4 m: the camera itself would be clear, but
    // its near plane (0.1 m) would cut into the wall, so it keeps the clearance.
    const Box wall{{0.0, 1.6, 4.3}, {5.F, 3.F, 0.25F}};
    Room room = make_room({ground, wall});
    FollowCameraConfig config;
    FollowCamera camera{config};
    camera.turn(0.F, -camera.pitch());
    const auto view = camera.update(*room.world, {0.0, 1.6, 0.0}, 1.F / 60.F);
    expect(camera.pitch() == 0.F, "the camera is level");
    expect(distance_to(wall, view.eye) >= config.clearance - 1e-3,
           "a wall just past the full distance still keeps the clearance: "
               + std::to_string(distance_to(wall, view.eye)));
}

void test_hugging_a_wall()
{
    // A wall 0.35 m to the right of the pivot, running along the view: no reason to pull in.
    const Box wall{{0.55, 2.0, 0.0}, {0.2F, 3.F, 10.F}};
    Room room = make_room({ground, wall});
    FollowCameraConfig config;
    FollowCamera camera{config};
    (void)camera.update(*room.world, {0.0, 1.6, 0.0}, 1.F / 60.F);
    expect(near(camera.current_distance(), config.distance, 1e-5),
           "a wall beside the character, along the view, does not pull the camera in");
}

void test_controls()
{
    Room room = make_room({ground});
    FollowCameraConfig config;
    FollowCamera camera{config};
    camera.turn(0.F, 100.F);
    expect(camera.pitch() == config.max_pitch, "pitch stops at its upper limit");
    camera.turn(0.F, -100.F);
    expect(camera.pitch() == config.min_pitch, "and at its lower limit");
    camera.turn(7.F * std::numbers::pi_v<float>, 0.F);
    expect(camera.yaw() >= -std::numbers::pi_v<float> && camera.yaw() < std::numbers::pi_v<float>, "yaw wraps");
    const float yaw = camera.yaw();
    camera.turn(std::nanf(""), 1.F);
    expect(camera.yaw() == yaw && camera.pitch() == config.min_pitch, "non-finite turns are ignored");

    camera.set_distance(0.F);
    const math::DVec3 pivot{1.0, 1.7, 1.0};
    const auto view = camera.update(*room.world, pivot, 1.F / 60.F);
    expect(view.eye.x == pivot.x && view.eye.y == pivot.y && view.eye.z == pivot.z, "distance 0 is first person");
    const math::Vec3 look{static_cast<float>(view.target.x - pivot.x), static_cast<float>(view.target.y - pivot.y),
                          static_cast<float>(view.target.z - pivot.z)};
    expect(near(math::length(look), 1.0, 1e-5) && math::dot(look, camera.forward()) > 0.F, "and looks forward");
    camera.set_distance(-3.F);
    (void)camera.update(*room.world, pivot, 1.F / 60.F);
    expect(camera.current_distance() == 0.F, "a negative distance is first person");
}

// 02.14: first/third-person transitions travel the distance instead of popping, and never put
// the eye through a wall. Off by default, which keeps the pull-in of test_controls.
struct Track {
    float worst_step = 0.F;    // the most the eye moved in one frame, metres
    float worst_turn = 0.F;    // the most the view direction turned in one frame, radians
    bool through_wall = false; // the eye was ever behind a wall seen from the pivot
    int frames = 0;            // frames until the camera stopped moving
};

// Measures from the view the camera last returned (`last`), so the first frame after a change counts.
Track follow(FollowCamera& camera, const physics::World& world, math::DVec3 pivot, int frames,
             lykoware::render::Camera& last)
{
    Track track;
    const auto direction = [](const lykoware::render::Camera& view) {
        return math::normalize(math::Vec3{static_cast<float>(view.target.x - view.eye.x),
                                          static_cast<float>(view.target.y - view.eye.y),
                                          static_cast<float>(view.target.z - view.eye.z)});
    };
    for (int i = 0; i < frames; ++i) {
        const auto view = camera.update(world, pivot, 1.F / 60.F);
        const math::Vec3 moved{static_cast<float>(view.eye.x - last.eye.x), static_cast<float>(view.eye.y - last.eye.y),
                               static_cast<float>(view.eye.z - last.eye.z)};
        track.worst_step = std::max(track.worst_step, math::length(moved));
        track.worst_turn = std::max(track.worst_turn,
            std::acos(std::clamp(math::dot(direction(view), direction(last)), -1.F, 1.F)));
        if (math::length(moved) > 0.F) { track.frames = i + 1; }
        const math::Vec3 out{static_cast<float>(view.eye.x - pivot.x), static_cast<float>(view.eye.y - pivot.y),
                             static_cast<float>(view.eye.z - pivot.z)};
        if (math::length(out) > 0.F && world.cast_ray(pivot, out)) { track.through_wall = true; }
        last = view;
    }
    return track;
}

void test_transitions()
{
    const math::DVec3 pivot{0.0, 1.7, 0.0};
    const float speed = 8.F;
    const float bound = speed / 60.F + 1e-4F;
    {
        // Open ground: third to first person and back, each step no more than speed x dt.
        auto room = make_room({ground});
        FollowCameraConfig config;
        config.transition_speed = speed;
        FollowCamera camera{config};
        lykoware::render::Camera view = camera.update(*room.world, pivot, 1.F / 60.F);
        (void)follow(camera, *room.world, pivot, 5, view);
        camera.set_distance(0.F);
        expect(camera.transitioning(), "a distance change starts a transition");
        const auto in = follow(camera, *room.world, pivot, 120, view);
        expect(in.worst_step <= bound, "third to first person moves no more than the transition speed allows");
        expect(in.worst_turn < 1e-3F, "the view direction does not turn while moving in");
        expect(camera.current_distance() == 0.F && !camera.transitioning(), "the camera reaches first person and stops");
        expect(in.frames >= 25 && in.frames <= 35, "4 m at 8 m/s takes about half a second");
        camera.set_distance(4.F);
        const auto out = follow(camera, *room.world, pivot, 120, view);
        expect(out.worst_step <= bound && out.worst_turn < 1e-3F, "first to third person does not pop");
        expect(near(camera.current_distance(), 4.0, 1e-5), "the camera reaches third person");
    }
    {
        // A wall 2 m behind: going out to third person stops short of it and never passes it.
        const Box wall{{0.0, 1.5, 2.5}, {5.F, 3.F, 0.5F}};
        auto room = make_room({ground, wall});
        FollowCameraConfig config;
        config.transition_speed = speed;
        config.distance = 0.F;
        FollowCamera camera{config};
        lykoware::render::Camera view = camera.update(*room.world, pivot, 1.F / 60.F);
        (void)follow(camera, *room.world, pivot, 5, view);
        camera.set_distance(4.F);
        const auto out = follow(camera, *room.world, pivot, 120, view);
        expect(!out.through_wall, "a transition out never puts the eye through a wall");
        expect(camera.current_distance() < 2.5F && camera.current_distance() > .5F, "it stops at the wall");
        expect(!camera.transitioning(), "the transition ends at the wall");
    }
    {
        // A wall appearing mid-transition still pulls the camera in at once.
        FollowCameraConfig config;
        config.transition_speed = 1.F; // slow, so the wall lands while still travelling
        auto open = make_room({ground});
        FollowCamera camera{config};
        lykoware::render::Camera view = camera.update(*open.world, pivot, 1.F / 60.F);
        (void)follow(camera, *open.world, pivot, 5, view);
        camera.set_distance(1.F);
        (void)follow(camera, *open.world, pivot, 30, view);
        expect(camera.transitioning() && camera.current_distance() > 3.F, "still travelling in");
        const Box wall{{0.0, 1.5, 1.5}, {5.F, 3.F, 0.5F}};
        auto walled = make_room({ground, wall});
        (void)camera.update(*walled.world, pivot, 1.F / 60.F);
        expect(camera.current_distance() < 1.F, "a wall during a transition pulls in at once");
    }
    {
        // Deliberate break: with the transition off (today's behaviour) the same change pops, so
        // the step bound above would catch it.
        auto room = make_room({ground});
        FollowCamera camera{FollowCameraConfig{}};
        lykoware::render::Camera view = camera.update(*room.world, pivot, 1.F / 60.F);
        (void)follow(camera, *room.world, pivot, 5, view);
        camera.set_distance(0.F);
        expect(!camera.transitioning(), "without a transition speed nothing transitions");
        const auto snap = follow(camera, *room.world, pivot, 5, view);
        expect(snap.worst_step > bound, "without a transition the change is a pop the bound catches");
        expect(snap.frames == 1, "and today's behaviour takes it in one frame");
    }
}

}

int main()
{
    test_open_ground();
    test_wall_behind();
    test_never_through_walls();
    test_wall_just_past_the_camera();
    test_hugging_a_wall();
    test_controls();
    test_transitions();
    if (g_failures != 0) {
        std::cerr << g_failures << " follow camera assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "follow camera passed\n";
    return EXIT_SUCCESS;
}
