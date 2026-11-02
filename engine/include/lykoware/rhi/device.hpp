#pragma once

#include <lykoware/rhi/instance.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lykoware::rhi {

enum class DeviceType {
    Other,
    Integrated,
    Discrete,
    Virtual,
    // A CPU implementation such as Mesa lavapipe. Used by CI.
    Cpu,
};

// One physical device (GPU or software implementation) as the driver reports it.
struct DeviceInfo {
    std::string name;
    DeviceType type = DeviceType::Other;
    ApiVersion api_version{};
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t driver_version = 0;
    // Whether Device::create could use it with the requested extensions.
    bool suitable = false;
    // Why not, when suitable is false. Empty otherwise.
    std::string unsuitable_reason;
};

enum class DevicePreference {
    // Discrete, then integrated, then virtual, then other, then CPU.
    HighPerformance,
    // CPU implementations first (deterministic CI and tests), then as above.
    Software,
};

struct DeviceConfig {
    DevicePreference preference = DevicePreference::HighPerformance;
    // When not empty, only devices whose name contains this text qualify.
    std::string name_contains;
    // Device extensions to enable, for example "VK_KHR_swapchain".
    std::vector<std::string> extensions;
};

// Lists every physical device with its suitability for `config`.
// Requirements for "suitable": Vulkan 1.3 or later, a queue family with
// graphics and compute, the 1.3 dynamicRendering and synchronization2
// features, every extension in config.extensions, and config.name_contains.
//
// Ownership: returns owned copies; nothing refers back to the instance.
// Threading: same thread rules as Instance. Failure: returns an empty list
// and fills error if the driver refuses enumeration; an empty list with
// RhiFailure::None means the driver reports no devices.
[[nodiscard]] std::vector<DeviceInfo> enumerate_devices(
    const Instance& instance, const DeviceConfig& config, RhiError& error);

// One logical device with one graphics-and-compute queue.
//
// Ownership: create() returns the sole owner. The destructor waits for the
// device to go idle, then destroys it. Copy and move are not provided.
// Objects created from this device must be destroyed before it.
//
// Lifetime: the Device must be destroyed before the Instance it was created
// from. At most one Device exists per process at a time (volk keeps one
// device function table); a second create() fails with AlreadyExists.
//
// Threading: create, wait_idle and destroy on one thread. Accessors are const
// and safe from any thread while the device is alive. Submitting to the queue
// from several threads needs external synchronisation (later RHI work).
//
// Failure: create() returns nullptr and fills error; on success error is reset.
// RhiFailure::NoSuitableDevice lists every device and why it was rejected.
// It does not throw for driver refusals. std::bad_alloc can propagate.
class Device {
public:
    [[nodiscard]] static std::unique_ptr<Device> create(
        const Instance& instance, const DeviceConfig& config, RhiError& error);

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] const DeviceInfo& info() const;
    [[nodiscard]] std::uint32_t graphics_queue_family() const;
    [[nodiscard]] std::span<const std::string> enabled_extensions() const;

    // Blocks until all queued work on the device has finished.
    [[nodiscard]] bool wait_idle(RhiError& error) const;

    // Vulkan handles as opaque pointers for other RHI code. Valid until this
    // object is destroyed. VkDevice, VkPhysicalDevice and VkQueue.
    [[nodiscard]] void* native_device() const;
    [[nodiscard]] void* native_physical_device() const;
    [[nodiscard]] void* native_graphics_queue() const;

private:
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* to_string(DeviceType type) noexcept;

}
