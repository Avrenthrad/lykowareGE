#include <lykoware/clock.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/log.hpp>
#include <lykoware/math.hpp>
#include <lykoware/paths.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/trace.hpp>
#include <lykoware/version.hpp>
#include <lykoware/window.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
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
    bool has_frames = false;
    std::uint64_t frames = 0;
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

// The falling-crates scene (ADR 0026): a grey ground and a grid of coloured crates. Physics
// moves the crate entities and the renderer draws every entity with a Transform and a
// MeshRenderer.
class CrateScene {
public:
    static constexpr int grid = 5;
    // Seconds between drops: then every crate is removed and a new set falls.
    static constexpr double seconds_per_drop = 8.0;

    explicit CrateScene(lykoware::physics::World& physics)
        : link_(entities_, physics)
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
    void tick(float seconds)
    {
        link_.step(seconds);
        elapsed_ += seconds;
        if (elapsed_ >= seconds_per_drop) {
            elapsed_ = 0.0;
            for (const lykoware::ecs::Entity crate : crates_) {
                entities_.destroy(crate); // the link destroys each crate's physics body too
            }
            crates_.clear();
            drop();
        }
    }

    lykoware::ecs::World& entities() { return entities_; }

private:
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
                    crates_.push_back(entity);
                }
            }
        }
    }

    // The ECS world before the link; the physics world outlives both (ADR 0023).
    lykoware::ecs::World entities_;
    lykoware::physics_ecs::Link link_;
    std::vector<lykoware::ecs::Entity> crates_;
    double elapsed_ = 0.0;
};

// Draws the falling-crates scene through the RHI swapchain.
int present_loop(lykoware::Window& window, const WindowOptions& options, const ShaderWords& shaders)
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
    auto scene = std::make_unique<CrateScene>(*physics);
    const lykoware::render::Camera camera{{0.0, 7.0, 15.0}, {0.0, 1.5, 0.0}};
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

    ShaderWords shaders;
    if (options.vulkan && !read_shaders(shaders)) {
        return EXIT_FAILURE;
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
        const int status = present_loop(*window, options, shaders);
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
                         "       lykoware_sandbox --window [--dummy | --offscreen] [--vulkan] [--frames N]\n"
                         "Without options, runs the console foundation check.\n"
                         "--frames with --trace runs N CPU iterations and writes a Chrome trace.\n"
                         "--window opens a window and pumps input until it closes.\n"
                         "--dummy selects the headless video driver and requires --frames.\n"
                         "--offscreen is headless too, can host Vulkan, and requires --frames.\n"
                         "--vulkan draws a triangle over a cycling clear colour through the\n"
                         "  Vulkan RHI, with shaders from the shaders folder beside the executable;\n"
                         "  exit 3 when no usable Vulkan device exists, 1 when shaders are missing.\n";
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
