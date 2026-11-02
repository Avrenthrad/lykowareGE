// rhi.instance: creates and destroys a Vulkan instance through the RHI.
//
// Exit codes: 0 pass, 1 fail, 77 skip. CTest maps 77 to "Skipped" (not
// "Passed") so a runner without a Vulkan loader never reports this as green.

#include <lykoware/rhi/instance.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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

bool skip_reason(const lykoware::rhi::RhiError& error)
{
    return error.failure == lykoware::rhi::RhiFailure::NoLoader
        || error.failure == lykoware::rhi::RhiFailure::NoDriver
        || error.failure == lykoware::rhi::RhiFailure::VersionTooOld;
}

// Run by CTest with the loader's driver list pointed at a missing file, so a
// loader is present but finds no driver. Creation must fail as NoDriver, not
// as a generic DriverError. Without any loader the case cannot be built: skip.
int expect_no_driver()
{
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create({}, error);
    if (error.failure == lykoware::rhi::RhiFailure::NoLoader) {
        std::cout << "SKIP: " << error.message << '\n';
        return exit_skip;
    }
    expect(instance == nullptr, "no instance without a driver");
    expect(error.failure == lykoware::rhi::RhiFailure::NoDriver, "missing driver is reported as NoDriver");
    if (g_failures != 0) {
        std::cerr << "got: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "no-driver classification passed: " << error.message << '\n';
    return EXIT_SUCCESS;
}

void test_basic(const lykoware::rhi::Instance& instance)
{
    const auto version = instance.api_version();
    expect(version.major > lykoware::rhi::required_api_major
               || (version.major == lykoware::rhi::required_api_major
                   && version.minor >= lykoware::rhi::required_api_minor),
           "loader reports at least the required API version");
    expect(instance.native_handle() != nullptr, "native handle is a live VkInstance");
    expect(!instance.validation_enabled(), "validation is off by default");
    expect(instance.enabled_extensions().empty(), "default instance enables no extensions");
    expect(instance.validation_warning_count() == 0 && instance.validation_error_count() == 0,
           "no validation messages without validation");
}

void test_single_instance()
{
    lykoware::rhi::RhiError error;
    auto second = lykoware::rhi::Instance::create({}, error);
    expect(second == nullptr, "a second live instance is refused");
    expect(error.failure == lykoware::rhi::RhiFailure::AlreadyExists, "refusal names AlreadyExists");
}

void test_missing_extension()
{
    lykoware::rhi::InstanceConfig config;
    config.extensions = {"VK_LYKOWARE_not_a_real_extension"};
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(config, error);
    expect(instance == nullptr, "an unknown extension is refused");
    expect(error.failure == lykoware::rhi::RhiFailure::MissingExtension, "refusal names MissingExtension");
    expect(error.message.find("VK_LYKOWARE_not_a_real_extension") != std::string::npos,
           "refusal names the extension");
}

void test_validation()
{
    lykoware::rhi::InstanceConfig config;
    config.validation = lykoware::rhi::Validation::IfAvailable;
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(config, error);
    expect(instance != nullptr, "IfAvailable never fails for a missing layer");
    if (!instance) {
        std::cerr << "validation create error: " << error.message << '\n';
        return;
    }
    if (!instance->validation_enabled()) {
        std::cout << "note: VK_LAYER_KHRONOS_validation not installed; validation path not exercised\n";
        lykoware::rhi::RhiError required_error;
        config.validation = lykoware::rhi::Validation::Required;
        instance.reset();
        auto required = lykoware::rhi::Instance::create(config, required_error);
        expect(required == nullptr && required_error.failure == lykoware::rhi::RhiFailure::MissingLayer,
               "Required validation fails with MissingLayer when the layer is absent");
        return;
    }
    bool has_debug_utils = false;
    for (const std::string& name : instance->enabled_extensions()) {
        has_debug_utils = has_debug_utils || name == "VK_EXT_debug_utils";
    }
    expect(has_debug_utils, "validation enables VK_EXT_debug_utils");
    expect(instance->validation_error_count() == 0, "creating a valid instance reports no validation errors");
}

}

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string_view{argv[1]} == "--expect-no-driver") {
        return expect_no_driver();
    }
    if (argc != 1) {
        std::cerr << "usage: lykoware_rhi_instance_test [--expect-no-driver]\n";
        return EXIT_FAILURE;
    }
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create({}, error);
    if (!instance) {
        if (skip_reason(error)) {
            std::cout << "SKIP: " << error.message << '\n';
            return exit_skip;
        }
        std::cerr << "FAIL: instance creation: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    expect(error.failure == lykoware::rhi::RhiFailure::None && error.message.empty(),
           "success resets the error");
    test_basic(*instance);
    test_single_instance();
    instance.reset();

    test_missing_extension();
    test_validation();

    auto again = lykoware::rhi::Instance::create({}, error);
    expect(again != nullptr, "an instance can be created again after the last one is destroyed");

    if (g_failures != 0) {
        std::cerr << g_failures << " RHI instance assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    const auto version = again->api_version();
    std::cout << "rhi instance passed (loader Vulkan " << version.major << '.' << version.minor << '.'
              << version.patch << ")\n";
    return EXIT_SUCCESS;
}
