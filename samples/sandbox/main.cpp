#include "animated_mannequin.hpp"
#include "character_fit.hpp"
#include "rig_roles.hpp"
#include "foot_placement.hpp"

#include <lykoware/audio.hpp>
#include <lykoware/audio_output.hpp>
#include <lykoware/audio_spatial.hpp>
#include <lykoware/clock.hpp>
#include <lykoware/character_asset.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/gltf.hpp>
#include <lykoware/log.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/paths.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>
#include <lykoware/player.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/trace.hpp>
#include <lykoware/transform.hpp>
#include <lykoware/version.hpp>
#include <lykoware/window.hpp>

#include <fstream>
#include <set>

#include <bit>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "obstacle_course.hpp"

namespace {

std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path{std::u8string(text.begin(), text.end())};
}

std::uint64_t parse_frames(std::string_view text)
{
    if (text.empty() || text.front() == '+' || text.front() == '-') {
        throw std::invalid_argument(
            "--frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || value < 1 || value > lykoware::cpu_trace_frame_limit) {
        throw std::invalid_argument(
            "--frames must be an integer from 1 to "
            + std::to_string(lykoware::cpu_trace_frame_limit) + ".");
    }
    return value;
}

int write_cpu_trace(std::uint64_t frames, const std::filesystem::path& path)
{
    lykoware::CpuTrace trace;
    std::uint64_t state = 0x6c796b6f77617265ULL;
    {
        lykoware::CpuTrace::Zone loop{trace, "sandbox.loop"};
        for (std::uint64_t index = 0; index < frames; ++index) {
            lykoware::CpuTrace::Zone frame{trace, "sandbox.frame"};
            state = lykoware::cpu_fixture_iteration(state ^ (index + 1));
        }
    }
    if (state == 0) {
        lykoware::log(lykoware::LogLevel::info, "CPU fixture accumulator is zero.");
    }
    if (!trace.write(path)) {
        lykoware::log(lykoware::LogLevel::error, "Could not write CPU trace.");
        return EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info, "Wrote CPU trace.");
    return EXIT_SUCCESS;
}

int run_trace(const std::vector<std::string>& args)
{
    bool saw_frames = false;
    bool saw_trace = false;
    std::uint64_t frames = 0;
    std::string trace_file;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& option = args[index];
        if (option != "--frames" && option != "--trace") {
            throw std::invalid_argument("Unknown option: " + option + ". Use --help.");
        }
        if ((option == "--frames" && saw_frames) || (option == "--trace" && saw_trace)) {
            throw std::invalid_argument("Duplicate option: " + option);
        }
        if (index + 1 >= args.size() || args[index + 1].empty()
            || args[index + 1].starts_with("--")) {
            throw std::invalid_argument("Missing value for " + option);
        }
        const std::string& value = args[++index];
        if (option == "--frames") {
            frames = parse_frames(value);
            saw_frames = true;
        } else {
            trace_file = value;
            saw_trace = true;
        }
    }
    if (!saw_frames || !saw_trace) {
        throw std::invalid_argument("CPU trace requires --frames N and --trace FILE.");
    }
    return write_cpu_trace(frames, utf8_path(trace_file));
}

bool has_option(const std::vector<std::string>& args, std::string_view name)
{
    for (const std::string& arg : args) {
        if (arg == name) {
            return true;
        }
    }
    return false;
}

// Exit code when --vulkan finds no usable Vulkan device: 3, the code lyko
// uses for "unsupported".
constexpr int exit_unsupported = 3;

struct WindowOptions {
    bool dummy = false;
    bool offscreen = false;
    bool vulkan = false;
    bool no_audio = false;
    bool has_frames = false;
    std::uint64_t frames = 0;
    // --model FILE: a glTF model shown beside the crates (ADR 0035).
    std::string model;
    std::string character, idle_clip, walk_clip, character_profile;
    std::array<std::string,4> locomotion_clips{};
    lykoware::character::Profile character_settings;
    std::optional<float> character_height;
    std::optional<rig_roles::Names> bones;
    bool foot_ik = false;
    double foot_clearance = 0.03;
    // --trace FILE: a Chrome trace of each frame's CPU zones and GPU time (ADR 0039).
    std::string trace;
    // --play: a character to walk round the obstacle course, with a follow camera (ADR 0047).
    bool play = false;
};

// GPU frame times from the swapchain, as spans on the trace's GPU track (when tracing) and as a
// running total for the closing log line (ADR 0039).
class GpuFrameLog {
public:
    explicit GpuFrameLog(lykoware::CpuTrace* trace)
        : trace_{trace}
    {
    }

    void collect(lykoware::rhi::Swapchain& swapchain)
    {
        const std::vector<lykoware::rhi::GpuFrameTime> times = swapchain.take_gpu_frame_times();
        // The trace's clock now and the steady clock now, to place each frame where the CPU
        // submitted it; the GPU starts on it no earlier.
        const std::int64_t trace_now_us = trace_ != nullptr ? trace_->elapsed_us() : 0;
        const auto now = std::chrono::steady_clock::now();
        for (const lykoware::rhi::GpuFrameTime& time : times) {
            total_ns_ += time.nanoseconds;
            ++frames_;
            if (trace_ != nullptr) {
                const auto ago = std::chrono::duration_cast<std::chrono::microseconds>(now - time.submitted).count();
                trace_->add_gpu_span("gpu.frame", trace_now_us - ago,
                                     static_cast<std::int64_t>((time.nanoseconds + 500) / 1000));
            }
        }
    }

    [[nodiscard]] std::uint64_t frames() const { return frames_; }
    [[nodiscard]] double average_ms() const
    {
        return frames_ == 0 ? 0.0 : static_cast<double>(total_ns_) / static_cast<double>(frames_) / 1e6;
    }

private:
    lykoware::CpuTrace* trace_;
    std::uint64_t total_ns_ = 0;
    std::uint64_t frames_ = 0;
};

// The engine's lit shader compiled for the sandbox, deployed by the build and
// by cmake --install into the "shaders" folder beside the executable (ADR 0016).
constexpr const char* vertex_shader_file = "sandbox_lit.vert.spv";
constexpr const char* fragment_shader_file = "sandbox_lit.frag.spv";

struct ShaderWords {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
    std::vector<std::uint32_t> skin;
};

// Reads the shader files before any window or GPU work, so a broken install
// fails clearly even on a machine without Vulkan.
bool read_shaders(ShaderWords& shaders, bool skinning)
{
    const std::filesystem::path directory = lykoware::shader_directory();
    if (directory.empty()) {
        lykoware::log(lykoware::LogLevel::error, "Cannot find the executable's directory to load shaders from.");
        return false;
    }
    lykoware::rhi::RhiError error;
    if (!lykoware::rhi::read_spirv_file(directory / vertex_shader_file, shaders.vertex, error)
        || !lykoware::rhi::read_spirv_file(directory / fragment_shader_file, shaders.fragment, error)) {
        lykoware::log(lykoware::LogLevel::error,
                      "Sandbox shaders: " + error.message
                          + ". The build and cmake --install put them in the shaders folder beside the executable.");
        return false;
    }
    if (skinning && !lykoware::rhi::read_spirv_file(directory / "sandbox_skin.vert.spv", shaders.skin, error)) {
        lykoware::log(lykoware::LogLevel::error, "Sandbox skin shader: " + error.message);
        return false;
    }
    return true;
}

bool unavailable(lykoware::rhi::RhiFailure failure)
{
    using lykoware::rhi::RhiFailure;
    return failure == RhiFailure::NoLoader || failure == RhiFailure::NoDriver
        || failure == RhiFailure::VersionTooOld || failure == RhiFailure::MissingExtension
        || failure == RhiFailure::NoSuitableDevice;
}

// A short wooden knock, made here rather than loaded, so the repository holds no audio files:
// a low body tone with two decaying partials, plus a burst of low-passed noise for the impact.
std::shared_ptr<const lykoware::audio::Sound> make_thump()
{
    constexpr std::uint32_t rate = lykoware::audio::output_rate;
    constexpr float two_pi = 6.2831853F;
    std::vector<float> samples(rate * 3 / 10);
    std::uint32_t noise = 0x9E3779B9U;
    float knock = 0.F;
    float peak = 0.F;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const float body = std::sin(two_pi * 92.F * t) * std::exp(-t * 16.F)
            + 0.45F * std::sin(two_pi * 211.F * t) * std::exp(-t * 34.F);
        noise = noise * 1664525U + 1013904223U;
        const float white = static_cast<float>(noise >> 8U) / 8388608.F - 1.F;
        knock += 0.2F * (white - knock);
        samples[i] = 0.75F * body + 0.9F * knock * std::exp(-t * 70.F);
        peak = std::max(peak, std::abs(samples[i]));
    }
    for (float& sample : samples) {
        sample *= 0.9F / peak;
    }
    std::string error;
    auto sound = lykoware::audio::Sound::create(samples, 1, rate, error);
    if (!sound) {
        lykoware::log(lykoware::LogLevel::warning, "Crate sound: " + error);
    }
    return sound;
}

// The obstacle course's terrain (samples/course) as a mesh to draw: the height field's samples
// as a grid of triangles, facing up, with normals from the slope between neighbours.
lykoware::MeshData course_terrain_mesh()
{
    const lykoware::physics::HeightFieldShape field = obstacle_course::terrain();
    const std::uint32_t n = field.samples_per_side;
    const float spacing = field.spacing;
    const auto height = [&field, n](std::uint32_t x, std::uint32_t z) {
        return field.heights[static_cast<std::size_t>(std::min(z, n - 1)) * n + std::min(x, n - 1)];
    };
    lykoware::MeshData mesh;
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            mesh.positions.push_back({static_cast<float>(x) * spacing, height(x, z), static_cast<float>(z) * spacing});
            const float slope_x = (height(x + 1, z) - height(x == 0 ? 0 : x - 1, z)) / (2.F * spacing);
            const float slope_z = (height(x, z + 1) - height(x, z == 0 ? 0 : z - 1)) / (2.F * spacing);
            mesh.normals.push_back(lykoware::math::normalize(lykoware::math::Vec3{-slope_x, 1.F, -slope_z}));
        }
    }
    for (std::uint32_t z = 0; z + 1 < n; ++z) {
        for (std::uint32_t x = 0; x + 1 < n; ++x) {
            const std::uint32_t corner = z * n + x;
            // Counter-clockwise seen from above.
            mesh.indices.insert(mesh.indices.end(), {corner, corner + n, corner + 1, corner + 1, corner + n, corner + n + 1});
        }
    }
    return mesh;
}

// The falling-crates scene (ADR 0026): a grey ground and a grid of coloured crates. Physics
// moves the crate entities and the renderer draws every entity with a Transform and a
// MeshRenderer. With a sound, each crate has an AudioSource and reports its impacts (ADR 0034).
// With a terrain mesh it is the obstacle course instead of the flat ground (ADR 0047), and the
// crates fall to one side of it.
class CrateScene {
public:
    static constexpr int grid = 5;
    // Seconds between drops: then every crate is removed and a new set falls.
    static constexpr double seconds_per_drop = 8.0;

    // A crate that hit something this tick, and how hard (0..1).
    struct Impact {
        lykoware::ecs::Entity crate;
        float strength = 0.F;
    };

    CrateScene(lykoware::physics::World& physics, std::shared_ptr<const lykoware::audio::Sound> thump,
               std::optional<lykoware::render::MeshId> course_terrain = std::nullopt)
        : physics_(physics)
        , thump_(std::move(thump))
        , link_(entities_, physics)
    {
        std::string error;
        if (course_terrain) {
            add_course(*course_terrain);
            drop_centre_ = {-14.0, 0.0, 0.0};
        } else {
            lykoware::physics::BodyDesc ground;
            ground.shape = lykoware::physics::BoxShape{{12.F, 0.5F, 12.F}};
            ground.motion = lykoware::physics::MotionType::Static;
            ground.position = {0.0, -0.5, 0.0};
            const lykoware::ecs::Entity entity = entities_.create();
            link_.add_body(entity, ground, error);
            entities_.add<lykoware::render::MeshRenderer>(
                entity, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {24.F, 1.F, 24.F}, {0.45F, 0.45F, 0.45F}});
        }
        drop();
    }

    // Advances one fixed tick; every seconds_per_drop the crates are replaced.
    // Shows an uploaded model standing on the ground to the left of the crates, scaled so its
    // largest side is 3 m, turning slowly.
    void add_model(lykoware::render::MeshId mesh, const lykoware::Bounds& box)
    {
        const lykoware::math::Vec3 size = box.max - box.min;
        const float largest = std::max({size.x, size.y, size.z});
        model_scale_ = largest > 0.F ? 3.F / largest : 1.F;
        model_centre_ = (box.min + box.max) * 0.5F;
        model_base_ = box.min.y;
        model_ = entities_.create();
        entities_.add<lykoware::Transform>(model_, lykoware::Transform{});
        entities_.add<lykoware::render::MeshRenderer>(
            model_, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube,
                                                   {model_scale_, model_scale_, model_scale_},
                                                   {1.F, 1.F, 1.F},
                                                   mesh});
        place_model();
    }

    void tick(float seconds)
    {
        // Where each crate was before this step, to draw it between steps (ADR 0048).
        for (std::size_t i = 0; i < crates_.size(); ++i) {
            before_[i] = pose(crates_[i]);
        }
        link_.step(seconds);
        find_impacts(seconds);
        if (entities_.alive(model_)) {
            model_turn_ += 0.5F * seconds; // radians per second
            place_model();
        }
        elapsed_ += seconds;
        if (elapsed_ >= seconds_per_drop) {
            elapsed_ = 0.0;
            for (const lykoware::ecs::Entity crate : crates_) {
                entities_.destroy(crate); // the link destroys each crate's physics body too
            }
            crates_.clear();
            before_.clear();
            last_velocity_.clear();
            quiet_.clear();
            drop();
        }
    }

    // Draws each crate `alpha` (FrameSteps::alpha) of the way from where it was before the
    // latest step to where it is now, so crates fall smoothly at any frame rate (ADR 0048). Only
    // the drawn Transform changes; the next step overwrites it from physics.
    void present(float alpha)
    {
        const float t = std::isfinite(alpha) ? std::clamp(alpha, 0.F, 1.F) : 1.F;
        for (std::size_t i = 0; i < crates_.size(); ++i) {
            auto* transform = entities_.get<lykoware::Transform>(crates_[i]);
            if (transform == nullptr) {
                continue;
            }
            const Pose now = pose(crates_[i]);
            const lykoware::math::DVec3 from = before_[i].position;
            const auto mix = [t](double a, double b) { return a + (b - a) * static_cast<double>(t); };
            transform->position = {mix(from.x, now.position.x), mix(from.y, now.position.y), mix(from.z, now.position.z)};
            transform->rotation = blend(before_[i].rotation, now.rotation, t);
        }
    }

    lykoware::ecs::World& entities() { return entities_; }
    // Impacts since the last take_impacts(), oldest first.
    std::vector<Impact> take_impacts() { return std::exchange(impacts_, {}); }

private:
    struct Pose {
        lykoware::math::DVec3 position;
        lykoware::math::Quat rotation;
    };

    Pose pose(lykoware::ecs::Entity crate) const
    {
        const auto* body = entities_.get<lykoware::physics_ecs::RigidBody>(crate);
        const auto state = body != nullptr ? physics_.state(body->id()) : std::nullopt;
        return state ? Pose{state->position, state->rotation} : Pose{};
    }

    // Normalised linear blend along the shorter way round; close enough to a slerp for one
    // tick's turn.
    static lykoware::math::Quat blend(lykoware::math::Quat a, lykoware::math::Quat b, float t)
    {
        const float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.F ? -1.F : 1.F;
        return lykoware::math::normalize(lykoware::math::Quat{a.x + (sign * b.x - a.x) * t, a.y + (sign * b.y - a.y) * t,
                                                              a.z + (sign * b.z - a.z) * t, a.w + (sign * b.w - a.w) * t});
    }

    // The model turns about its own centre, with its lowest point on the ground at (-7, 0, 1).
    void place_model()
    {
        const lykoware::math::Quat turn = lykoware::math::from_axis_angle({0.F, 1.F, 0.F}, model_turn_);
        const lykoware::math::Vec3 centre{model_centre_.x * model_scale_, model_base_ * model_scale_, model_centre_.z * model_scale_};
        const lykoware::math::Vec3 offset = lykoware::math::rotate(turn, centre);
        auto* transform = entities_.get<lykoware::Transform>(model_);
        transform->position = lykoware::math::DVec3{-7.0, 0.0, 1.0} + (-1.F * offset);
        transform->rotation = turn;
    }

    // Gravity changes a crate's velocity by about 0.16 m/s per 60 Hz tick. A change of more than
    // 1 m/s in one tick is a collision; 6 m/s or more is the hardest knock. A short quiet time
    // after each knock stops a crate resting on another from rattling.
    void find_impacts(float seconds)
    {
        for (std::size_t i = 0; i < crates_.size(); ++i) {
            const auto* body = entities_.get<lykoware::physics_ecs::RigidBody>(crates_[i]);
            const auto state = body != nullptr ? physics_.state(body->id()) : std::nullopt;
            if (!state) {
                continue;
            }
            const float jolt = lykoware::math::length(state->linear_velocity - last_velocity_[i]);
            last_velocity_[i] = state->linear_velocity;
            quiet_[i] = std::max(0.F, quiet_[i] - seconds);
            if (thump_ && jolt > 1.F && quiet_[i] == 0.F) {
                impacts_.push_back({crates_[i], std::min(jolt / 6.F, 1.F)});
                quiet_[i] = 0.12F;
            }
        }
    }

    // The course's terrain and blocks as static bodies, each drawn.
    void add_course(lykoware::render::MeshId terrain)
    {
        std::string error;
        lykoware::physics::BodyDesc ground;
        ground.shape = obstacle_course::terrain();
        ground.motion = lykoware::physics::MotionType::Static;
        ground.position = obstacle_course::terrain_origin;
        lykoware::ecs::Entity entity = entities_.create();
        if (!link_.add_body(entity, ground, error)) {
            lykoware::log(lykoware::LogLevel::error, "Course terrain: " + error);
        }
        entities_.add<lykoware::render::MeshRenderer>(
            entity, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.38F, 0.55F, 0.32F}, terrain});
        for (const obstacle_course::Block& block : obstacle_course::blocks()) {
            lykoware::physics::BodyDesc desc;
            desc.shape = lykoware::physics::BoxShape{block.half};
            desc.motion = lykoware::physics::MotionType::Static;
            desc.position = block.centre;
            desc.rotation = block.rotation;
            entity = entities_.create();
            if (!link_.add_body(entity, desc, error)) {
                lykoware::log(lykoware::LogLevel::error, "Course block: " + error);
            }
            entities_.add<lykoware::render::MeshRenderer>(
                entity, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, block.half * 2.F, block.colour});
        }
    }

    void drop()
    {
        constexpr lykoware::math::Vec3 palette[] = {
            {0.85F, 0.25F, 0.2F}, {0.2F, 0.7F, 0.3F}, {0.2F, 0.4F, 0.85F}, {0.9F, 0.75F, 0.2F}, {0.7F, 0.3F, 0.8F}};
        std::string error;
        for (int row = 0; row < grid; ++row) {
            for (int column = 0; column < grid; ++column) {
                lykoware::physics::BodyDesc crate;
                crate.shape = lykoware::physics::BoxShape{{0.5F, 0.5F, 0.5F}};
                crate.position = drop_centre_
                    + lykoware::math::DVec3{(column - grid / 2) * 1.6, 4.0 + 1.3 * row + 0.4 * column, (row - grid / 2) * 1.6};
                // A small turn about a tilted axis so the crates tumble as they land.
                crate.rotation = lykoware::math::from_axis_angle({1.F, 0.F, 1.F}, 0.15F * static_cast<float>(row - column));
                const lykoware::ecs::Entity entity = entities_.create();
                if (link_.add_body(entity, crate, error)) {
                    entities_.add<lykoware::render::MeshRenderer>(
                        entity, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {1.F, 1.F, 1.F},
                                                               palette[(row + column) % 5]});
                    if (thump_) {
                        // Each crate knocks at its own pitch; heard fully within 4 m, silent past 80 m.
                        lykoware::audio::AudioSource source;
                        source.sound = thump_;
                        source.pitch = 0.8F + 0.08F * static_cast<float>((row * grid + column) % 6);
                        source.falloff = {4.F, 80.F};
                        entities_.add<lykoware::audio::AudioSource>(entity, source);
                    }
                    crates_.push_back(entity);
                    before_.push_back(pose(entity));
                    last_velocity_.push_back({});
                    quiet_.push_back(0.F);
                }
            }
        }
    }

    lykoware::physics::World& physics_;
    std::shared_ptr<const lykoware::audio::Sound> thump_;
    // The ECS world before the link; the physics world outlives both (ADR 0023).
    lykoware::ecs::World entities_;
    lykoware::physics_ecs::Link link_;
    std::vector<lykoware::ecs::Entity> crates_;
    std::vector<lykoware::math::Vec3> last_velocity_;
    std::vector<float> quiet_;
    std::vector<Pose> before_;
    std::vector<Impact> impacts_;
    double elapsed_ = 0.0;
    // Where the crates fall: the middle of the flat ground, or beside the course.
    lykoware::math::DVec3 drop_centre_{};
    lykoware::ecs::Entity model_{};
    float model_scale_ = 1.F;
    lykoware::math::Vec3 model_centre_{};
    float model_base_ = 0.F;
    float model_turn_ = 0.F;
};

// Drawing-only procedural mannequin (ADR 0052), driven by the existing controller's speed.
// Idle/walk blending never changes physics. Hidden in first person.
class PlayerBody {
public:
    PlayerBody(lykoware::ecs::World& entities, lykoware::render::MeshId mesh, training_mannequin::Asset asset, bool imported, float scale, lykoware::math::Vec3 offset, lykoware::math::Quat visual_rotation, std::optional<rig_roles::Indices> foot_roles, double clearance)
        : visual_scale_{scale}
        , visual_offset_{offset}
        , visual_rotation_{visual_rotation}
        , imported_{imported}
        , asset_{std::move(asset)}
        , mesh_{mesh}
        , entities_{entities}
        , body_{entities.create()}
        , visor_{entities.create()}
    {
        if (foot_roles) {
            foot_.emplace();
            std::string reason;
            if (!foot_->configure(asset_.skeleton,*foot_roles,reason,clearance)) { throw std::invalid_argument("Foot IK calibration: "+reason); }
            corrected_.resize(asset_.skeleton.joints.size());
        }
        entities_.add<lykoware::Transform>(body_, lykoware::Transform{});
        entities_.add<lykoware::Transform>(visor_, lykoware::Transform{});
        playback_.advance(asset_, 0.F, 0.F);
        if(asset_.graph){socket_poses_.resize(asset_.graph->sockets.size());}
        entities_.add<lykoware::render::SkinPose>(body_, lykoware::render::SkinPose{playback_.matrices});
    }

    void reset_animation()
    {
        playback_.reset();
        if (foot_) { foot_->reset(); }
    }

    // Draws the body `alpha` of the way between the last two steps (ADR 0048).
    void place(const lykoware::PlayerCharacter& player, float alpha, float seconds, const lykoware::physics::World& world)
    {
        const lykoware::math::DVec3 feet = player.drawn_feet(alpha);
        const lykoware::math::Quat facing = lykoware::math::from_axis_angle({0.F, 1.F, 0.F}, player.drawn_facing(alpha));
        auto* body = entities_.get<lykoware::Transform>(body_);
        body->position = feet + lykoware::math::rotate(facing, visual_offset_);
        body->rotation = facing * visual_rotation_;
        auto* visor = entities_.get<lykoware::Transform>(visor_);
        visor->rotation = facing;

        const auto state = player.state();
        const float speed = state.ground == lykoware::physics::GroundState::OnGround
            ? std::hypot(state.velocity.x, state.velocity.z) : 0.F;
        playback_.advance_character(asset_, seconds, state);
        ++motion_frames_[static_cast<std::size_t>(playback_.selector.state())];
        if (playback_.used_fallback) { ++motion_fallback_frames_; }
        std::span<const lykoware::animation::JointPose> final_pose=playback_.blended;
        const auto base_palette = foot_ ? palette_hash(playback_.matrices) : 0;
        if (foot_) {
            const auto local_offset=lykoware::math::rotate(lykoware::math::conjugate(visual_rotation_),visual_offset_);
            const foot_placement::Placement placement{feet,facing*visual_rotation_,visual_scale_,
                {local_offset.x,local_offset.y,local_offset.z}};
            std::string reason;
            const bool active=state.ground==lykoware::physics::GroundState::OnGround && speed<0.2F
                && player.camera().current_distance()>=0.5F;
            if (foot_->update(playback_.blended,placement,world,seconds,active,corrected_,reason)) {
                lykoware::animation::skinning_matrices(asset_.skeleton,corrected_,playback_.matrices);
                final_pose=corrected_;
            } else {
                ++foot_failures_;
                foot_->reset();
                if (foot_failures_==1) { lykoware::log(lykoware::LogLevel::warning,"Foot IK fallback: "+reason); }
            }
        }
        if(asset_.graph){
            if(!lykoware::animation::evaluate_sockets(asset_.skeleton,final_pose,asset_.graph->sockets,socket_poses_,socket_error_)){throw std::invalid_argument(socket_error_);}
        }
        if (!imported_) { const auto face = lykoware::animation::skin_point(playback_.matrices, {2, 0, 0, 0}, {1.F, 0.F, 0.F, 0.F},
                                                          {0.F, 1.58F, -0.18F});
        visor->position = feet + lykoware::math::rotate(facing, face); }
        entities_.get<lykoware::render::SkinPose>(body_)->matrices = playback_.matrices;
        if (foot_) {
            const auto expected=palette_hash(playback_.matrices);
            const auto actual=palette_hash(entities_.get<lykoware::render::SkinPose>(body_)->matrices);
            if (expected!=base_palette && actual==expected) { ++foot_applied_frames_; }
        }
        const bool shown = player.camera().current_distance() >= 0.5F;
        if (shown && !entities_.has<lykoware::render::MeshRenderer>(body_)) {
            entities_.add<lykoware::render::MeshRenderer>(
                body_, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {visual_scale_, visual_scale_, visual_scale_}, {1.F, 1.F, 1.F}, mesh_});
            if (!imported_) { entities_.add<lykoware::render::MeshRenderer>(
                visor_, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {0.3F, 0.08F, 0.06F}, {0.95F, 0.82F, 0.3F}}); }
        } else if (!shown && entities_.has<lykoware::render::MeshRenderer>(body_)) {
            entities_.remove<lykoware::render::MeshRenderer>(body_);
            entities_.remove<lykoware::render::MeshRenderer>(visor_);
        }
    }

    void report() const
    {
        std::string motion = "Locomotion diagnostics:";
        for (std::size_t i = 0; i < motion_frames_.size(); ++i) {
            motion += " " + std::string(locomotion::name(static_cast<locomotion::State>(i))) + "=" + std::to_string(motion_frames_[i]);
        }
        lykoware::log(lykoware::LogLevel::info, motion + " fallback_frames=" + std::to_string(motion_fallback_frames_));
        if (foot_) {
            const auto& s=foot_->stats();
            lykoware::log(lykoware::LogLevel::info,"Foot IK diagnostics: frames="+std::to_string(s.frames)
                +" contacts="+std::to_string(s.contacts)+" misses="+std::to_string(s.misses)
                +" clamped="+std::to_string(s.clamped)+" resets="+std::to_string(s.resets)
                +" failures="+std::to_string(foot_failures_)+" applied_frames="+std::to_string(foot_applied_frames_));
        }
    }
private:
    static std::uint64_t palette_hash(const std::vector<lykoware::math::Mat4>& matrices)
    {
        std::uint64_t hash=14695981039346656037ULL;
        for (const auto& matrix:matrices) {
            for (const auto& column:matrix.m) {
                for (float value:column) { hash^=std::bit_cast<std::uint32_t>(value);hash*=1099511628211ULL; }
            }
        }
        return hash;
    }
    lykoware::animation::Pose corrected_;
    std::vector<lykoware::math::Mat4> socket_poses_;
    std::string socket_error_;
    std::uint64_t foot_applied_frames_=0;
    std::array<std::uint64_t,6> motion_frames_{};
    std::uint64_t motion_fallback_frames_ = 0;
    std::optional<foot_placement::Controller> foot_;
    std::uint64_t foot_failures_=0;
    float visual_scale_ = 1.F;
    lykoware::math::Vec3 visual_offset_{};
    lykoware::math::Quat visual_rotation_{};
    bool imported_ = false;
    training_mannequin::Asset asset_;
    training_mannequin::Playback playback_;
    lykoware::render::MeshId mesh_;
    lykoware::ecs::World& entities_;
    lykoware::ecs::Entity body_;
    lykoware::ecs::Entity visor_;
};

// Uploads a glTF model's textures and its mesh as parts with their materials (ADR 0038).
std::optional<lykoware::render::MeshId> upload_model(lykoware::render::Renderer& renderer, const lykoware::gltf::Model& model,
                                                     lykoware::rhi::RhiError& error, training_mannequin::Asset* skin = nullptr)
{
    std::vector<lykoware::render::TextureId> textures;
    for (const lykoware::gltf::Texture& texture : model.textures) {
        lykoware::render::TextureSettings settings;
        settings.wrap = texture.sampling.wrap == lykoware::gltf::Wrap::ClampToEdge      ? lykoware::rhi::TextureWrap::ClampToEdge
            : texture.sampling.wrap == lykoware::gltf::Wrap::MirroredRepeat             ? lykoware::rhi::TextureWrap::MirroredRepeat
                                                                                         : lykoware::rhi::TextureWrap::Repeat;
        settings.filter = texture.sampling.nearest ? lykoware::rhi::TextureFilter::Nearest : lykoware::rhi::TextureFilter::Linear;
        settings.mipmaps = texture.sampling.mipmaps;
        const auto id = renderer.add_texture(model.images[texture.image], settings, error);
        if (!id) {
            return std::nullopt;
        }
        textures.push_back(*id);
    }
    std::vector<lykoware::render::MeshPart> parts;
    for (const lykoware::gltf::Part& part : model.parts) {
        lykoware::render::MeshPart mesh_part{part.first_index, part.index_count};
        if (part.material) {
            const lykoware::gltf::Material& material = model.materials[*part.material];
            mesh_part.colour = {material.base_colour.x, material.base_colour.y, material.base_colour.z};
            if (material.base_colour_texture) {
                mesh_part.texture = textures[*material.base_colour_texture];
            }
        }
        parts.push_back(mesh_part);
    }
    if (skin) { skin->parts = std::move(parts); return lykoware::render::MeshId{}; }
    return renderer.add_mesh(model.mesh, parts, error);
}

// Draws the falling-crates scene through the RHI swapchain.
int present_loop(lykoware::Window& window, const WindowOptions& options, const ShaderWords& shaders,
                 const std::optional<lykoware::gltf::Model>& model)
{
    lykoware::rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware sandbox";
    instance_config.validation = lykoware::rhi::Validation::IfAvailable;
    for (const char* name : window.vulkan_instance_extensions()) {
        instance_config.extensions.emplace_back(name);
    }
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(instance_config, error);
    if (!instance) {
        lykoware::log(lykoware::LogLevel::error, "Vulkan instance: " + error.message);
        return unavailable(error.failure) ? exit_unsupported : EXIT_FAILURE;
    }
    lykoware::rhi::DeviceConfig device_config;
    device_config.extensions = {lykoware::rhi::swapchain_device_extension};
    auto device = lykoware::rhi::Device::create(*instance, device_config, error);
    if (!device) {
        lykoware::log(lykoware::LogLevel::error, "Vulkan device: " + error.message);
        return unavailable(error.failure) ? exit_unsupported : EXIT_FAILURE;
    }
    lykoware::rhi::SwapchainConfig swapchain_config;
    swapchain_config.depth = true;
    auto swapchain = lykoware::rhi::Swapchain::create(*instance, *device, window, swapchain_config, error);
    if (!swapchain) {
        lykoware::log(lykoware::LogLevel::error, "Vulkan swapchain: " + error.message);
        return unavailable(error.failure) ? exit_unsupported : EXIT_FAILURE;
    }
    auto renderer = lykoware::render::Renderer::create(*device, swapchain->color_format(), shaders.vertex,
                                                       shaders.fragment, error, shaders.skin);
    if (!renderer) {
        lykoware::log(lykoware::LogLevel::error, "Renderer: " + error.message);
        return EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info, "Drawing with Vulkan on " + device->info().name + ".");

    std::string physics_error;
    auto physics = lykoware::physics::World::create({}, physics_error);
    if (!physics) {
        lykoware::log(lykoware::LogLevel::error, "Physics: " + physics_error);
        return EXIT_FAILURE;
    }
    // Sound (ADR 0033, ADR 0034): the platform's device, or the dummy driver when the window is
    // headless. Without a device the sandbox runs silently.
    std::unique_ptr<lykoware::audio::Output> audio;
    if (!options.no_audio) {
        lykoware::audio::OutputConfig audio_config;
        audio_config.driver = options.dummy || options.offscreen ? lykoware::audio::OutputDriver::Dummy
                                                                 : lykoware::audio::OutputDriver::Platform;
        std::string audio_error;
        audio = lykoware::audio::Output::open(audio_config, audio_error);
        if (audio) {
            audio->lock()->set_master_gain(0.8F);
            audio->resume();
            lykoware::log(lykoware::LogLevel::info, "Playing sound through SDL's " + audio->driver_name() + " driver.");
        } else {
            lykoware::log(lykoware::LogLevel::warning, "No sound: " + audio_error);
        }
    }
    std::optional<lykoware::render::MeshId> course_terrain;
    if (options.play) {
        course_terrain = renderer->add_mesh(course_terrain_mesh(), error);
        if (!course_terrain) {
            lykoware::log(lykoware::LogLevel::error, "Course terrain: " + error.message);
            return EXIT_FAILURE;
        }
    }
    auto scene = std::make_unique<CrateScene>(*physics, audio ? make_thump() : nullptr, course_terrain);
    if (model) {
        const std::optional<lykoware::render::MeshId> mesh = upload_model(*renderer, *model, error);
        if (!mesh) {
            lykoware::log(lykoware::LogLevel::error, "Model: " + error.message);
            return EXIT_FAILURE;
        }
        scene->add_model(*mesh, lykoware::bounds(model->mesh));
    }
    lykoware::render::Camera camera{{0.0, 7.0, 15.0}, {0.0, 1.5, 0.0}};

    // --play (ADR 0047): a character on the course. Click to hold the mouse for mouse-look; Esc
    // lets it go; R goes back to the start.
    lykoware::PlayerConfig player_config;
    player_config.body.position = obstacle_course::start;
    lykoware::character::Capsule body_alignment;
    std::string alignment_error;
    if(!lykoware::character::capsule(options.character_settings,body_alignment,alignment_error)){throw std::invalid_argument(alignment_error);}
    player_config.body.radius=body_alignment.radius;player_config.body.cylinder_half_height=body_alignment.cylinder_half_height;
    player_config.eye_height=body_alignment.eye_height;
    player_config.max_turn_rate=options.character_settings.turn_degrees_per_second.value_or(lykoware::character::default_turn_degrees_per_second)*std::numbers::pi_v<float>/180.F;
    const lykoware::PlayerInputConfig input_config;
    std::unique_ptr<lykoware::PlayerCharacter> player;
    std::optional<PlayerBody> player_body;
    bool warned_mouse = false;
    if (options.play) {
        std::string player_error;
        player = lykoware::PlayerCharacter::create(*physics, player_config, player_error);
        if (!player) {
            lykoware::log(lykoware::LogLevel::error, "Player: " + player_error);
            return EXIT_FAILURE;
        }
        auto mannequin = training_mannequin::make();
        std::optional<rig_roles::Indices> resolved_roles;
        if (!options.character.empty()) {
            std::string reason;
            auto imported = lykoware::gltf::load_skinned(utf8_path(options.character), reason);
            if (!imported || imported->clips.empty()) {
                lykoware::log(lykoware::LogLevel::error, "Character: " + reason + " (requires animation clips)");
                return EXIT_FAILURE;
            }
            lykoware::character::Inspection inspection;
            if(!lykoware::character::inspect(*imported,options.character_settings,inspection,reason)){throw std::invalid_argument(reason);}
            auto choose = [&](const std::string& name, std::size_t fallback) -> lykoware::animation::Clip {
                const auto index=lykoware::character::find_clip(*imported,name,fallback,reason);
                if(!index){throw std::invalid_argument(reason);}return imported->clips[*index];
            };
            mannequin.run = {}; mannequin.jump = {}; mannequin.fall = {}; mannequin.land = {};
            const std::array<lykoware::animation::Clip*,4> motion{{&mannequin.run,&mannequin.jump,&mannequin.fall,&mannequin.land}};
            for (std::size_t i = 0; i < motion.size(); ++i) {
                if (!options.locomotion_clips[i].empty()) { *motion[i] = choose(options.locomotion_clips[i],0); }
            }
            mannequin.idle = choose(options.idle_clip, 0);
            mannequin.walk = choose(options.walk_clip, imported->clips.size() > 1 ? 1 : 0);
            mannequin.mesh = imported->model.mesh;
            mannequin.joints = imported->joints;
            mannequin.weights = imported->weights;
            mannequin.skeleton = imported->skeleton;
            mannequin.graph_clips=imported->clips;
            if (options.bones) {
                rig_roles::Indices roles;
                if (!rig_roles::resolve(mannequin.skeleton, *options.bones, roles, reason)) { throw std::invalid_argument(reason); }
                resolved_roles=roles;
                for (std::size_t role = 0; role < roles.size(); ++role) {
                    lykoware::log(lykoware::LogLevel::info, "Rig role " + std::string(rig_roles::names[role])
                        + " = " + (*options.bones)[role] + " [" + std::to_string(roles[role]) + "]");
                }
            }

            if (!upload_model(*renderer, imported->model, error, &mannequin)) { return EXIT_FAILURE; }
        }

        if(!options.character_settings.animation_graph.empty()){
            auto graph=std::make_shared<lykoware::animation::Graph>();std::string reason;
            if(!lykoware::animation::load_graph(options.character_settings.animation_graph,*graph,reason)){throw std::invalid_argument(reason);}
            if(mannequin.graph_clips.empty()){mannequin.graph_clips={mannequin.idle,mannequin.walk,mannequin.run,mannequin.jump,mannequin.fall,mannequin.land};}
            mannequin.graph=std::move(graph);
        }
        float visual_scale = 1.F;
        lykoware::math::Vec3 visual_offset{};
        lykoware::math::Quat visual_rotation{};
        const auto bounds = lykoware::bounds(mannequin.mesh);
        if (options.character_height || options.character_settings.yaw_degrees) {
            lykoware::character::Alignment placement;
            std::string reason;
            if (!lykoware::character::align(bounds, options.character_height.value_or(bounds.max.y-bounds.min.y), options.character_settings.yaw_degrees.value_or(0), placement, reason)) {
                throw std::invalid_argument(reason);
            }
            visual_scale = placement.scale;
            visual_offset = placement.offset;
            visual_rotation = placement.rotation;
        }
        lykoware::log(lykoware::LogLevel::info, "Character inspection: " + std::to_string(mannequin.skeleton.joints.size())
            + " joints; bind height " + std::to_string(static_cast<double>(bounds.max.y) - bounds.min.y)
            + "; visual scale " + std::to_string(visual_scale)
            + "; yaw " + std::to_string(options.character_settings.yaw_degrees.value_or(0))
            + "; capsule height " + std::to_string(body_alignment.height) + "; eye height " + std::to_string(body_alignment.eye_height));
        const auto mesh = renderer->add_skinned_mesh(mannequin.mesh, mannequin.joints, mannequin.weights,
                                                      mannequin.skeleton.joints.size(), mannequin.parts, error);
        if (!mesh) {
            lykoware::log(lykoware::LogLevel::error, "Mannequin upload: " + error.message);
            return EXIT_FAILURE;
        }
        player_body.emplace(scene->entities(), *mesh, std::move(mannequin), !options.character.empty(), visual_scale, visual_offset, visual_rotation, options.foot_ik ? resolved_roles : std::nullopt, options.foot_clearance);
        if (options.foot_ik) { lykoware::log(lykoware::LogLevel::info,"Foot IK: stationary terrain alignment enabled"); }
        lykoware::log(lykoware::LogLevel::info, options.character.empty() ? "Animation: GPU-skinned training mannequin (7 joints), idle/walk/run/jump/fall/land clips." : "Animation: GPU-skinned imported character; missing run/air/landing clips fall back to walk/idle.");
        lykoware::log(lykoware::LogLevel::info,
                      "Play: WASD or the left stick to move, Shift or L3 to run, Space or South to jump, mouse wheel or "
                      "d-pad to zoom. Click to look with the mouse, Esc to let it go, R to start again.");
    }
    lykoware::audio::SpatialAudio spatial;
    std::uint64_t knocks = 0;
    const lykoware::rhi::ClearColor sky{0.45F, 0.6F, 0.8F, 1.F};

    // The simulation advances only in fixed 60 Hz ticks (ADR 0017), so the crates fall at the
    // same speed at any frame rate. Frames are drawn between the last two ticks (ADR 0048).
    std::string clock_error;
    std::optional<lykoware::FixedStep> simulation = lykoware::FixedStep::create({}, clock_error);
    if (!simulation) {
        lykoware::log(lykoware::LogLevel::error, "Simulation clock: " + clock_error);
        return EXIT_FAILURE;
    }
    lykoware::FrameClock frame_clock;

    // --trace: CPU zones for each frame's parts, and the GPU's time for each frame (ADR 0039).
    std::unique_ptr<lykoware::CpuTrace> trace;
    if (!options.trace.empty()) {
        trace = std::make_unique<lykoware::CpuTrace>();
    }
    GpuFrameLog gpu_frames{trace.get()};
    // A zone only while tracing.
    const auto zone = [&trace](std::string_view name) {
        return trace ? std::make_unique<lykoware::CpuTrace::Zone>(*trace, name) : nullptr;
    };

    bool ok = true;
    std::uint64_t frame = 0;
    while (window.pump()) {
        auto frame_zone = zone("sandbox.frame");
        const std::int64_t lap = frame_clock.lap();
        const float frame_seconds = static_cast<float>(static_cast<double>(lap) * 1e-9);
        if (player) {
            const lykoware::InputState& input = window.input();
            std::string mouse_error;
            if (!window.relative_mouse() && window.has_focus()
                && input.mouse.buttons[static_cast<std::size_t>(lykoware::MouseButton::Left)].pressed
                && !window.set_relative_mouse(true, mouse_error) && !warned_mouse) {
                lykoware::log(lykoware::LogLevel::warning, "No mouse-look: " + mouse_error);
                warned_mouse = true;
            }
            if (window.relative_mouse() && input.keys[static_cast<std::size_t>(lykoware::Key::Escape)].pressed) {
                (void)window.set_relative_mouse(false, mouse_error);
            }
            // Back to the start on R, or after falling off the edge of the world.
            if (input.keys[static_cast<std::size_t>(lykoware::Key::R)].pressed || player->state().position.y < -30.0) {
                player.reset();
                std::string player_error;
                player = lykoware::PlayerCharacter::create(*physics, player_config, player_error);
                if (!player) {
                    lykoware::log(lykoware::LogLevel::error, "Player: " + player_error);
                    ok = false;
                    break;
                }
                if (player_body) { player_body->reset_animation(); }
            }
            player->steer(lykoware::read_player_intent(input, input_config, frame_seconds, window.relative_mouse()));
        }
        const lykoware::FrameSteps steps = simulation->advance(lap);
        {
            auto simulate_zone = zone("sandbox.simulate");
            for (std::uint32_t tick = 0; tick < steps.ticks; ++tick) {
                if (player) {
                    player->before_step(static_cast<float>(simulation->tick_seconds()));
                }
                scene->tick(static_cast<float>(simulation->tick_seconds()));
            }
        }
        // Draw between the last two steps, by how far real time has run past the latest one.
        const auto alpha = static_cast<float>(steps.alpha);
        scene->present(alpha);
        if (player) {
            camera = player->view(frame_seconds, alpha);
            player_body->place(*player, alpha, frame_seconds, *physics);
        }
        const auto listener = lykoware::audio::Listener::looking_at(camera.eye, camera.target, camera.up);
        if (audio) {
            auto audio_zone = zone("sandbox.audio");
            const std::vector<CrateScene::Impact> impacts = scene->take_impacts();
            auto mixer = audio->lock();
            for (const CrateScene::Impact& impact : impacts) {
                std::string sound_error; // a full mixer skips the knock
                if (spatial.play(scene->entities(), impact.crate, listener, *mixer, sound_error, impact.strength)) {
                    ++knocks;
                }
            }
            spatial.update(scene->entities(), listener, *mixer);
        }
        {
            auto render_zone = zone("sandbox.render");
            if (!renderer->draw(scene->entities(), camera, *swapchain, sky, error)) {
                lykoware::log(lykoware::LogLevel::error, "Vulkan frame: " + error.message);
                ok = false;
                break;
            }
        }
        gpu_frames.collect(*swapchain);
        if (window.minimized()) {
            // The swapchain draws nothing while minimised (ADR 0040), so don't spin.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        ++frame;
        if (options.has_frames && frame >= options.frames) {
            break;
        }
    }
    // The last frames' GPU times, once the GPU has finished them.
    if (device->wait_idle(error)) {
        gpu_frames.collect(*swapchain);
    }
    const std::uint64_t presented = swapchain->frames_presented();
    const std::size_t drawn = renderer->last_draw_count();
    if (player_body) { player_body->report(); }
    if (player) {
        const lykoware::math::DVec3 feet = player->state().position;
        lykoware::log(lykoware::LogLevel::info, "The player ended at (" + std::to_string(feet.x) + ", " + std::to_string(feet.y)
                                                    + ", " + std::to_string(feet.z) + ").");
        player.reset();
    }
    if (audio) {
        lykoware::log(lykoware::LogLevel::info, "Played " + std::to_string(knocks) + " crate knocks.");
        audio.reset();
    }
    // The swapchain destructor waits for the GPU, so the renderer's pipeline
    // and buffers are idle when they are destroyed next.
    swapchain.reset();
    renderer.reset();
    scene.reset();
    device.reset();
    if (!ok) {
        return EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info,
                  "Presented " + std::to_string(presented) + " frames of " + std::to_string(drawn)
                      + " objects; ran " + std::to_string(simulation->tick()) + " simulation ticks at "
                      + std::to_string(simulation->ticks_per_second()) + " Hz.");
    if (simulation->total_dropped_ns() > 0) {
        lykoware::log(lykoware::LogLevel::warning,
                      "The simulation fell behind and dropped "
                          + std::to_string(simulation->total_dropped_ns() / 1'000'000) + " ms.");
    }
    if (gpu_frames.frames() > 0) {
        char average[32] = {};
        const auto written = std::to_chars(average, average + sizeof(average) - 1, gpu_frames.average_ms(),
                                           std::chars_format::fixed, 3);
        *written.ptr = '\0';
        lykoware::log(lykoware::LogLevel::info, std::string{"GPU time per frame: "} + average + " ms on average over "
                                                    + std::to_string(gpu_frames.frames()) + " frames.");
    } else {
        lykoware::log(lykoware::LogLevel::info, "No GPU frame times: the device has no timestamps.");
    }
    if (instance->validation_error_count() != 0) {
        lykoware::log(lykoware::LogLevel::error,
                      std::to_string(instance->validation_error_count()) + " Vulkan validation errors.");
        return EXIT_FAILURE;
    }
    if (trace) {
        if (!trace->write(utf8_path(options.trace))) {
            lykoware::log(lykoware::LogLevel::error, "Could not write the trace.");
            return EXIT_FAILURE;
        }
        lykoware::log(lykoware::LogLevel::info, "Wrote CPU and GPU trace.");
    }
    return EXIT_SUCCESS;
}

int run_window(const std::vector<std::string>& args)
{
    bool saw_window = false;
    WindowOptions options;
    auto flag = [](bool& seen, const std::string& name) {
        if (seen) {
            throw std::invalid_argument("Duplicate option: " + name);
        }
        seen = true;
    };
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& option = args[index];
        if (option == "--window") {
            flag(saw_window, option);
        } else if (option == "--dummy") {
            flag(options.dummy, option);
        } else if (option == "--offscreen") {
            flag(options.offscreen, option);
        } else if (option == "--vulkan") {
            flag(options.vulkan, option);
        } else if (option == "--no-audio") {
            flag(options.no_audio, option);
        } else if (option == "--play") {
            flag(options.play, option);
        } else if (option == "--character-profile") {
            if (!options.character_profile.empty() || index + 1 >= args.size() || args[index + 1].empty() || args[index + 1].starts_with("--")) {
                throw std::invalid_argument("Duplicate or missing --character-profile");
            }
            options.character_profile = args[++index];
        } else if (option == "--character-height") {
            if (options.character_height || index + 1 >= args.size()) {
                throw std::invalid_argument("Duplicate or missing --character-height");
            }
            const auto& text = args[++index];
            float height = 0.F;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), height);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(height)
                || height < 0.1F || height > 10.F) {
                throw std::invalid_argument("--character-height must be 0.1..10 metres");
            }
            options.character_height = height;
        } else if (option == "--character" || option == "--idle-clip" || option == "--walk-clip") {
            auto& value = option == "--character" ? options.character : (option == "--idle-clip" ? options.idle_clip : options.walk_clip);
            if (!value.empty() || index + 1 >= args.size() || args[index + 1].empty() || args[index + 1].starts_with("--")) {
                throw std::invalid_argument("Duplicate or missing value: " + std::string(option));
            }
            value = args[++index];
        } else if (option == "--model") {
            if (!options.model.empty()) {
                throw std::invalid_argument("Duplicate option: --model");
            }
            if (index + 1 >= args.size() || args[index + 1].empty() || args[index + 1].starts_with("--")) {
                throw std::invalid_argument("Missing value for --model");
            }
            options.model = args[++index];
        } else if (option == "--frames") {
            if (options.has_frames) {
                throw std::invalid_argument("Duplicate option: --frames");
            }
            if (index + 1 >= args.size() || args[index + 1].empty() || args[index + 1].starts_with("--")) {
                throw std::invalid_argument("Missing value for --frames");
            }
            options.frames = parse_frames(args[++index]);
            options.has_frames = true;
        } else if (option == "--trace") {
            if (!options.trace.empty()) {
                throw std::invalid_argument("Duplicate option: --trace");
            }
            if (index + 1 >= args.size() || args[index + 1].empty() || args[index + 1].starts_with("--")) {
                throw std::invalid_argument("Missing value for --trace");
            }
            options.trace = args[++index];
        } else {
            throw std::invalid_argument("Unknown option: " + option + ". Use --help.");
        }
    }
    if (!saw_window) {
        throw std::invalid_argument("Expected --window.");
    }
    if (options.dummy && options.offscreen) {
        throw std::invalid_argument("--dummy and --offscreen are different video drivers; choose one.");
    }
    if (options.dummy && options.vulkan) {
        throw std::invalid_argument("--dummy cannot host Vulkan; use --offscreen for a headless Vulkan window.");
    }
    if ((options.dummy || options.offscreen) && !options.has_frames) {
        throw std::invalid_argument("--window --dummy or --offscreen requires --frames N so it can exit without a display.");
    }

    if (!options.model.empty() && !options.vulkan) {
        throw std::invalid_argument("--model needs --vulkan to draw it.");
    }
    if (!options.character_profile.empty()) {
        if (!options.play || !options.character.empty() || !options.idle_clip.empty() || !options.walk_clip.empty() || options.character_height) {
            throw std::invalid_argument("--character-profile requires --play and cannot mix with character flags");
        }
        const auto profile_path = utf8_path(options.character_profile);
        std::string reason;
        if (!lykoware::character::load_profile(profile_path,options.character_settings,reason)) { throw std::invalid_argument(reason); }
        const auto& profile=options.character_settings;
        const auto encoded=profile.model.u8string();options.character.assign(encoded.begin(),encoded.end());
        options.idle_clip=profile.clips[0];options.walk_clip=profile.clips[1];
        for(std::size_t i=0;i<options.locomotion_clips.size();++i){options.locomotion_clips[i]=profile.clips[i+2];}
        options.character_height=profile.height;options.bones=profile.bones;
        options.foot_ik=profile.foot_ik;options.foot_clearance=profile.foot_clearance;
    }
    options.character_settings.model=utf8_path(options.character);
    options.character_settings.height=options.character_height;
    options.character_settings.clips[0]=options.idle_clip;options.character_settings.clips[1]=options.walk_clip;
    for(std::size_t i=0;i<options.locomotion_clips.size();++i){options.character_settings.clips[i+2]=options.locomotion_clips[i];}
    options.character_settings.bones=options.bones;options.character_settings.foot_ik=options.foot_ik;
    options.character_settings.foot_clearance=options.foot_clearance;
    if (options.character_height && options.character.empty()) { throw std::invalid_argument("--character-height requires --character"); }
    if ((!options.character.empty() && !options.play) || ((!options.idle_clip.empty() || !options.walk_clip.empty()) && options.character.empty())) {
        throw std::invalid_argument("--character requires --play; clip selectors require --character");
    }
    if (options.play && !options.vulkan) {
        throw std::invalid_argument("--play needs --vulkan to draw it.");
    }
    if (!options.trace.empty() && (!options.vulkan || !options.has_frames)) {
        throw std::invalid_argument("--window --trace needs --vulkan and --frames N.");
    }

    ShaderWords shaders;
    if (options.vulkan && !read_shaders(shaders, options.play)) {
        return EXIT_FAILURE;
    }
    // The model is read before any window or GPU work, like the shaders, so a bad file fails
    // clearly on any machine.
    std::optional<lykoware::gltf::Model> model;
    if (!options.model.empty()) {
        std::string model_error;
        model = lykoware::gltf::load_model(utf8_path(options.model), model_error);
        if (!model) {
            lykoware::log(lykoware::LogLevel::error, "Model " + options.model + ": " + model_error);
            return EXIT_FAILURE;
        }
        lykoware::log(lykoware::LogLevel::info,
                      "Loaded model " + options.model + ": " + std::to_string(model->mesh.indices.size() / 3) + " triangles, "
                          + std::to_string(model->materials.size()) + " materials, " + std::to_string(model->images.size())
                          + " images.");
    }

    lykoware::WindowConfig config;
    config.title = "lykoware sandbox";
    config.width = 960;
    config.height = 540;
    config.resizable = true;
    config.visible = !options.dummy && !options.offscreen;
    config.vulkan = options.vulkan;
    config.driver = options.dummy ? lykoware::VideoDriver::Dummy
        : options.offscreen       ? lykoware::VideoDriver::Offscreen
                                  : lykoware::VideoDriver::Platform;
    std::string error;
    const std::unique_ptr<lykoware::Window> window = lykoware::Window::open(config, error);
    if (!window) {
        lykoware::log(lykoware::LogLevel::error, error);
        // A Vulkan window fails to open when no Vulkan loader exists.
        return options.vulkan ? exit_unsupported : EXIT_FAILURE;
    }
    lykoware::log(lykoware::LogLevel::info, "Opened sandbox window.");
    if (options.vulkan) {
        const int status = present_loop(*window, options, shaders, model);
        lykoware::log(lykoware::LogLevel::info, "Closed sandbox window.");
        return status;
    }
    if (options.has_frames) {
        for (std::uint64_t index = 0; index < options.frames; ++index) {
            if (!window->pump()) {
                break;
            }
        }
    } else {
        while (window->pump()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    lykoware::log(lykoware::LogLevel::info, "Closed sandbox window.");
    return EXIT_SUCCESS;
}

int run(const std::vector<std::string>& args)
{
    if (has_option(args, "--window")) {
        return run_window(args);
    }
    if (has_option(args, "--frames") || has_option(args, "--trace")) {
        return run_trace(args);
    }
    if (args.size() > 1) {
        throw std::invalid_argument("Expected at most one option. Use --help.");
    }
    if (args.size() == 1) {
        const std::string_view option{args[0]};
        if (option == "--version") {
            std::cout << "lykowareGE " << lykoware::version << '\n';
            return EXIT_SUCCESS;
        }
        if (option == "--help") {
            std::cout << "Usage: lykoware_sandbox [--version|--help]\n"
                         "       lykoware_sandbox --frames N --trace FILE\n"
                         "       lykoware_sandbox --window [--dummy | --offscreen] [--vulkan] [--no-audio]\n"
                         "                        [--model FILE] [--play [--character FILE --idle-clip NAME --walk-clip NAME]] [--frames N [--trace FILE]]\n"
                         "Without options, runs the console foundation check.\n"
                         "--frames with --trace runs N CPU iterations and writes a Chrome trace.\n"
                         "--window opens a window and pumps input until it closes.\n"
                         "--dummy selects the headless video driver and requires --frames.\n"
                         "--offscreen is headless too, can host Vulkan, and requires --frames.\n"
                         "--vulkan shows falling crates through the Vulkan RHI, with shaders from\n"
                         "  the shaders folder beside the executable; exit 3 when no usable Vulkan\n"
                         "  device exists, 1 when shaders are missing. Crates knock as they land,\n"
                         "  through the default sound device (the dummy driver when headless).\n"
                         "--no-audio runs --vulkan without sound.\n"
                         "--model FILE shows a glTF model (.gltf or .glb) beside the crates, with its\n"
                         "  base colour materials and textures (PNG or JPEG).\n"
                         "--play with --vulkan puts a character on an obstacle course beside the crates,\n"
                         "  with a follow camera: WASD or the left stick to move, Shift or L3 to run,\n"
                         "  Space or South to jump, the mouse wheel or d-pad to zoom (in to first\n"
                         "  person). Click to look with the mouse, Esc to let go, R to start again.\n"
                         "--trace FILE with --vulkan and --frames writes a Chrome trace of each frame's\n"
                         "  CPU zones and, where the GPU has timestamps, its GPU time.\n"
                         "--vulkan logs the average GPU time per frame on exit.\n";
            return EXIT_SUCCESS;
        }
        throw std::invalid_argument("Unknown option: " + std::string{option}
                                    + ". Use --help.");
    }

    lykoware::log(lykoware::LogLevel::info,
                  "Starting lykowareGE " + std::string{lykoware::version});
    lykoware::log(lykoware::LogLevel::info,
                  "Engine library linked; sandbox bootstrap ready.");
    lykoware::log(lykoware::LogLevel::info,
                  "Foundation check complete. Pass --window to open one.");
    return EXIT_SUCCESS;
}

int fail(const std::exception& error)
{
    lykoware::log(lykoware::LogLevel::error, error.what());
    return EXIT_FAILURE;
}

}

#ifdef _WIN32
namespace {
std::string utf8_from_wide(const wchar_t* text)
{
    if (text == nullptr) {
        throw std::runtime_error("Invalid Unicode command argument.");
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        throw std::runtime_error("Invalid Unicode command argument.");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(),
                            size, nullptr, nullptr) == 0) {
        throw std::runtime_error("Unable to convert command argument.");
    }
    result.pop_back();
    return result;
}
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.push_back(utf8_from_wide(argv[index]));
        }
        return run(arguments);
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        lykoware::log(lykoware::LogLevel::error, "Unknown startup failure.");
    }
    return EXIT_FAILURE;
}
#else
int main(int argc, char* argv[])
{
    try {
        std::vector<std::string> arguments;
        if (argc > 1) {
            arguments.assign(argv + 1, argv + argc);
        }
        return run(arguments);
    } catch (const std::exception& error) {
        return fail(error);
    } catch (...) {
        lykoware::log(lykoware::LogLevel::error, "Unknown startup failure.");
    }
    return EXIT_FAILURE;
}
#endif
