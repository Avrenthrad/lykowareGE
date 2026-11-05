#include <lykoware/audio.hpp>
#include <lykoware/audio_output.hpp>
#include <lykoware/audio_spatial.hpp>
#include <lykoware/clock.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/gltf.hpp>
#include <lykoware/log.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/paths.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/trace.hpp>
#include <lykoware/transform.hpp>
#include <lykoware/version.hpp>
#include <lykoware/window.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
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
};

// The engine's lit shader compiled for the sandbox, deployed by the build and
// by cmake --install into the "shaders" folder beside the executable (ADR 0016).
constexpr const char* vertex_shader_file = "sandbox_lit.vert.spv";
constexpr const char* fragment_shader_file = "sandbox_lit.frag.spv";

struct ShaderWords {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
};

// Reads the shader files before any window or GPU work, so a broken install
// fails clearly even on a machine without Vulkan.
bool read_shaders(ShaderWords& shaders)
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

// The falling-crates scene (ADR 0026): a grey ground and a grid of coloured crates. Physics
// moves the crate entities and the renderer draws every entity with a Transform and a
// MeshRenderer. With a sound, each crate has an AudioSource and reports its impacts (ADR 0034).
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

    CrateScene(lykoware::physics::World& physics, std::shared_ptr<const lykoware::audio::Sound> thump)
        : physics_(physics)
        , thump_(std::move(thump))
        , link_(entities_, physics)
    {
        lykoware::physics::BodyDesc ground;
        ground.shape = lykoware::physics::BoxShape{{12.F, 0.5F, 12.F}};
        ground.motion = lykoware::physics::MotionType::Static;
        ground.position = {0.0, -0.5, 0.0};
        const lykoware::ecs::Entity entity = entities_.create();
        std::string error;
        link_.add_body(entity, ground, error);
        entities_.add<lykoware::render::MeshRenderer>(
            entity, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {24.F, 1.F, 24.F}, {0.45F, 0.45F, 0.45F}});
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
            last_velocity_.clear();
            quiet_.clear();
            drop();
        }
    }

    lykoware::ecs::World& entities() { return entities_; }
    // Impacts since the last take_impacts(), oldest first.
    std::vector<Impact> take_impacts() { return std::exchange(impacts_, {}); }

private:
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

    void drop()
    {
        constexpr lykoware::math::Vec3 palette[] = {
            {0.85F, 0.25F, 0.2F}, {0.2F, 0.7F, 0.3F}, {0.2F, 0.4F, 0.85F}, {0.9F, 0.75F, 0.2F}, {0.7F, 0.3F, 0.8F}};
        std::string error;
        for (int row = 0; row < grid; ++row) {
            for (int column = 0; column < grid; ++column) {
                lykoware::physics::BodyDesc crate;
                crate.shape = lykoware::physics::BoxShape{{0.5F, 0.5F, 0.5F}};
                crate.position = {(column - grid / 2) * 1.6, 4.0 + 1.3 * row + 0.4 * column, (row - grid / 2) * 1.6};
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
    std::vector<Impact> impacts_;
    double elapsed_ = 0.0;
    lykoware::ecs::Entity model_{};
    float model_scale_ = 1.F;
    lykoware::math::Vec3 model_centre_{};
    float model_base_ = 0.F;
    float model_turn_ = 0.F;
};

// Uploads a glTF model's textures and its mesh as parts with their materials (ADR 0038).
std::optional<lykoware::render::MeshId> upload_model(lykoware::render::Renderer& renderer, const lykoware::gltf::Model& model,
                                                     lykoware::rhi::RhiError& error)
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
                                                       shaders.fragment, error);
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
    auto scene = std::make_unique<CrateScene>(*physics, audio ? make_thump() : nullptr);
    if (model) {
        const std::optional<lykoware::render::MeshId> mesh = upload_model(*renderer, *model, error);
        if (!mesh) {
            lykoware::log(lykoware::LogLevel::error, "Model: " + error.message);
            return EXIT_FAILURE;
        }
        scene->add_model(*mesh, lykoware::bounds(model->mesh));
    }
    const lykoware::render::Camera camera{{0.0, 7.0, 15.0}, {0.0, 1.5, 0.0}};
    const auto listener = lykoware::audio::Listener::looking_at(camera.eye, camera.target, camera.up);
    lykoware::audio::SpatialAudio spatial;
    std::uint64_t knocks = 0;
    const lykoware::rhi::ClearColor sky{0.45F, 0.6F, 0.8F, 1.F};

    // The simulation advances only in fixed 60 Hz ticks (ADR 0017), so the crates fall at the
    // same speed at any frame rate. Frames show the latest tick; smoothing between ticks
    // (interpolation) comes later.
    std::string clock_error;
    std::optional<lykoware::FixedStep> simulation = lykoware::FixedStep::create({}, clock_error);
    if (!simulation) {
        lykoware::log(lykoware::LogLevel::error, "Simulation clock: " + clock_error);
        return EXIT_FAILURE;
    }
    lykoware::FrameClock frame_clock;

    bool ok = true;
    std::uint64_t frame = 0;
    while (window.pump()) {
        const lykoware::FrameSteps steps = simulation->advance(frame_clock.lap());
        for (std::uint32_t tick = 0; tick < steps.ticks; ++tick) {
            scene->tick(static_cast<float>(simulation->tick_seconds()));
        }
        if (audio) {
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
        if (!renderer->draw(scene->entities(), camera, *swapchain, sky, error)) {
            lykoware::log(lykoware::LogLevel::error, "Vulkan frame: " + error.message);
            ok = false;
            break;
        }
        ++frame;
        if (options.has_frames && frame >= options.frames) {
            break;
        }
    }
    const std::uint64_t presented = swapchain->frames_presented();
    const std::size_t drawn = renderer->last_draw_count();
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
    if (instance->validation_error_count() != 0) {
        lykoware::log(lykoware::LogLevel::error,
                      std::to_string(instance->validation_error_count()) + " Vulkan validation errors.");
        return EXIT_FAILURE;
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

    ShaderWords shaders;
    if (options.vulkan && !read_shaders(shaders)) {
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
                         "                        [--model FILE] [--frames N]\n"
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
                         "  base colour materials and textures (PNG or JPEG).\n";
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
