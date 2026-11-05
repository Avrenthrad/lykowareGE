// player.course (ADR 0047): a player-steered character runs a fixed-seed obstacle course
// through the same input path as the sandbox. Terrain bumps, a ramp, a fall, stairs, a ledge
// that needs a jump, a second fall, a low tunnel for the camera and a slope too steep to climb.
// Physics and input only; no window or GPU.

#include <lykoware/clock.hpp>
#include <lykoware/input.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/player.hpp>

#include "obstacle_course.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace physics = lykoware::physics;
using lykoware::GamepadAxis;
using lykoware::GamepadButton;
using lykoware::InputState;
using lykoware::Key;
using lykoware::PlayerCharacter;
using lykoware::PlayerConfig;
using lykoware::PlayerInputConfig;
using lykoware::PlayerIntent;

constexpr float pi = std::numbers::pi_v<float>;

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

std::string text(double value)
{
    return std::to_string(value);
}

std::unique_ptr<physics::World> make_world()
{
    std::string error;
    auto world = physics::World::create({}, error);
    if (!world) {
        std::cerr << "FATAL: world: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return world;
}

std::unique_ptr<PlayerCharacter> make_player(physics::World& world, PlayerConfig config)
{
    std::string error;
    auto player = PlayerCharacter::create(world, config, error);
    if (!player) {
        std::cerr << "FATAL: player: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    return player;
}

float tick_seconds()
{
    std::string error;
    const auto clock = lykoware::FixedStep::create({}, error);
    return clock ? static_cast<float>(clock->tick_seconds()) : 1.F / 60.F;
}

void add_static(physics::World& world, physics::Shape shape, math::DVec3 position, math::Quat rotation = {})
{
    physics::BodyDesc desc;
    desc.shape = std::move(shape);
    desc.motion = physics::MotionType::Static;
    desc.position = position;
    desc.rotation = rotation;
    std::string error;
    if (!world.create_body(desc, error)) {
        std::cerr << "FATAL: course body: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// --- input mapping --------------------------------------------------------------------------

void test_intent_mapping()
{
    const PlayerInputConfig config;
    InputState input;
    expect(read_player_intent(input, config, 1.F / 60.F, true).forward == 0.F, "no input asks for nothing");

    lykoware::note_key(input, Key::W, true);
    lykoware::note_key(input, Key::D, true);
    PlayerIntent intent = read_player_intent(input, config, 1.F / 60.F, true);
    expect(near(intent.forward, std::sqrt(0.5), 1e-6) && near(intent.right, std::sqrt(0.5), 1e-6),
           "W and D together move diagonally at full speed, not faster");
    lykoware::note_key(input, Key::S, true);
    intent = read_player_intent(input, config, 1.F / 60.F, true);
    expect(intent.forward == 0.F && intent.right == 1.F, "W and S cancel");
    lykoware::note_key(input, Key::W, false);
    lykoware::note_key(input, Key::S, false);
    lykoware::note_key(input, Key::D, false);

    // Stick: up is negative Y and means forward; the dead zone is cut out and the rest rescaled.
    lykoware::note_gamepad_connection(input, true);
    lykoware::note_gamepad_axis(input, GamepadAxis::LeftY, -0.15F);
    expect(read_player_intent(input, config, 1.F / 60.F, true).forward == 0.F, "a resting stick inside the dead zone is ignored");
    lykoware::note_gamepad_axis(input, GamepadAxis::LeftY, -0.6F);
    expect(near(read_player_intent(input, config, 1.F / 60.F, true).forward, 0.5, 1e-6),
           "past the dead zone the stick is rescaled: 0.6 of the way is half speed");
    lykoware::note_gamepad_axis(input, GamepadAxis::LeftY, -1.F);
    expect(read_player_intent(input, config, 1.F / 60.F, true).forward == 1.F, "a full stick is full speed");
    lykoware::note_gamepad_axis(input, GamepadAxis::LeftY, std::nanf(""));
    expect(read_player_intent(input, config, 1.F / 60.F, true).forward == 0.F, "a non-finite axis is ignored");
    lykoware::note_gamepad_axis(input, GamepadAxis::LeftY, 0.F);

    // Looking: mouse right turns the yaw negative (to the right), mouse down raises the pitch.
    input.mouse.move_x = 100.F;
    input.mouse.move_y = 40.F;
    intent = read_player_intent(input, config, 1.F / 60.F, true);
    const double per_pixel = static_cast<double>(config.mouse_radians_per_pixel);
    expect(near(intent.yaw, -100.0 * per_pixel, 1e-6) && near(intent.pitch, 40.0 * per_pixel, 1e-6),
           "mouse motion turns the view by its sensitivity");
    expect(read_player_intent(input, config, 1.F / 60.F, false).yaw == 0.F,
           "without mouse-look a free cursor does not turn the view");
    PlayerInputConfig inverted = config;
    inverted.invert_pitch = true;
    expect(read_player_intent(input, inverted, 1.F / 60.F, true).pitch < 0.F, "inverted pitch looks up for mouse down");
    input.mouse = {};

    lykoware::note_gamepad_axis(input, GamepadAxis::RightX, 1.F);
    intent = read_player_intent(input, config, 0.5F, true);
    expect(near(intent.yaw, -0.5 * static_cast<double>(config.turn_radians_per_second), 1e-6),
           "the right stick turns at its rate");
    expect(read_player_intent(input, config, -1.F, true).yaw == 0.F, "a negative frame length turns nothing");
    lykoware::note_gamepad_axis(input, GamepadAxis::RightX, 0.F);
    lykoware::note_key(input, Key::Up, true);
    expect(read_player_intent(input, config, 0.5F, true).pitch < 0.F, "the up arrow looks up");
    lykoware::note_key(input, Key::Up, false);

    lykoware::begin_input_frame(input);
    lykoware::note_key(input, Key::Space, true);
    expect(read_player_intent(input, config, 1.F / 60.F, true).jump, "Space jumps");
    lykoware::begin_input_frame(input);
    expect(!read_player_intent(input, config, 1.F / 60.F, true).jump, "holding Space jumps once");
    lykoware::note_gamepad_button(input, GamepadButton::South, true);
    expect(read_player_intent(input, config, 1.F / 60.F, true).jump, "the South button jumps");
    lykoware::note_key(input, Key::LeftShift, true);
    expect(read_player_intent(input, config, 1.F / 60.F, true).run, "Shift runs");
    input.mouse.wheel_y = 2.F;
    expect(read_player_intent(input, config, 1.F / 60.F, true).zoom == 2.F, "the wheel zooms");
}

// --- the character on flat ground -----------------------------------------------------------

void add_floor(physics::World& world)
{
    add_static(world, physics::BoxShape{{30.F, 0.5F, 30.F}}, {0.0, -0.5, 0.0});
}

void settle(physics::World& world, PlayerCharacter& player, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        player.before_step(tick_seconds());
        (void)world.step(tick_seconds());
    }
}

void test_moves_relative_to_the_camera()
{
    auto world = make_world();
    add_floor(*world);
    PlayerConfig config;
    config.body.position = {0.0, 0.1, 0.0};
    auto player = make_player(*world, config);
    settle(*world, *player, 30);
    expect(player->state().ground == physics::GroundState::OnGround, "the player lands");

    // Turn the camera a quarter to the right: forward is now +X.
    PlayerIntent intent;
    intent.yaw = -pi / 2.F;
    intent.forward = 1.F;
    player->steer(intent);
    const math::DVec3 start = player->state().position;
    settle(*world, *player, 60);
    const math::DVec3 end = player->state().position;
    expect(near(end.x - start.x, config.walk_speed, 0.15) && near(end.z - start.z, 0.0, 0.05),
           "forward walks where the camera looks, at walking speed: " + text(end.x - start.x));
    expect(near(player->facing(), -pi / 2.F, 1e-4), "the body faces the way it walks");

    intent = {};
    intent.right = 1.F;
    intent.run = true;
    player->steer(intent);
    settle(*world, *player, 60);
    const math::DVec3 ran = player->state().position;
    expect(near(ran.z - end.z, config.run_speed, 0.2) && near(ran.x - end.x, 0.0, 0.05),
           "right is to the camera's right, and running is faster: " + text(ran.z - end.z));

    intent = {};
    player->steer(intent);
    settle(*world, *player, 10);
    const math::DVec3 stopped = player->state().position;
    settle(*world, *player, 30);
    expect(near(player->state().position.x, stopped.x, 1e-3) && near(player->state().position.z, stopped.z, 1e-3),
           "letting go stops it");
    expect(near(player->facing(), pi, 1e-4) || near(player->facing(), -pi, 1e-4), "and it keeps facing its last way");

    const auto view = player->view(1.F / 60.F);
    const math::DVec3 pivot = player->pivot();
    expect(view.target == pivot && near(pivot.y - player->state().position.y, config.eye_height, 1e-6),
           "the camera looks at the pivot, eye_height above the feet");
}

// Ticks from a jump on flat ground until it stands on the ground again.
int ticks_to_land(physics::World& world, PlayerCharacter& player)
{
    for (int tick = 1; tick <= 300; ++tick) {
        player.before_step(tick_seconds());
        (void)world.step(tick_seconds());
        if (tick > 5 && player.state().ground == physics::GroundState::OnGround) {
            return tick;
        }
    }
    return -1;
}

// Jumps, then presses jump again `before` ticks before it lands; true if it jumps again on
// landing.
bool jumps_again(int before, int airtime)
{
    auto world = make_world();
    add_floor(*world);
    PlayerConfig config;
    config.body.position = {0.0, 0.0, 0.0};
    auto player = make_player(*world, config);
    settle(*world, *player, 30);
    PlayerIntent jump;
    jump.jump = true;
    player->steer(jump);
    for (int tick = 0; tick < airtime + 20; ++tick) {
        if (tick == airtime - before) {
            player->steer(jump);
        }
        player->before_step(tick_seconds());
        (void)world->step(tick_seconds());
        if (tick > airtime + 2 && player->state().position.y > 0.2) {
            return true;
        }
    }
    return false;
}

void test_jumps()
{
    auto world = make_world();
    add_floor(*world);
    PlayerConfig config;
    config.body.position = {0.0, 0.0, 0.0};
    auto player = make_player(*world, config);
    settle(*world, *player, 30);
    PlayerIntent intent;
    intent.jump = true;
    player->steer(intent);
    intent.jump = false;
    player->steer(intent); // a later frame without the press does not cancel it
    const int airtime = ticks_to_land(*world, *player);
    expect(airtime > 30, "a jump leaves the ground for a while: " + std::to_string(airtime) + " ticks");
    settle(*world, *player, 60);
    expect(player->state().position.y < 0.05, "and lands once, without jumping again");

    // A press in a frame with no physics step still jumps at the next step.
    player->steer(PlayerIntent{.jump = true});
    (void)player->view(1.F / 144.F);
    player->steer(PlayerIntent{});
    (void)player->view(1.F / 144.F);
    settle(*world, *player, 10);
    expect(player->state().position.y > 0.3, "a press between steps is not lost");
    (void)ticks_to_land(*world, *player);

    // Pressed just before landing it jumps on landing (the buffer is 0.1 s, six ticks); pressed
    // well before, it does not, and no jump is left waiting in the world.
    expect(jumps_again(3, airtime), "a press three ticks before landing jumps on landing");
    expect(!jumps_again(15, airtime), "a press a quarter second before landing is dropped");

    // Holding the button does not bunny-hop.
    settle(*world, *player, 30);
    InputState input;
    lykoware::note_key(input, Key::Space, true);
    player->steer(read_player_intent(input, {}, 1.F / 60.F, false));
    int landings = 0;
    bool airborne = false;
    for (int frame = 0; frame < 240; ++frame) {
        lykoware::begin_input_frame(input);
        player->steer(read_player_intent(input, {}, 1.F / 60.F, false));
        player->before_step(tick_seconds());
        (void)world->step(tick_seconds());
        const bool in_air = player->state().position.y > 0.1;
        landings += airborne && !in_air ? 1 : 0;
        airborne = in_air;
    }
    expect(landings == 1 && player->state().position.y < 0.05, "holding jump jumps once");
}

void test_zoom()
{
    auto world = make_world();
    add_floor(*world);
    PlayerConfig config;
    config.body.position = {0.0, 0.0, 0.0};
    auto player = make_player(*world, config);
    const auto zoom = [&player](float steps) {
        PlayerIntent intent;
        intent.zoom = steps;
        player->steer(intent);
        return player->camera().distance();
    };
    expect(player->camera().distance() == 4.F, "it starts at the camera's distance");
    expect(zoom(1.F) == 3.F && zoom(2.F) == 1.F, "the wheel moves the camera in a metre a step");
    expect(zoom(1.F) == 0.F, "one more step is first person");
    expect(zoom(1.F) == 0.F, "and it stays there");
    expect(zoom(-0.3F) == 1.F, "zooming out from first person goes straight to one step out");
    expect(zoom(0.4F) == 0.F, "and a part step in from there is first person, never inside the head");
    expect(zoom(-100.F) == config.max_distance, "zooming out stops at the farthest distance");
    expect(zoom(std::nanf("")) == config.max_distance, "a non-finite zoom is ignored");
    const auto view = player->view(1.F / 60.F);
    expect(near(player->camera().current_distance(), config.max_distance, 1e-4), "the camera moves out to it");
    (void)view;
}

// Drawing between steps (ADR 0048): the drawn feet blend the last two steps by the frame's alpha,
// so at 144 Hz the character and camera move evenly every frame instead of jumping a whole step
// on some frames and standing still on others.
void test_drawn_between_steps()
{
    auto world = make_world();
    add_floor(*world);
    PlayerConfig config;
    config.body.position = {0.0, 0.0, 0.0};
    auto player = make_player(*world, config);
    settle(*world, *player, 30);
    expect(player->drawn_feet(0.F) == player->state().position, "standing still, every alpha draws where it stands");
    PlayerIntent walk;
    walk.forward = 1.F;
    player->steer(walk);
    settle(*world, *player, 30);
    const math::DVec3 before = player->state().position;
    settle(*world, *player, 1);
    const math::DVec3 after = player->state().position;
    expect(player->drawn_feet(0.F) == before && player->drawn_feet(1.F) == after,
           "alpha 0 draws the step before, alpha 1 the latest");
    const math::DVec3 half = player->drawn_feet(0.5F);
    expect(near(half.z, (before.z + after.z) / 2.0, 1e-9) && near(half.x, (before.x + after.x) / 2.0, 1e-9),
           "alpha 0.5 draws halfway");
    expect(player->drawn_feet(-1.F) == before && player->drawn_feet(2.F) == after, "alpha is clamped to 0..1");
    expect(player->drawn_feet(std::nanf("")) == after, "a non-finite alpha draws the latest step");
    const auto view = player->view(1.F / 60.F, 0.25F);
    const math::DVec3 drawn = player->drawn_feet(0.25F);
    expect(view.target == math::DVec3{drawn.x, drawn.y + static_cast<double>(config.eye_height), drawn.z},
           "the camera orbits the drawn pivot");
    expect(player->view(1.F / 60.F).target == player->pivot(), "without an alpha it orbits the latest step");

    // 144 Hz frames over 60 Hz steps, walking at 4 m/s: 0.0278 m a frame.
    std::string error;
    auto clock = lykoware::FixedStep::create({}, error);
    const double expected = static_cast<double>(config.walk_speed) / 144.0;
    double drawn_least = 1e9;
    double drawn_most = 0.0;
    double eye_least = 1e9;
    double eye_most = 0.0;
    double latest_least = 1e9;
    double latest_most = 0.0;
    math::DVec3 last_drawn = player->drawn_feet(0.F);
    math::DVec3 last_eye = view.eye;
    math::DVec3 last_latest = player->state().position;
    for (int frame = 0; frame < 300; ++frame) {
        player->steer(walk);
        const lykoware::FrameSteps steps = clock->advance(1'000'000'000 / 144);
        for (std::uint32_t tick = 0; tick < steps.ticks; ++tick) {
            player->before_step(tick_seconds());
            (void)world->step(tick_seconds());
        }
        const auto alpha = static_cast<float>(steps.alpha);
        const auto camera = player->view(1.F / 144.F, alpha);
        const math::DVec3 now = player->drawn_feet(alpha);
        const math::DVec3 latest = player->state().position;
        if (frame >= 10) {
            const auto moved = [](math::DVec3 a, math::DVec3 b) { return std::hypot(a.x - b.x, a.z - b.z); };
            drawn_least = std::min(drawn_least, moved(now, last_drawn));
            drawn_most = std::max(drawn_most, moved(now, last_drawn));
            eye_least = std::min(eye_least, moved(camera.eye, last_eye));
            eye_most = std::max(eye_most, moved(camera.eye, last_eye));
            latest_least = std::min(latest_least, moved(latest, last_latest));
            latest_most = std::max(latest_most, moved(latest, last_latest));
        }
        last_drawn = now;
        last_eye = camera.eye;
        last_latest = latest;
    }
    expect(drawn_least > expected * 0.9 && drawn_most < expected * 1.1,
           "drawn between steps, the feet move evenly every 144 Hz frame: " + text(drawn_least) + " to "
               + text(drawn_most) + " m, expected " + text(expected));
    expect(eye_least > expected * 0.9 && eye_most < expected * 1.1,
           "and so does the camera: " + text(eye_least) + " to " + text(eye_most) + " m");
    std::cout << "144 Hz frames: drawn feet move " << drawn_least << " to " << drawn_most << " m, camera " << eye_least
              << " to " << eye_most << " m, latest step alone " << latest_least << " to " << latest_most << " m (expected "
              << expected << ")\n";
    expect(latest_least < expected * 0.1 && latest_most > expected * 2.0,
           "while the latest step alone stands still on some frames and jumps on others: " + text(latest_least)
               + " to " + text(latest_most) + " m");
}

void test_refusals()
{
    auto world = make_world();
    std::string error;
    PlayerConfig config;
    config.eye_height = 1.9F;
    expect(!PlayerCharacter::create(*world, config, error) && !error.empty(), "an eye above the capsule is refused");
    config = {};
    config.eye_height = std::nanf("");
    expect(!PlayerCharacter::create(*world, config, error) && !error.empty(), "a non-finite eye height is refused");
    config = {};
    config.walk_speed = -1.F;
    expect(!PlayerCharacter::create(*world, config, error) && !error.empty(), "a negative speed is refused");
    config = {};
    config.body.radius = 0.F;
    expect(!PlayerCharacter::create(*world, config, error) && !error.empty(), "the world's refusal is passed on");
    expect(world->character_count() == 0, "refused players leave no character");
    {
        auto player = make_player(*world, {});
        expect(world->character_count() == 1 && world->is_valid(player->character()), "a player has a character");
    }
    expect(world->character_count() == 0, "and takes it with it");
}

// --- the obstacle course --------------------------------------------------------------------

using obstacle_course::Block;

// Distance from a point to an oriented block's surface; 0 inside.
double distance_to(const Block& box, math::DVec3 point)
{
    const math::Vec3 local = math::rotate(math::conjugate(box.rotation), math::relative_to(point, box.centre));
    const double dx = std::max(0.0, static_cast<double>(std::abs(local.x) - box.half.x));
    const double dy = std::max(0.0, static_cast<double>(std::abs(local.y) - box.half.y));
    const double dz = std::max(0.0, static_cast<double>(std::abs(local.z) - box.half.z));
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The course the sandbox's --play mode shows (samples/course/obstacle_course.hpp).
struct Course {
    std::unique_ptr<physics::World> world;
    std::vector<Block> boxes;
};

Course build_course()
{
    Course course;
    course.world = make_world();
    std::string error;
    if (!obstacle_course::build(*course.world, error)) {
        std::cerr << "FATAL: course: " << error << '\n';
        std::exit(EXIT_FAILURE);
    }
    course.boxes = obstacle_course::blocks();
    return course;
}

// A small fixed-seed generator (xorshift32) for frame times and the bot's hand wobble.
class Random {
public:
    explicit Random(std::uint32_t seed)
        : state_{seed}
    {
    }
    float uniform(float low, float high)
    {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 17U;
        state_ ^= state_ << 5U;
        return low + (high - low) * static_cast<float>(state_ >> 8U) / 16777216.F;
    }

private:
    std::uint32_t state_;
};

struct Results {
    std::vector<std::string> failures; // course checkpoints that were not met
    bool steps_ok = true;
    bool view_clear = true;
    bool view_outside = true;
    std::string first_view_failure;
    double lowest_feet = 1e9;
    double deepest = 0.0;       // feet below the surface under them, at worst
    int longest_sunk = 0;       // most consecutive ticks with the feet more than 2 cm under it
    double eye_above_terrain = 1e9;
    int tunnel_frames = 0;
    int tunnel_pulled_in = 0;
    int frames = 0;
    std::uint64_t hash = 0;
    math::DVec3 end;
};

// Drives a player through the course frame by frame, as a person would: keys or the stick
// to walk, the mouse to steer and look around, Space to jump. Frame times vary from 30 to
// 144 Hz; the physics steps at a fixed 60 Hz in between.
class Bot {
public:
    Bot(Course& course, PlayerCharacter& player, Results& results, std::uint32_t seed)
        : course_{course}
        , player_{player}
        , results_{results}
        , random_{seed}
    {
        std::string error;
        clock_.emplace(*lykoware::FixedStep::create({}, error));
        lykoware::note_gamepad_connection(input_, true);
    }

    bool use_stick = false;
    bool run = false;

    // Walks towards (x, z) until within 0.75 m; false if it takes longer than `seconds`.
    bool walk_to(double x, double z, float seconds)
    {
        for (float elapsed = 0.F; elapsed < seconds;) {
            elapsed += frame(x, z, true, false);
            const math::DVec3 at = player_.state().position;
            if (std::hypot(at.x - x, at.z - z) < 0.75) {
                return true;
            }
        }
        return false;
    }
    // Keeps walking towards (x, z) for `seconds`, whether it gets there or not.
    void push_towards(double x, double z, float seconds)
    {
        for (float elapsed = 0.F; elapsed < seconds;) {
            elapsed += frame(x, z, true, false);
        }
    }
    void jump_towards(double x, double z)
    {
        (void)frame(x, z, true, true);
    }
    // Stands still (still looking around) until on walkable ground; false after `seconds`.
    bool land(float seconds)
    {
        const math::DVec3 at = player_.state().position;
        for (float elapsed = 0.F; elapsed < seconds;) {
            if (player_.state().ground == physics::GroundState::OnGround) {
                return true;
            }
            elapsed += frame(at.x, at.z, false, false);
        }
        return player_.state().ground == physics::GroundState::OnGround;
    }

private:
    float frame(double x, double z, bool walk, bool jump)
    {
        const float seconds = random_.uniform(1.F / 144.F, 1.F / 30.F);
        lykoware::begin_input_frame(input_);

        // Steer with the mouse: turn towards the target, a little unsteadily.
        const math::DVec3 at = player_.state().position;
        const float wanted = std::atan2(-static_cast<float>(x - at.x), -static_cast<float>(z - at.z));
        const float off = std::remainder(wanted - player_.camera().yaw(), 2.F * pi);
        const float per_pixel = input_config_.mouse_radians_per_pixel;
        float move_x = walk ? std::clamp(-off / per_pixel, -60.F, 60.F) : 0.F;
        move_x += random_.uniform(-3.F, 3.F);
        // Look up and down at random, a new pitch every 0.75 s.
        look_timer_ -= seconds;
        if (look_timer_ <= 0.F) {
            look_target_ = random_.uniform(-0.6F, 1.3F);
            look_timer_ = 0.75F;
        }
        const float move_y = std::clamp((look_target_ - player_.camera().pitch()) / per_pixel, -25.F, 25.F);
        lykoware::note_mouse_motion(input_, move_x, move_y);

        lykoware::note_key(input_, Key::W, walk && !use_stick);
        lykoware::note_gamepad_axis(input_, GamepadAxis::LeftY, walk && use_stick ? -1.F : 0.F);
        lykoware::note_key(input_, Key::LeftShift, run);
        lykoware::note_key(input_, Key::Space, jump);

        player_.steer(read_player_intent(input_, input_config_, seconds, true));
        const lykoware::FrameSteps steps = clock_->advance(static_cast<std::int64_t>(seconds * 1e9F));
        for (std::uint32_t tick = 0; tick < steps.ticks; ++tick) {
            player_.before_step(static_cast<float>(clock_->tick_seconds()));
            results_.steps_ok = course_.world->step(static_cast<float>(clock_->tick_seconds())) && results_.steps_ok;
            check_feet();
        }
        // Drawn as the sandbox draws it, between the last two steps (ADR 0048).
        const auto alpha = static_cast<float>(steps.alpha);
        check_view(player_.view(seconds, alpha), alpha);
        ++results_.frames;
        return seconds;
    }

    // The feet against the surface straight under them.
    void check_feet()
    {
        const math::DVec3 feet = player_.state().position;
        results_.lowest_feet = std::min(results_.lowest_feet, feet.y);
        const auto hit = course_.world->cast_ray({feet.x, feet.y + 0.5, feet.z}, {0.F, -1.F, 0.F});
        const double depth = hit ? hit->point.y - feet.y : -1.0;
        results_.deepest = std::max(results_.deepest, depth);
        sunk_ = depth > 0.02 ? sunk_ + 1 : 0;
        results_.longest_sunk = std::max(results_.longest_sunk, sunk_);
    }

    void check_view(const lykoware::render::Camera& view, float alpha)
    {
        const math::DVec3 feet = player_.drawn_feet(alpha);
        const math::DVec3 pivot{feet.x, feet.y + static_cast<double>(PlayerConfig{}.eye_height), feet.z};
        const math::Vec3 line = math::relative_to(view.eye, pivot);
        const std::string where = " at (" + text(pivot.x) + ", " + text(pivot.z) + "), yaw "
            + text(player_.camera().yaw()) + ", pitch " + text(player_.camera().pitch());
        if (course_.world->cast_ray(pivot, line)) {
            if (results_.view_clear && results_.view_outside) {
                results_.first_view_failure = "something between the pivot and the eye" + where;
            }
            results_.view_clear = false;
        }
        for (const Block& box : course_.boxes) {
            if (distance_to(box, view.eye) < static_cast<double>(view.near)) {
                if (results_.view_clear && results_.view_outside) {
                    results_.first_view_failure = "the near plane in a wall" + where;
                }
                results_.view_outside = false;
            }
        }
        results_.eye_above_terrain =
            std::min(results_.eye_above_terrain, view.eye.y - static_cast<double>(obstacle_course::terrain_height(view.eye.x, view.eye.z)));
        if (pivot.x > 4.0 && pivot.x < 12.0 && pivot.z < -19.0 && pivot.z > -22.0) {
            ++results_.tunnel_frames;
            results_.tunnel_pulled_in += player_.camera().current_distance() < player_.camera().distance() - 1e-3F ? 1 : 0;
        }
    }

    Course& course_;
    PlayerCharacter& player_;
    Results& results_;
    Random random_;
    std::optional<lykoware::FixedStep> clock_;
    InputState input_;
    PlayerInputConfig input_config_;
    float look_target_ = 0.3F;
    float look_timer_ = 0.F;
    int sunk_ = 0;
};

Results run_course(std::uint32_t seed)
{
    Results results;
    Course course = build_course();
    PlayerConfig config;
    config.body.position = obstacle_course::start;
    auto player = make_player(*course.world, config);
    Bot bot{course, *player, results, seed};
    const auto check = [&results](bool ok, const std::string& what) {
        if (!ok) {
            results.failures.push_back(what);
        }
    };
    const auto height = [&player] { return player->state().position.y; };

    check(bot.land(1.F), "it lands at the start");
    bot.run = true;
    check(bot.walk_to(0.0, 6.0, 6.F), "it runs over the rolling terrain");
    bot.run = false;
    check(bot.walk_to(0.0, -2.0, 4.F) && bot.land(0.5F) && near(height(), 1.5, 0.05),
          "it walks up the 20 degree ramp onto the 1.5 m platform: " + text(height()));
    check(bot.walk_to(0.0, -7.0, 4.F) && bot.land(1.F) && near(height(), 0.0, 0.05),
          "it walks off the end and lands 1.5 m down: " + text(height()));
    check(bot.walk_to(0.0, -12.4, 5.F) && bot.land(0.5F) && near(height(), 1.2, 0.05),
          "it climbs the 0.3 m stairs to the 1.2 m platform: " + text(height()));
    bot.push_towards(0.0, -16.0, 1.F);
    check(near(height(), 1.2, 0.05) && player->state().position.z > -14.0,
          "a 0.6 m ledge stops it without a jump: " + text(height()));
    bot.jump_towards(0.0, -16.0);
    check(bot.walk_to(0.0, -16.0, 3.F) && bot.land(1.F) && near(height(), 1.8, 0.05),
          "a jump gets it onto the 1.8 m platform: " + text(height()));
    check(bot.walk_to(0.0, -20.5, 4.F) && bot.land(1.F) && near(height(), 0.0, 0.05),
          "it walks off the end and lands 1.8 m down: " + text(height()));
    bot.use_stick = true;
    check(bot.walk_to(13.0, -20.5, 8.F), "it turns and walks through the tunnel with the stick");
    bot.push_towards(20.0, -20.5, 2.F);
    check(height() < 1.0 && player->state().position.x < 17.0,
          "a 60 degree slope is too steep to walk up: " + text(height()));

    results.hash = course.world->state_hash();
    results.end = player->state().position;
    return results;
}

void test_course()
{
    constexpr std::uint32_t seed = 0x4C594B4FU;
    const Results first = run_course(seed);
    for (const std::string& failure : first.failures) {
        expect(false, "course: " + failure);
    }
    expect(first.steps_ok, "every physics step succeeds");
    expect(first.lowest_feet > -0.05, "it never falls through the world: lowest feet " + text(first.lowest_feet));
    expect(first.longest_sunk <= 2, "the feet never stay sunk into the ground: " + std::to_string(first.longest_sunk)
                                        + " ticks at worst, " + text(first.deepest) + " m deepest");
    expect(first.view_clear, "nothing ever comes between the pivot and the eye: " + first.first_view_failure);
    expect(first.view_outside, "the eye never comes within the near plane of a wall: " + first.first_view_failure);
    expect(first.eye_above_terrain >= 0.09, "the eye stays above the terrain by the near plane: "
                                                + text(first.eye_above_terrain));
    expect(first.tunnel_frames > 30 && first.tunnel_pulled_in * 2 > first.tunnel_frames,
           "the tunnel's ceiling pulls the camera in for most of the way: " + std::to_string(first.tunnel_pulled_in)
               + " of " + std::to_string(first.tunnel_frames) + " frames");
    std::cout << "course: " << first.frames << " frames, deepest " << first.deepest << " m, longest sunk "
              << first.longest_sunk << " ticks, eye above terrain " << first.eye_above_terrain << " m, tunnel "
              << first.tunnel_pulled_in << "/" << first.tunnel_frames << " pulled in\n";

    const Results again = run_course(seed);
    expect(again.hash == first.hash && again.end == first.end, "the same seed runs the course identically");
    const Results other = run_course(seed + 1);
    expect(other.failures.empty(), "another seed finishes the course too");
    expect(other.hash != first.hash, "and a different seed takes a different path");
}

}

int main()
{
    test_intent_mapping();
    test_moves_relative_to_the_camera();
    test_jumps();
    test_zoom();
    test_drawn_between_steps();
    test_refusals();
    test_course();
    if (g_failures != 0) {
        std::cerr << g_failures << " player course assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "player course passed\n";
    return EXIT_SUCCESS;
}
