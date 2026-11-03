// rhi.device: physical-device selection and logical-device lifetime.
//
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver, or 1.3
// device). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int exit_skip = 77;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

bool at_least_required(const lykoware::rhi::ApiVersion& version)
{
    return version.major > lykoware::rhi::required_api_major
        || (version.major == lykoware::rhi::required_api_major
            && version.minor >= lykoware::rhi::required_api_minor);
}

void test_selected_device(const lykoware::rhi::Device& device)
{
    const lykoware::rhi::DeviceInfo& info = device.info();
    expect(info.suitable && info.unsuitable_reason.empty(), "selected device is suitable");
    expect(!info.name.empty(), "selected device has a name");
    expect(at_least_required(info.api_version), "selected device supports the required API version");
    expect(device.native_device() != nullptr, "VkDevice exists");
    expect(device.native_physical_device() != nullptr, "VkPhysicalDevice exists");
    expect(device.native_graphics_queue() != nullptr, "graphics queue exists");
    expect(device.graphics_queue_family() != UINT32_MAX, "graphics queue family is valid");
    expect(device.enabled_extensions().empty(), "no extensions requested, none enabled");
    lykoware::rhi::RhiError error;
    expect(device.wait_idle(error), "wait_idle succeeds on an idle device");
}

void test_rejections(const lykoware::rhi::Instance& instance, const std::string& known_name)
{
    lykoware::rhi::RhiError error;
    lykoware::rhi::DeviceConfig by_name;
    by_name.name_contains = "lykoware-no-such-gpu";
    auto none = lykoware::rhi::Device::create(instance, by_name, error);
    expect(none == nullptr && error.failure == lykoware::rhi::RhiFailure::NoSuitableDevice,
           "an unmatched name filter fails with NoSuitableDevice");
    expect(error.message.find(known_name) != std::string::npos, "the rejection lists the real devices");
    expect(error.message.find("name does not contain") != std::string::npos, "the rejection gives the reason");

    lykoware::rhi::DeviceConfig by_extension;
    by_extension.extensions = {"VK_LYKOWARE_not_a_real_extension"};
    auto missing = lykoware::rhi::Device::create(instance, by_extension, error);
    expect(missing == nullptr && error.failure == lykoware::rhi::RhiFailure::NoSuitableDevice,
           "a missing device extension fails with NoSuitableDevice");
    expect(error.message.find("VK_LYKOWARE_not_a_real_extension") != std::string::npos,
           "the rejection names the missing extension");
}

void test_software_preference(const lykoware::rhi::Instance& instance, const std::vector<lykoware::rhi::DeviceInfo>& devices)
{
    const bool has_cpu = std::any_of(devices.begin(), devices.end(), [](const lykoware::rhi::DeviceInfo& info) {
        return info.suitable && info.type == lykoware::rhi::DeviceType::Cpu;
    });
    if (!has_cpu) {
        std::cout << "note: no suitable CPU device; Software preference not exercised\n";
        return;
    }
    lykoware::rhi::DeviceConfig config;
    config.preference = lykoware::rhi::DevicePreference::Software;
    lykoware::rhi::RhiError error;
    auto device = lykoware::rhi::Device::create(instance, config, error);
    expect(device != nullptr, "Software preference creates a device");
    if (device) {
        expect(device->info().type == lykoware::rhi::DeviceType::Cpu, "Software preference picks the CPU device");
    }
}

}

int main()
{
    lykoware::rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.device test";
    instance_config.validation = lykoware::rhi::Validation::IfAvailable;
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(instance_config, error);
    if (!instance) {
        const bool unavailable = error.failure == lykoware::rhi::RhiFailure::NoLoader
            || error.failure == lykoware::rhi::RhiFailure::NoDriver
            || error.failure == lykoware::rhi::RhiFailure::VersionTooOld;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }

    const std::vector<lykoware::rhi::DeviceInfo> devices = lykoware::rhi::enumerate_devices(*instance, {}, error);
    expect(error.failure == lykoware::rhi::RhiFailure::None, "enumeration succeeds");
    for (const lykoware::rhi::DeviceInfo& info : devices) {
        std::cout << "device: " << info.name << " (" << lykoware::rhi::to_string(info.type) << ", Vulkan "
                  << info.api_version.major << '.' << info.api_version.minor << '.' << info.api_version.patch
                  << ") " << (info.suitable ? "suitable" : "unsuitable: " + info.unsuitable_reason) << '\n';
    }
    const bool any_suitable = std::any_of(devices.begin(), devices.end(),
                                          [](const lykoware::rhi::DeviceInfo& info) { return info.suitable; });
    if (!any_suitable) {
        std::cout << "SKIP: no Vulkan " << lykoware::rhi::required_api_major << '.'
                  << lykoware::rhi::required_api_minor << " device with the required features\n";
        return exit_skip;
    }

    auto device = lykoware::rhi::Device::create(*instance, {}, error);
    expect(device != nullptr, "default config creates a device");
    if (!device) {
        std::cerr << "device error: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    expect(error.failure == lykoware::rhi::RhiFailure::None && error.message.empty(), "success resets the error");
    test_selected_device(*device);
    const std::string selected_name = device->info().name;
    std::cout << "selected: " << selected_name << '\n';

    lykoware::rhi::RhiError second_error;
    auto second = lykoware::rhi::Device::create(*instance, {}, second_error);
    expect(second == nullptr && second_error.failure == lykoware::rhi::RhiFailure::AlreadyExists,
           "a second live device is refused");
    device.reset();

    test_rejections(*instance, selected_name);
    test_software_preference(*instance, devices);

    auto again = lykoware::rhi::Device::create(*instance, {}, error);
    expect(again != nullptr, "a device can be created again after the last one is destroyed");
    again.reset();

    if (instance->validation_enabled()) {
        expect(instance->validation_error_count() == 0, "device lifetime produces no validation errors");
    } else {
        std::cout << "note: validation layer not installed; validation not checked\n";
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " RHI device assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi device passed\n";
    return EXIT_SUCCESS;
}
