#include <lykoware/rhi/device.hpp>

#include "common.hpp"
#include "device_access.hpp"

#include <volk.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lykoware::rhi {
namespace detail {

namespace {
// volk keeps one process-wide device function table, so only one Device may own it.
std::atomic<bool> g_device_alive{false};
}

bool device_alive() noexcept
{
    return g_device_alive.load();
}

}

namespace {

using detail::driver_failure;
using detail::fail;

constexpr std::uint32_t no_queue_family = UINT32_MAX;

struct Candidate {
    VkPhysicalDevice handle = VK_NULL_HANDLE;
    DeviceInfo info;
    std::uint32_t queue_family = no_queue_family;
};

DeviceType device_type(VkPhysicalDeviceType type) noexcept
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return DeviceType::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return DeviceType::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return DeviceType::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return DeviceType::Cpu;
    default: return DeviceType::Other;
    }
}

int performance_rank(DeviceType type) noexcept
{
    switch (type) {
    case DeviceType::Discrete: return 4;
    case DeviceType::Integrated: return 3;
    case DeviceType::Virtual: return 2;
    case DeviceType::Other: return 1;
    case DeviceType::Cpu: return 0;
    }
    return 0;
}

int rank(DeviceType type, DevicePreference preference) noexcept
{
    if (preference == DevicePreference::Software && type == DeviceType::Cpu) {
        return 10;
    }
    return performance_rank(type);
}

std::uint32_t find_queue_family(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    constexpr VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (families[index].queueCount > 0 && (families[index].queueFlags & wanted) == wanted) {
            return index;
        }
    }
    return no_queue_family;
}

std::vector<std::string> device_extensions(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> properties(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data()) != VK_SUCCESS) {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        names.emplace_back(properties[index].extensionName);
    }
    return names;
}

// Fills info.suitable / info.unsuitable_reason. The first failed requirement wins.
void judge(Candidate& candidate, const DeviceConfig& config)
{
    DeviceInfo& info = candidate.info;
    auto reject = [&info](std::string reason) {
        info.suitable = false;
        info.unsuitable_reason = std::move(reason);
    };

    if (!config.name_contains.empty() && info.name.find(config.name_contains) == std::string::npos) {
        reject("name does not contain \"" + config.name_contains + "\"");
        return;
    }
    if (info.api_version.major < required_api_major
        || (info.api_version.major == required_api_major && info.api_version.minor < required_api_minor)) {
        reject("supports Vulkan " + std::to_string(info.api_version.major) + "."
               + std::to_string(info.api_version.minor) + "; " + std::to_string(required_api_major) + "."
               + std::to_string(required_api_minor) + " is required");
        return;
    }
    if (candidate.queue_family == no_queue_family) {
        reject("no queue family supports both graphics and compute");
        return;
    }

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features11;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(candidate.handle, &features);
    if (features13.dynamicRendering != VK_TRUE) {
        reject("dynamicRendering is not supported");
        return;
    }
    if (features13.synchronization2 != VK_TRUE) {
        reject("synchronization2 is not supported");
        return;
    }
    if (features11.shaderDrawParameters != VK_TRUE) {
        // Slang implements SV_VertexID with the DrawParameters capability.
        reject("shaderDrawParameters is not supported");
        return;
    }

    const std::vector<std::string> available = device_extensions(candidate.handle);
    for (const std::string& name : config.extensions) {
        if (std::find(available.begin(), available.end(), name) == available.end()) {
            reject("extension " + name + " is not available");
            return;
        }
    }
    info.suitable = true;
    info.unsuitable_reason.clear();
}

bool inspect(VkInstance instance, const DeviceConfig& config, std::vector<Candidate>& out, RhiError& error)
{
    out.clear();
    std::uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkEnumeratePhysicalDevices", result));
    }
    std::vector<VkPhysicalDevice> handles(count);
    result = vkEnumeratePhysicalDevices(instance, &count, handles.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkEnumeratePhysicalDevices", result));
    }
    handles.resize(count);

    for (VkPhysicalDevice handle : handles) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(handle, &properties);
        Candidate candidate;
        candidate.handle = handle;
        candidate.info.name = properties.deviceName;
        candidate.info.type = device_type(properties.deviceType);
        candidate.info.api_version = {VK_API_VERSION_MAJOR(properties.apiVersion),
                                      VK_API_VERSION_MINOR(properties.apiVersion),
                                      VK_API_VERSION_PATCH(properties.apiVersion)};
        candidate.info.vendor_id = properties.vendorID;
        candidate.info.device_id = properties.deviceID;
        candidate.info.driver_version = properties.driverVersion;
        candidate.queue_family = find_queue_family(handle);
        judge(candidate, config);
        out.push_back(std::move(candidate));
    }
    return true;
}

std::string rejection_list(const std::vector<Candidate>& candidates)
{
    if (candidates.empty()) {
        return "no suitable Vulkan device: the driver reports no physical devices";
    }
    std::string message = "no suitable Vulkan device:";
    for (const Candidate& candidate : candidates) {
        message += " [";
        message += candidate.info.name;
        message += ": ";
        message += candidate.info.unsuitable_reason;
        message += ']';
    }
    return message;
}

}

std::vector<DeviceInfo> enumerate_devices(const Instance& instance, const DeviceConfig& config, RhiError& error)
{
    error = {};
    std::vector<Candidate> candidates;
    if (!inspect(static_cast<VkInstance>(instance.native_handle()), config, candidates, error)) {
        return {};
    }
    std::vector<DeviceInfo> infos;
    infos.reserve(candidates.size());
    for (Candidate& candidate : candidates) {
        infos.push_back(std::move(candidate.info));
    }
    return infos;
}

struct Device::Impl {
    ~Impl()
    {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
            if (command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, command_pool, nullptr);
            }
            if (skin_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, skin_layout, nullptr);
            }
            if (shadow_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, shadow_layout, nullptr);
            }
            if (texture_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, texture_layout, nullptr);
            }
            if (timestamps != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, timestamps, nullptr);
            }
            if (allocator != VK_NULL_HANDLE) {
                vmaDestroyAllocator(allocator);
            }
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (owns_slot) {
            detail::g_device_alive.store(false);
            owns_slot = false;
        }
    }

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skin_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadow_layout = VK_NULL_HANDLE;
    VkDeviceSize uniform_alignment = 1;
    std::uint32_t max_image_dimension = 0;
    std::uint32_t max_push_constant_bytes = 0;
    // GPU timestamps (ADR 0039): the graphics queue's valid bits (0 means none), nanoseconds
    // per tick, and two queries for timing one-shot submissions.
    std::uint32_t timestamp_bits = 0;
    float timestamp_period = 0.F;
    VkQueryPool timestamps = VK_NULL_HANDLE;
    std::uint32_t queue_family = no_queue_family;
    bool owns_slot = false;
    DeviceInfo info;
    std::vector<std::string> extensions;
};

Device::Device(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Device::~Device() = default;

std::unique_ptr<Device> Device::create(const Instance& instance, const DeviceConfig& config, RhiError& error)
{
    error = {};
    auto impl = std::make_unique<Impl>();

    bool expected = false;
    if (!detail::g_device_alive.compare_exchange_strong(expected, true)) {
        fail(error, RhiFailure::AlreadyExists, "an rhi::Device already exists in this process");
        return nullptr;
    }
    impl->owns_slot = true;

    std::vector<Candidate> candidates;
    if (!inspect(static_cast<VkInstance>(instance.native_handle()), config, candidates, error)) {
        return nullptr;
    }
    const Candidate* best = nullptr;
    for (const Candidate& candidate : candidates) {
        if (!candidate.info.suitable) {
            continue;
        }
        // Strictly greater keeps the driver's order among equal ranks.
        if (best == nullptr || rank(candidate.info.type, config.preference) > rank(best->info.type, config.preference)) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        fail(error, RhiFailure::NoSuitableDevice, rejection_list(candidates));
        return nullptr;
    }

    impl->physical = best->handle;
    impl->queue_family = best->queue_family;
    impl->info = best->info;
    for (const std::string& name : config.extensions) {
        if (std::find(impl->extensions.begin(), impl->extensions.end(), name) == impl->extensions.end()) {
            impl->extensions.push_back(name);
        }
    }
    std::vector<const char*> extension_names;
    extension_names.reserve(impl->extensions.size());
    for (const std::string& name : impl->extensions) {
        extension_names.push_back(name.c_str());
    }

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = impl->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE; // Slang's SV_VertexID (ADR 0015)
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features11;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = &features;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extension_names.size());
    create_info.ppEnabledExtensionNames = extension_names.data();

    const VkResult created = vkCreateDevice(impl->physical, &create_info, nullptr, &impl->device);
    if (created != VK_SUCCESS) {
        impl->device = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateDevice", created));
        return nullptr;
    }
    volkLoadDevice(impl->device);
    vkGetDeviceQueue(impl->device, impl->queue_family, 0, &impl->queue);

    // GPU memory allocator. Vulkan functions come from volk (no static linking).
    VmaVulkanFunctions vma_functions{};
    vma_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vma_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_MAKE_API_VERSION(0, required_api_major, required_api_minor, 0);
    allocator_info.physicalDevice = impl->physical;
    allocator_info.device = impl->device;
    allocator_info.instance = static_cast<VkInstance>(instance.native_handle());
    allocator_info.pVulkanFunctions = &vma_functions;
    VkResult result = vmaCreateAllocator(&allocator_info, &impl->allocator);
    if (result != VK_SUCCESS) {
        impl->allocator = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateAllocator", result));
        return nullptr;
    }

    // One-shot command buffer and fence for submit_and_wait.
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = impl->queue_family;
    result = vkCreateCommandPool(impl->device, &pool_info, nullptr, &impl->command_pool);
    if (result != VK_SUCCESS) {
        impl->command_pool = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateCommandPool", result));
        return nullptr;
    }
    VkCommandBufferAllocateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    buffer_info.commandPool = impl->command_pool;
    buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buffer_info.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(impl->device, &buffer_info, &impl->command_buffer);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkAllocateCommandBuffers", result));
        return nullptr;
    }
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(impl->device, &fence_info, nullptr, &impl->fence);
    if (result != VK_SUCCESS) {
        impl->fence = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateFence", result));
        return nullptr;
    }

    // Every texture's descriptor set has this layout, so any texture works with any pipeline
    // that reads one (ADR 0037).
    VkDescriptorSetLayoutBinding texture_binding{};
    texture_binding.binding = 0;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_binding.descriptorCount = 1;
    texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &texture_binding;
    result = vkCreateDescriptorSetLayout(impl->device, &layout_info, nullptr, &impl->texture_layout);
    if (result != VK_SUCCESS) {
        impl->texture_layout = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateDescriptorSetLayout", result));
        return nullptr;
    }
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    texture_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    result = vkCreateDescriptorSetLayout(impl->device, &layout_info, nullptr, &impl->skin_layout);
    if (result != VK_SUCCESS) {
        impl->skin_layout = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("skin descriptor layout", result));
        return nullptr;
    }
    // A shadow map's set (ADR 0084): its depth image for the fragment stage, and the
    // camera-clip-to-light matrix for the vertex stage.
    VkDescriptorSetLayoutBinding shadow_bindings[2]{};
    shadow_bindings[0].binding = 0;
    shadow_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadow_bindings[0].descriptorCount = 1;
    shadow_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadow_bindings[1].binding = 1;
    shadow_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadow_bindings[1].descriptorCount = 1;
    shadow_bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layout_info.bindingCount = 2;
    layout_info.pBindings = shadow_bindings;
    result = vkCreateDescriptorSetLayout(impl->device, &layout_info, nullptr, &impl->shadow_layout);
    if (result != VK_SUCCESS) {
        impl->shadow_layout = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("shadow descriptor layout", result));
        return nullptr;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(impl->physical, &properties);
    impl->max_image_dimension = properties.limits.maxImageDimension2D;
    impl->max_push_constant_bytes = properties.limits.maxPushConstantsSize;
    impl->uniform_alignment = properties.limits.minUniformBufferOffsetAlignment;

    // GPU timestamps, when the graphics queue writes them (ADR 0039).
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(impl->physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(impl->physical, &family_count, families.data());
    if (impl->queue_family < families.size() && families[impl->queue_family].timestampValidBits > 0
        && properties.limits.timestampPeriod > 0.F) {
        VkQueryPoolCreateInfo query_info{};
        query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = 2;
        result = vkCreateQueryPool(impl->device, &query_info, nullptr, &impl->timestamps);
        if (result != VK_SUCCESS) {
            impl->timestamps = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateQueryPool", result));
            return nullptr;
        }
        impl->timestamp_bits = families[impl->queue_family].timestampValidBits;
        impl->timestamp_period = properties.limits.timestampPeriod;
    }

    return std::unique_ptr<Device>(new Device(std::move(impl)));
}

const DeviceInfo& Device::info() const
{
    return impl_->info;
}

std::uint32_t Device::graphics_queue_family() const
{
    return impl_->queue_family;
}

std::span<const std::string> Device::enabled_extensions() const
{
    return impl_->extensions;
}

bool Device::wait_idle(RhiError& error) const
{
    error = {};
    const VkResult result = vkDeviceWaitIdle(impl_->device);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkDeviceWaitIdle", result));
    }
    return true;
}

bool Device::gpu_timestamps() const
{
    return impl_->timestamp_bits > 0;
}

void* Device::native_device() const
{
    return static_cast<void*>(impl_->device);
}

void* Device::native_physical_device() const
{
    return static_cast<void*>(impl_->physical);
}

void* Device::native_graphics_queue() const
{
    return static_cast<void*>(impl_->queue);
}

namespace detail {

VkDevice DeviceAccess::device(const Device& device)
{
    return device.impl_->device;
}

VmaAllocator DeviceAccess::allocator(const Device& device)
{
    return device.impl_->allocator;
}

VkQueue DeviceAccess::queue(const Device& device)
{
    return device.impl_->queue;
}

std::uint32_t DeviceAccess::timestamp_bits(const Device& device)
{
    return device.impl_->timestamp_bits;
}

float DeviceAccess::timestamp_period(const Device& device)
{
    return device.impl_->timestamp_period;
}

VkDescriptorSetLayout DeviceAccess::texture_set_layout(const Device& device)
{
    return device.impl_->texture_layout;
}

VkDescriptorSetLayout DeviceAccess::skin_set_layout(const Device& device)
{
    return device.impl_->skin_layout;
}

VkDescriptorSetLayout DeviceAccess::shadow_set_layout(const Device& device)
{
    return device.impl_->shadow_layout;
}

VkDeviceSize DeviceAccess::uniform_alignment(const Device& device)
{
    return device.impl_->uniform_alignment;
}

std::uint32_t DeviceAccess::max_image_dimension(const Device& device)
{
    return device.impl_->max_image_dimension;
}

std::uint32_t DeviceAccess::max_push_constant_bytes(const Device& device)
{
    return device.impl_->max_push_constant_bytes;
}

bool DeviceAccess::submit_and_wait(const Device& device, const std::function<void(VkCommandBuffer)>& record,
                                   RhiError& error, std::optional<std::uint64_t>* gpu_ns)
{
    if (gpu_ns != nullptr) {
        *gpu_ns = std::nullopt;
    }
    error = {};
    const Device::Impl& impl = *device.impl_;
    VkResult result = vkResetCommandBuffer(impl.command_buffer, 0);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkResetCommandBuffer", result));
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(impl.command_buffer, &begin);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkBeginCommandBuffer", result));
    }
    const bool timed = gpu_ns != nullptr && impl.timestamps != VK_NULL_HANDLE;
    if (timed) {
        vkCmdResetQueryPool(impl.command_buffer, impl.timestamps, 0, 2);
        detail::write_timestamp(impl.command_buffer, impl.timestamps, 0, false);
    }
    record(impl.command_buffer);
    if (timed) {
        detail::write_timestamp(impl.command_buffer, impl.timestamps, 1, true);
    }
    result = vkEndCommandBuffer(impl.command_buffer);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkEndCommandBuffer", result));
    }
    result = vkResetFences(impl.device, 1, &impl.fence);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkResetFences", result));
    }
    VkCommandBufferSubmitInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = impl.command_buffer;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    result = vkQueueSubmit2(impl.queue, 1, &submit, impl.fence);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkQueueSubmit2", result));
    }
    // Ten seconds is far beyond any one-shot transfer; a timeout means a hang.
    constexpr std::uint64_t timeout_ns = 10'000'000'000ULL;
    result = vkWaitForFences(impl.device, 1, &impl.fence, VK_TRUE, timeout_ns);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkWaitForFences", result));
    }
    if (timed) {
        *gpu_ns =
            detail::read_gpu_interval(impl.device, impl.timestamps, 0, impl.timestamp_bits, impl.timestamp_period);
    }
    return true;
}

}

const char* to_string(DeviceType type) noexcept
{
    switch (type) {
    case DeviceType::Integrated: return "integrated";
    case DeviceType::Discrete: return "discrete";
    case DeviceType::Virtual: return "virtual";
    case DeviceType::Cpu: return "cpu";
    case DeviceType::Other: return "other";
    }
    return "other";
}

}
