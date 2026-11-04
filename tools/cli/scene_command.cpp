#include "scene_command.hpp"

#include <lykoware/image.hpp>
#include <lykoware/paths.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/scene.hpp>
#include <lykoware/version.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace lykoware::cli {
namespace {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
std::filesystem::path path(std::string_view text)
{
    return std::filesystem::path{std::u8string(text.begin(), text.end())};
}
std::string read(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 16*1024*1024) {
        throw std::runtime_error("cannot read input or input exceeds 16 MiB");
    }
    input.seekg(0);
    return {std::istreambuf_iterator<char>(input), {}};
}
std::uint32_t integer(std::string_view value, std::uint32_t maximum)
{
    std::uint32_t result = 0;
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data()+value.size() || result == 0 || result > maximum) {
        throw std::invalid_argument("invalid positive integer option");
    }
    return result;
}
double ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now()-start).count();
}
Json summary(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const auto percentile = [&](std::size_t percent) {
        return values.empty() ? 0.0 : values[((values.size()-1)*percent)/100];
    };
    return {{"p50_ms", percentile(50)}, {"p95_ms", percentile(95)}, {"p99_ms", percentile(99)},
            {"max_ms", values.empty() ? 0.0 : values.back()}, {"samples", values.size()}};
}
std::uint64_t peak_memory_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
    }
#endif
    return 0;
}
}

int scene_command(std::span<const std::string> arguments, bool screenshot)
{
    std::filesystem::path source, replay, capture, report;
    std::uint32_t ticks = 120, frames = 0, warmup = 30, width = 1280, height = 720;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (!seen.insert(option).second || i+1 >= arguments.size()) {
            throw std::invalid_argument("duplicate option or missing scene option value");
        }
        const auto& value = arguments[++i];
        if (option == "--scene") { source = path(value); }
        else if (option == "--commands") { replay = path(value); }
        else if (option == "--capture" || (screenshot && option == "--output")) { capture = path(value); }
        else if (option == "--report") { report = path(value); }
        else if (option == "--ticks") { ticks = integer(value, 36000); }
        else if (option == "--measure-frames") { frames = integer(value, 10000); }
        else if (option == "--warmup") { warmup = integer(value, 1000); }
        else if (option == "--width") { width = integer(value, 8192); }
        else if (option == "--height") { height = integer(value, 8192); }
        else { throw std::invalid_argument("unknown scene option: " + option); }
    }
    if (source.empty() || (screenshot && capture.empty())) {
        throw std::invalid_argument("requires --scene FILE; screenshot also requires --output FILE");
    }
    if (static_cast<std::uint64_t>(width)*height > 16777216U) {
        throw std::invalid_argument("capture exceeds 16 megapixel limit");
    }
    std::string error;
    scene::Description description;
    if (!scene::decode(read(source), description, error)) {
        throw std::runtime_error("scene: " + error);
    }
    std::vector<scene::Command> commands;
    if (!replay.empty() && !scene::decode_commands(read(replay), commands, error)) {
        throw std::runtime_error("commands: " + error);
    }
    if (!commands.empty() && commands.back().tick >= ticks) {
        throw std::invalid_argument("command stream extends beyond --ticks");
    }
    auto world = scene::Scene::create(description, error);
    if (!world) { throw std::runtime_error(error); }
    std::size_t next = 0;
    const auto step = [&] {
        while (next < commands.size() && commands[next].tick == world->tick()) {
            if (!world->dispatch(commands[next++], error)) {
                throw std::runtime_error("command failed: " + error);
            }
        }
        if (!world->advance_tick(error)) { throw std::runtime_error(error); }
    };
    Json result{{"format", "lykoware.measurement"}, {"schema_version", 1}, {"engine_version", lykoware::version},
                {"cpu_threads", std::thread::hardware_concurrency()}, {"scene_ticks", ticks},
                {"public_release_validation", false}, {"gpu_timestamp_ms", nullptr},
                {"gpu_resident_bytes", nullptr}, {"measurement", "offscreen synchronous draw; excludes presentation"}};
    std::vector<double> simulation;
    for (std::uint32_t i = 0; i < ticks; ++i) {
        const auto start = Clock::now();
        step();
        simulation.push_back(ms(start));
    }
    result["simulation"] = summary(simulation);
    result["replay_state_hash"] = world->state_hash();
    result["commands_applied"] = next;
    result["scene_entities"] = world->entities().size();
    if (!capture.empty() || frames > 0) {
        rhi::RhiError failure;
        auto instance = rhi::Instance::create({}, failure);
        auto device = instance ? rhi::Device::create(*instance, {}, failure) : nullptr;
        if (!device) {
            if (failure.failure == rhi::RhiFailure::NoLoader || failure.failure == rhi::RhiFailure::NoDriver
                || failure.failure == rhi::RhiFailure::VersionTooOld
                || failure.failure == rhi::RhiFailure::NoSuitableDevice) {
                std::cerr << "lyko: GPU capture unavailable: " << failure.message << '\n';
                return 77;
            }
            throw std::runtime_error("GPU initialization failed: " + failure.message);
        }
        const auto directory = shader_directory();
        std::vector<std::uint32_t> vertex, fragment;
        if (!rhi::read_spirv_file(directory / "cli_lit.vert.spv", vertex, failure)
            || !rhi::read_spirv_file(directory / "cli_lit.frag.spv", fragment, failure)) {
            throw std::runtime_error(failure.message);
        }
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, failure);
        auto target = rhi::RenderTarget::create(*device, {width, height, true}, failure);
        if (!renderer || !target) { throw std::runtime_error(failure.message); }
        result["device"] = {{"name", device->info().name}, {"type", rhi::to_string(device->info().type)},
                            {"vendor_id", device->info().vendor_id}, {"device_id", device->info().device_id},
                            {"driver_version", device->info().driver_version}};
        result["hardware_gpu"] = device->info().type != rhi::DeviceType::Cpu;
        result["width"] = width;
        result["height"] = height;
        const auto draw = [&] {
            auto display = world->presentation(1.0);
            if (!renderer->draw(display, world->camera(), *target, {0.08F, 0.1F, 0.14F, 1.F}, failure)) {
                throw std::runtime_error(failure.message);
            }
        };
        // Warm up pipeline/driver; timings must not include pipeline creation.
        for (std::uint32_t i = 0; i < warmup; ++i) { draw(); }
        std::vector<double> frame_times;
        for (std::uint32_t i = 0; i < frames; ++i) {
            const auto start = Clock::now();
            step();
            draw();
            frame_times.push_back(ms(start));
        }
        result["simulation_and_draw"] = summary(frame_times);
        result["frame_times_ms"] = frame_times;
        result["warmup_draws"] = warmup;
        if (!capture.empty()) {
            Image image;
            if (!rhi::RenderTargetCapture{*target}.capture(image) || !write_ppm(image, capture)) {
                throw std::runtime_error("screenshot capture/write failed");
            }
        }
    }
    result["final_tick"] = world->tick();
    result["final_state_hash"] = world->state_hash();
    result["peak_process_resident_bytes"] = peak_memory_bytes();
    if (!report.empty()) {
        std::ofstream output(report, std::ios::binary);
        output << result.dump(2) << '\n';
        output.close();
        if (!output) { throw std::runtime_error("measurement report write failed"); }
    }
    std::cout << "scene ticks=" << world->tick() << " entities=" << world->entities().size()
              << " state_hash=" << world->state_hash() << '\n';
    return 0;
}
}
