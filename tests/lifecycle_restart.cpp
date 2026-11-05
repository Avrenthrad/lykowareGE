// lifecycle.restart and lifecycle.restart_vulkan (ADR 0040): the engine's systems shut down
// completely and start again in the same process, three times over.
//
// core: a headless window, audio on SDL's dummy driver, a physics world and an ECS world each
// cycle. The window closes through a platform close request; the window and audio shut down in
// alternating order. After each cycle no part of SDL is left running, and physics gives
// bit-identical results every cycle.
//
// vulkan: the same plus the whole Vulkan stack on an offscreen window: instance, device,
// swapchain with depth and the lit renderer drawing a crate for five frames. Each cycle presents
// every frame, times each on the GPU where supported, and leaves no validation error.
//
// Usage: lykoware_lifecycle_test core
//        lykoware_lifecycle_test vulkan LIT_VERTEX.spv LIT_FRAGMENT.spv
// Exit codes: 0 pass, 1 fail, 77 skip (vulkan only: no Vulkan, no headless surface, or no
// device that can present). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/audio.hpp>
#include <lykoware/audio_output.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/transform.hpp>
#include <lykoware/window.hpp>

#include <SDL3/SDL.h>

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

constexpr int exit_skip = 77;
constexpr int cycles = 3;
constexpr int frames = 5;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

int finish()
{
    if (g_failures != 0) {
        std::cerr << g_failures << " lifecycle assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "lifecycle restart passed\n";
    return EXIT_SUCCESS;
}

lykoware::WindowConfig window_config(lykoware::VideoDriver driver, bool vulkan)
{
    lykoware::WindowConfig config;
    config.title = "lykoware lifecycle test";
    config.width = 160;
    config.height = 120;
    config.visible = false;
    config.vulkan = vulkan;
    config.driver = driver;
    return config;
}

// Audio on the dummy driver, already playing a short looping tone.
std::unique_ptr<lykoware::audio::Output> open_audio(const std::string& at)
{
    lykoware::audio::OutputConfig config;
    config.driver = lykoware::audio::OutputDriver::Dummy;
    config.real_time = false;
    std::string error;
    auto output = lykoware::audio::Output::open(config, error);
    expect(output != nullptr, at + "audio opens: " + error);
    if (!output) {
        return nullptr;
    }
    std::vector<float> samples(4800);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = 0.25F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(index) / 48000.0));
    }
    const auto sound = lykoware::audio::Sound::create(samples, 1, 48000, error);
    expect(sound != nullptr, at + "a tone is made");
    lykoware::audio::PlayParams params;
    params.loop = true;
    const auto voice = output->lock()->play(sound, params, error);
    expect(voice.has_value(), at + "the tone plays");
    expect(output->resume(), at + "audio resumes");
    return output;
}

// A box dropped onto the ground for one second; the final state's hash, or nullopt.
std::optional<std::uint64_t> simulate(const std::string& at)
{
    namespace physics = lykoware::physics;
    std::string error;
    auto world = physics::World::create({}, error);
    expect(world != nullptr, at + "the physics world is created: " + error);
    if (!world) {
        return std::nullopt;
    }
    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{10.F, 0.5F, 10.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = {0.0, -0.5, 0.0};
    physics::BodyDesc box;
    box.position = {0.0, 3.0, 0.0};
    box.angular_velocity = {0.5F, 1.F, 0.F};
    expect(world->create_body(ground, error).has_value() && world->create_body(box, error).has_value(),
           at + "the bodies are created");
    for (int step = 0; step < 60; ++step) {
        expect(world->step(1.F / 60.F), at + "physics steps");
    }
    return world->state_hash();
}

// Closes the window as the platform would, with a close request for it.
void close_from_platform(lykoware::Window& window, const std::string& at)
{
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    expect(windows != nullptr && count == 1, at + "one platform window is open");
    if (windows != nullptr && count == 1) {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = SDL_GetWindowID(windows[0]);
        expect(SDL_PushEvent(&event), at + "the close request is pushed");
    }
    SDL_free(windows);
    expect(!window.pump() && window.close_requested(), at + "the platform close request closes the window");
}

// Shuts the window and audio down, in alternating order, and checks nothing of SDL is left.
void shut_down(std::unique_ptr<lykoware::Window>& window, std::unique_ptr<lykoware::audio::Output>& audio,
               int cycle, const std::string& at)
{
    // Each session is independent (ADR 0033): closing one leaves the other's SDL running.
    if (cycle % 2 == 0) {
        window.reset();
        expect(audio == nullptr || SDL_WasInit(SDL_INIT_AUDIO) != 0, at + "closing the window leaves audio running");
        audio.reset();
    } else {
        audio.reset();
        expect(window == nullptr || SDL_WasInit(SDL_INIT_VIDEO) != 0, at + "closing audio leaves the window's SDL running");
        window.reset();
    }
    expect(SDL_WasInit(0) == 0, at + "nothing of SDL is left running");
}

int run_core()
{
    std::optional<std::uint64_t> first_hash;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const std::string at = "core cycle " + std::to_string(cycle + 1) + ": ";
        std::string error;
        auto window = lykoware::Window::open(window_config(lykoware::VideoDriver::Dummy, false), error);
        expect(window != nullptr, at + "the window opens: " + error);
        auto audio = open_audio(at);
        const std::optional<std::uint64_t> hash = simulate(at);
        if (cycle == 0) {
            first_hash = hash;
        }
        expect(hash.has_value() && hash == first_hash, at + "physics gives the same result after a restart");
        lykoware::ecs::World entities;
        const lykoware::ecs::Entity entity = entities.create();
        entities.add<lykoware::Transform>(entity, lykoware::Transform{});
        expect(entities.alive(entity), at + "an entity is created");
        if (window) {
            for (int frame = 0; frame < frames; ++frame) {
                expect(window->pump(), at + "the window pumps");
            }
            close_from_platform(*window, at);
        }
        shut_down(window, audio, cycle, at);
    }
    return finish();
}

bool unavailable(lykoware::rhi::RhiFailure failure)
{
    using lykoware::rhi::RhiFailure;
    return failure == RhiFailure::NoLoader || failure == RhiFailure::NoDriver || failure == RhiFailure::VersionTooOld
        || failure == RhiFailure::MissingExtension || failure == RhiFailure::NoSuitableDevice;
}

// One full start, five frames and shutdown of the Vulkan stack. Returns false to skip the test.
bool vulkan_cycle(int cycle, const std::vector<std::uint32_t>& vertex, const std::vector<std::uint32_t>& fragment)
{
    namespace rhi = lykoware::rhi;
    const std::string at = "vulkan cycle " + std::to_string(cycle + 1) + ": ";
    std::string window_error;
    auto window = lykoware::Window::open(window_config(lykoware::VideoDriver::Offscreen, true), window_error);
    if (!window) {
        std::cout << "SKIP: offscreen Vulkan window unavailable: " << window_error << '\n';
        return false;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware lifecycle test";
    instance_config.validation = rhi::Validation::IfAvailable;
    for (const char* name : window->vulkan_instance_extensions()) {
        instance_config.extensions.emplace_back(name);
    }
    rhi::RhiError error;
    auto instance = rhi::Instance::create(instance_config, error);
    if (!instance && unavailable(error.failure)) {
        std::cout << "SKIP: " << error.message << '\n';
        return false;
    }
    expect(instance != nullptr, at + "the instance is created: " + error.message);
    if (!instance) {
        return true;
    }
    rhi::DeviceConfig device_config;
    device_config.extensions = {rhi::swapchain_device_extension};
    auto device = rhi::Device::create(*instance, device_config, error);
    if (!device && unavailable(error.failure)) {
        std::cout << "SKIP: " << error.message << '\n';
        return false;
    }
    expect(device != nullptr, at + "the device is created: " + error.message);
    if (!device) {
        return true;
    }
    rhi::SwapchainConfig swapchain_config;
    swapchain_config.depth = true;
    auto swapchain = rhi::Swapchain::create(*instance, *device, *window, swapchain_config, error);
    if (!swapchain && unavailable(error.failure)) {
        std::cout << "SKIP: " << error.message << '\n';
        return false;
    }
    expect(swapchain != nullptr, at + "the swapchain is created: " + error.message);
    if (!swapchain) {
        return true;
    }
    auto renderer = lykoware::render::Renderer::create(*device, swapchain->color_format(), vertex, fragment, error);
    expect(renderer != nullptr, at + "the renderer is created: " + error.message);
    auto audio = open_audio(at);

    lykoware::ecs::World entities;
    const lykoware::ecs::Entity crate = entities.create();
    entities.add<lykoware::Transform>(crate, lykoware::Transform{});
    entities.add<lykoware::render::MeshRenderer>(
        crate, lykoware::render::MeshRenderer{lykoware::render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.8F, 0.4F, 0.2F}});
    const lykoware::render::Camera camera{{0.0, 1.0, 4.0}, {0.0, 0.0, 0.0}};
    if (renderer) {
        bool drawn = true;
        for (int frame = 0; frame < frames && drawn; ++frame) {
            expect(window->pump(), at + "the window pumps");
            drawn = renderer->draw(entities, camera, *swapchain, {0.2F, 0.3F, 0.4F, 1.F}, error);
            expect(drawn, at + "a frame draws: " + error.message);
        }
        expect(swapchain->frames_presented() == frames, at + "every frame is presented");
        expect(device->wait_idle(error), at + "the device goes idle");
        const std::size_t timed = swapchain->take_gpu_frame_times().size();
        expect(timed == (device->gpu_timestamps() ? static_cast<std::size_t>(frames) : 0U),
               at + "every frame has a GPU time where the device has timestamps");
    }
    close_from_platform(*window, at);

    // Shutdown in the documented order: the swapchain waits for the GPU, then what it used goes.
    swapchain.reset();
    renderer.reset();
    device.reset();
    if (instance->validation_enabled()) {
        expect(instance->validation_error_count() == 0, at + "no validation errors");
    } else if (cycle == 0) {
        std::cout << "note: validation layer not installed; synchronisation not validated\n";
    }
    instance.reset();
    shut_down(window, audio, cycle, at);
    return true;
}

int run_vulkan(const char* vertex_path, const char* fragment_path)
{
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
    lykoware::rhi::RhiError error;
    if (!lykoware::rhi::read_spirv_file(vertex_path, vertex, error)
        || !lykoware::rhi::read_spirv_file(fragment_path, fragment, error)) {
        std::cerr << "FAIL: the lit shaders load: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    for (int cycle = 0; cycle < cycles; ++cycle) {
        if (!vulkan_cycle(cycle, vertex, fragment)) {
            if (cycle == 0) {
                return exit_skip;
            }
            std::cerr << "FAIL: Vulkan became unavailable after a restart\n";
            return EXIT_FAILURE;
        }
    }
    return finish();
}

}

int main(int argc, char* argv[])
{
    const std::string_view mode = argc >= 2 ? std::string_view{argv[1]} : std::string_view{};
    if (mode == "core" && argc == 2) {
        return run_core();
    }
    if (mode == "vulkan" && argc == 4) {
        return run_vulkan(argv[2], argv[3]);
    }
    std::cerr << "usage: lykoware_lifecycle_test core\n"
                 "       lykoware_lifecycle_test vulkan LIT_VERTEX.spv LIT_FRAGMENT.spv\n";
    return EXIT_FAILURE;
}
