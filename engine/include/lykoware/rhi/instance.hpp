#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lykoware::rhi {

// Lykoware render hardware interface (ADR 0002): Vulkan 1.3 behind engine
// types. Public RHI headers do not include Vulkan headers and do not expose
// Vulkan types. Handles that another engine module must pass to the platform
// layer are opaque pointers.
//
// The Vulkan loader is opened at run time. The engine has no link-time Vulkan
// dependency, so it builds and runs on a machine without a Vulkan driver;
// only the calls below fail, with RhiFailure::NoLoader or NoDriver.

// Lowest Vulkan API version the RHI accepts.
inline constexpr std::uint32_t required_api_major = 1;
inline constexpr std::uint32_t required_api_minor = 3;

struct ApiVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

// Why an RHI call failed. None means success.
enum class RhiFailure {
    None,
    // No Vulkan loader library could be opened (nothing Vulkan installed).
    NoLoader,
    // A loader is installed but it found no Vulkan driver (ICD), so it
    // cannot create an instance. Typical on GPU-less machines.
    NoDriver,
    // The loader reports an instance version below required_api_*.
    VersionTooOld,
    // A requested instance extension is not offered by the loader or layers.
    MissingExtension,
    // Validation::Required was requested and the Khronos validation layer is
    // not installed.
    MissingLayer,
    // Another rhi::Instance is alive in this process.
    AlreadyExists,
    // The driver refused a call. message holds the Vulkan result name.
    DriverError,
};

struct RhiError {
    RhiFailure failure = RhiFailure::None;
    std::string message;
};

enum class Validation {
    // No validation layer.
    Off,
    // Enable VK_LAYER_KHRONOS_validation if it is installed; otherwise run
    // without it. validation_enabled() reports the outcome.
    IfAvailable,
    // Fail with RhiFailure::MissingLayer if it is not installed.
    Required,
};

struct InstanceConfig {
    std::string application_name = "Lykoware";
    Validation validation = Validation::Off;
    // Extra instance extensions, for example the names returned by
    // Window::vulkan_instance_extensions(). Duplicates are ignored.
    std::vector<std::string> extensions;
};

// One Vulkan instance.
//
// Ownership: create() returns the sole owner. The destructor destroys the
// debug messenger (if any) and the VkInstance, then releases the loader.
// Copy and move are not provided. Every object later created from this
// instance (devices, surfaces) must be destroyed before it.
//
// Lifetime: at most one Instance exists per process at a time, because the
// loader's function table is process-wide. A second create() while one is
// alive fails with RhiFailure::AlreadyExists. After destruction a new one can
// be created.
//
// Threading: create and destroy on one thread. The accessors are const and
// may be called from any thread while the instance is alive. Validation
// messages can arrive on any thread that makes a Vulkan call; the counters are
// atomic and messages go to lykoware::log.
//
// Failure: create() returns nullptr and fills error; on success error is
// reset to RhiFailure::None. It does not throw for a missing loader, driver
// refusal, or bad configuration. std::bad_alloc can propagate.
class Instance {
public:
    [[nodiscard]] static std::unique_ptr<Instance> create(const InstanceConfig& config, RhiError& error);

    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    // Instance-level API version reported by the loader. At least 1.3.
    // Device support is checked separately when a device is created.
    [[nodiscard]] ApiVersion api_version() const;

    [[nodiscard]] bool validation_enabled() const;

    // Every extension enabled on this instance, including the debug-utils
    // extension added for validation.
    [[nodiscard]] std::span<const std::string> enabled_extensions() const;

    // Validation messages reported since creation. Zero when validation is off.
    [[nodiscard]] std::uint64_t validation_warning_count() const;
    [[nodiscard]] std::uint64_t validation_error_count() const;

    // VkInstance as an opaque pointer, for Window::create_vulkan_surface.
    // Valid until this object is destroyed.
    [[nodiscard]] void* native_handle() const;

private:
    struct Impl;
    explicit Instance(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
