#include <lykoware/rhi/swapchain.hpp>

#include "common.hpp"
#include "device_access.hpp"
#include "pipeline_access.hpp"
#include "skin_arena.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lykoware::rhi {
namespace {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

// Far beyond any frame; a timeout means the GPU or compositor hung.
constexpr std::uint64_t wait_timeout_ns = 10'000'000'000ULL;

// Frame times kept for take_gpu_frame_times(); older ones are dropped.
constexpr std::size_t gpu_frame_time_capacity = 1024;

struct Frame {
    detail::SkinArena skin;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    // The frame last submitted from this slot has timestamps to read (ADR 0039).
    bool timing_pending = false;
    std::uint64_t timed_frame = 0;
    std::chrono::steady_clock::time_point submitted_at{};
};

bool has_extension(const Device& device, std::string_view name)
{
    for (const std::string& enabled : device.enabled_extensions()) {
        if (enabled == name) {
            return true;
        }
    }
    return false;
}

// 8-bit UNORM, like RenderTarget, so pipelines have a ColorFormat to target.
// sRGB/HDR output waits for the colour-pipeline decision. The standard
// colour space is preferred; any colour space is accepted after that.
std::optional<VkSurfaceFormatKHR> choose_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const bool standard_space : {true, false}) {
        for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
            for (const VkSurfaceFormatKHR& format : formats) {
                if (format.format == wanted
                    && (!standard_space || format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
                    return format;
                }
            }
        }
    }
    return std::nullopt;
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
{
    if (!vsync) {
        for (const VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
                return wanted;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR; // required to be supported
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
    for (const VkCompositeAlphaFlagBitsKHR wanted :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if ((supported & wanted) != 0) {
            return wanted;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

}

struct Swapchain::Impl {
    ~Impl()
    {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            destroy_images();
            if (swapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            }
            if (timestamps != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device, timestamps, nullptr);
            }
            for (Frame& frame : frames) {
                if (frame.image_available != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, frame.image_available, nullptr);
                }
                if (frame.in_flight != VK_NULL_HANDLE) {
                    vkDestroyFence(device, frame.in_flight, nullptr);
                }
                if (frame.pool != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(device, frame.pool, nullptr);
                }
            }
        }
        if (surface != VK_NULL_HANDLE && window != nullptr && instance != nullptr) {
            window->destroy_vulkan_surface(instance->native_handle(), static_cast<void*>(surface), nullptr);
        }
    }

    void destroy_images()
    {
        if (depth_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depth_view, nullptr);
            depth_view = VK_NULL_HANDLE;
        }
        if (depth_image != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, depth_image, depth_allocation);
            depth_image = VK_NULL_HANDLE;
            depth_allocation = VK_NULL_HANDLE;
        }
        for (VkSemaphore semaphore : render_finished) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        render_finished.clear();
        for (VkImageView view : views) {
            vkDestroyImageView(device, view, nullptr);
        }
        views.clear();
        images.clear();
    }

    // Size the swapchain should have now: the surface's fixed extent when it
    // reports one, otherwise the window's pixel size clamped to the limits.
    bool desired_extent(VkExtent2D& wanted, RhiError& error) const
    {
        VkSurfaceCapabilitiesKHR caps{};
        const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        if (result != VK_SUCCESS) {
            return fail(error, RhiFailure::DriverError,
                        driver_failure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result));
        }
        if (caps.currentExtent.width != UINT32_MAX) {
            wanted = caps.currentExtent;
            return true;
        }
        const float density = window->pixel_density() > 0.F ? window->pixel_density() : 1.F;
        const auto pixels = [density](int logical) {
            return logical <= 0 ? 0U : static_cast<std::uint32_t>(std::lround(static_cast<float>(logical) * density));
        };
        wanted.width = std::clamp(pixels(window->width()), caps.minImageExtent.width, caps.maxImageExtent.width);
        wanted.height = std::clamp(pixels(window->height()), caps.minImageExtent.height, caps.maxImageExtent.height);
        return true;
    }

    bool build(RhiError& error)
    {
        VkExtent2D wanted{};
        if (!desired_extent(wanted, error)) {
            return false;
        }
        if (wanted.width == 0 || wanted.height == 0) {
            // Minimised: keep nothing to present to until the window has an area.
            vkDeviceWaitIdle(device);
            destroy_images();
            if (swapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device, swapchain, nullptr);
                swapchain = VK_NULL_HANDLE;
            }
            extent = {0, 0};
            return true;
        }

        VkSurfaceCapabilitiesKHR caps{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        if (result != VK_SUCCESS) {
            return fail(error, RhiFailure::DriverError,
                        driver_failure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result));
        }
        std::uint32_t count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) {
            count = std::min(count, caps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = count;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = wanted;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
        info.presentMode = present_mode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = swapchain;

        // Nothing may still use the old images, views or semaphores.
        vkDeviceWaitIdle(device);
        VkSwapchainKHR replacement = VK_NULL_HANDLE;
        result = vkCreateSwapchainKHR(device, &info, nullptr, &replacement);
        destroy_images();
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        if (result != VK_SUCCESS) {
            extent = {0, 0};
            return fail(error, RhiFailure::DriverError, driver_failure("vkCreateSwapchainKHR", result));
        }
        swapchain = replacement;
        extent = wanted;

        std::uint32_t image_total = 0;
        result = vkGetSwapchainImagesKHR(device, swapchain, &image_total, nullptr);
        if (result != VK_SUCCESS) {
            return fail(error, RhiFailure::DriverError, driver_failure("vkGetSwapchainImagesKHR", result));
        }
        images.resize(image_total);
        result = vkGetSwapchainImagesKHR(device, swapchain, &image_total, images.data());
        if (result != VK_SUCCESS) {
            return fail(error, RhiFailure::DriverError, driver_failure("vkGetSwapchainImagesKHR", result));
        }
        for (VkImage image : images) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format.format;
            view_info.subresourceRange = detail::colour_range();
            VkImageView view = VK_NULL_HANDLE;
            result = vkCreateImageView(device, &view_info, nullptr, &view);
            if (result != VK_SUCCESS) {
                return fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView", result));
            }
            views.push_back(view);
            // One "render finished" semaphore per image: present may still be
            // waiting on it when the same frame slot comes round again.
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkSemaphore semaphore = VK_NULL_HANDLE;
            result = vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore);
            if (result != VK_SUCCESS) {
                return fail(error, RhiFailure::DriverError, driver_failure("vkCreateSemaphore", result));
            }
            render_finished.push_back(semaphore);
        }
        if (want_depth && !build_depth(error)) {
            return false;
        }
        needs_rebuild = false;
        return true;
    }

    // One depth image for the whole swapchain. Each frame's depth barrier
    // waits for the previous frame's depth writes, so frames in flight never
    // use it at the same time.
    bool build_depth(RhiError& error)
    {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = detail::depth_format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
        VkResult result = vmaCreateImage(allocator, &info, &allocation_info, &depth_image, &depth_allocation, nullptr);
        if (result != VK_SUCCESS) {
            depth_image = VK_NULL_HANDLE;
            return fail(error, RhiFailure::DriverError, driver_failure("vmaCreateImage (depth)", result));
        }
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = depth_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = detail::depth_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(device, &view_info, nullptr, &depth_view);
        if (result != VK_SUCCESS) {
            depth_view = VK_NULL_HANDLE;
            return fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView (depth)", result));
        }
        return true;
    }

    bool rebuild(RhiError& error)
    {
        ++rebuilds;
        return build(error);
    }

    // Reads the timestamps of the frame last submitted from slot `index`, whose fence has
    // signalled, into gpu_times.
    void collect_gpu_time(std::uint32_t index)
    {
        Frame& frame = frames[index];
        if (!frame.timing_pending) {
            return;
        }
        frame.timing_pending = false;
        const std::optional<std::uint64_t> nanoseconds =
            detail::read_gpu_interval(device, timestamps, index * 2, timestamp_bits, timestamp_period);
        if (!nanoseconds) {
            return;
        }
        if (gpu_times.size() >= gpu_frame_time_capacity) {
            gpu_times.erase(gpu_times.begin());
        }
        gpu_times.push_back(GpuFrameTime{frame.timed_frame, *nanoseconds, frame.submitted_at});
    }

    // One acquire, clear, draw, present cycle. With `meshes` null, draws
    // vertex_count vertices with `pipeline` (when not null); otherwise draws
    // the meshes, with depth when `depth` is set.
    bool render_frame(const ClearColor& color, VkPipeline pipeline, std::uint32_t vertex_count, RhiError& error,
                      std::vector<detail::PreparedDraw>* meshes = nullptr, VkPipelineLayout layout = VK_NULL_HANDLE,
                      bool depth = false);

    const Device* owner_device = nullptr;
    const Instance* instance = nullptr;
    Window* window = nullptr;
    VmaAllocator allocator = VK_NULL_HANDLE;
    bool want_depth = false;
    VkImage depth_image = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR format{};
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent{0, 0};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> render_finished;
    std::array<Frame, frames_in_flight> frames{};
    std::uint32_t frame_index = 0;
    std::uint64_t presented = 0;
    std::uint32_t rebuilds = 0;
    bool needs_rebuild = false;
    // GPU frame timing (ADR 0039): two timestamp queries per frame slot, or none when the device
    // has no timestamps.
    VkQueryPool timestamps = VK_NULL_HANDLE;
    std::uint32_t timestamp_bits = 0;
    float timestamp_period = 0.F;
    std::uint64_t submitted = 0;
    std::vector<GpuFrameTime> gpu_times;
};

Swapchain::Swapchain(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Swapchain::~Swapchain() = default;

std::unique_ptr<Swapchain> Swapchain::create(
    const Instance& instance, const Device& device, Window& window, const SwapchainConfig& config, RhiError& error)
{
    error = {};
    if (!has_extension(device, swapchain_device_extension)) {
        fail(error, RhiFailure::InvalidArgument,
             std::string{"the device was not created with "} + swapchain_device_extension);
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->owner_device = &device;
    impl->instance = &instance;
    impl->window = &window;
    impl->physical = static_cast<VkPhysicalDevice>(device.native_physical_device());
    impl->device = DeviceAccess::device(device);
    impl->allocator = DeviceAccess::allocator(device);
    impl->want_depth = config.depth;
    if (config.depth) {
        VkFormatProperties format_properties{};
        vkGetPhysicalDeviceFormatProperties(impl->physical, detail::depth_format, &format_properties);
        if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
            fail(error, RhiFailure::NoSuitableDevice, "the device cannot render 32-bit float depth");
            return nullptr;
        }
    }
    impl->queue = DeviceAccess::queue(device);

    void* surface = nullptr;
    std::string surface_error;
    if (!window.create_vulkan_surface(instance.native_handle(), nullptr, surface, surface_error)) {
        fail(error, RhiFailure::DriverError, "window surface: " + surface_error);
        return nullptr;
    }
    impl->surface = static_cast<VkSurfaceKHR>(surface);

    VkBool32 can_present = VK_FALSE;
    VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(impl->physical, device.graphics_queue_family(),
                                                           impl->surface, &can_present);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkGetPhysicalDeviceSurfaceSupportKHR", result));
        return nullptr;
    }
    if (can_present != VK_TRUE) {
        fail(error, RhiFailure::NoSuitableDevice,
             "the device's graphics queue cannot present to this window; separate present queues are not supported");
        return nullptr;
    }

    std::uint32_t count = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(impl->physical, impl->surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (result == VK_SUCCESS) {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(impl->physical, impl->surface, &count, formats.data());
    }
    if (result != VK_SUCCESS || count == 0) {
        fail(error, RhiFailure::DriverError, driver_failure("vkGetPhysicalDeviceSurfaceFormatsKHR", result));
        return nullptr;
    }
    const std::optional<VkSurfaceFormatKHR> format = choose_format(formats);
    if (!format) {
        fail(error, RhiFailure::NoSuitableDevice,
             "the window surface offers no 8-bit UNORM BGRA or RGBA format");
        return nullptr;
    }
    impl->format = *format;

    result = vkGetPhysicalDeviceSurfacePresentModesKHR(impl->physical, impl->surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    if (result == VK_SUCCESS) {
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(impl->physical, impl->surface, &count, modes.data());
    }
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkGetPhysicalDeviceSurfacePresentModesKHR", result));
        return nullptr;
    }
    impl->present_mode = choose_present_mode(modes, config.vsync);

    for (Frame& frame : impl->frames) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = device.graphics_queue_family();
        result = vkCreateCommandPool(impl->device, &pool_info, nullptr, &frame.pool);
        if (result != VK_SUCCESS) {
            frame.pool = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateCommandPool", result));
            return nullptr;
        }
        VkCommandBufferAllocateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        buffer_info.commandPool = frame.pool;
        buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        buffer_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(impl->device, &buffer_info, &frame.commands);
        if (result != VK_SUCCESS) {
            fail(error, RhiFailure::DriverError, driver_failure("vkAllocateCommandBuffers", result));
            return nullptr;
        }
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the first wait must not block
        result = vkCreateFence(impl->device, &fence_info, nullptr, &frame.in_flight);
        if (result != VK_SUCCESS) {
            frame.in_flight = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateFence", result));
            return nullptr;
        }
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(impl->device, &semaphore_info, nullptr, &frame.image_available);
        if (result != VK_SUCCESS) {
            frame.image_available = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateSemaphore", result));
            return nullptr;
        }
    }

    if (device.gpu_timestamps()) {
        VkQueryPoolCreateInfo query_info{};
        query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = 2 * frames_in_flight;
        result = vkCreateQueryPool(impl->device, &query_info, nullptr, &impl->timestamps);
        if (result != VK_SUCCESS) {
            impl->timestamps = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateQueryPool", result));
            return nullptr;
        }
        impl->timestamp_bits = DeviceAccess::timestamp_bits(device);
        impl->timestamp_period = DeviceAccess::timestamp_period(device);
    }

    if (!impl->build(error)) {
        return nullptr;
    }
    return std::unique_ptr<Swapchain>(new Swapchain(std::move(impl)));
}

std::uint32_t Swapchain::width() const
{
    return impl_->extent.width;
}

std::uint32_t Swapchain::height() const
{
    return impl_->extent.height;
}

std::uint32_t Swapchain::image_count() const
{
    return static_cast<std::uint32_t>(impl_->images.size());
}

std::uint64_t Swapchain::frames_presented() const
{
    return impl_->presented;
}

std::uint32_t Swapchain::rebuild_count() const
{
    return impl_->rebuilds;
}

std::vector<GpuFrameTime> Swapchain::take_gpu_frame_times()
{
    Impl& impl = *impl_;
    if (impl.timestamps != VK_NULL_HANDLE) {
        // Frames whose fence has signalled are done; the rest are read later.
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
            const Frame& frame = impl.frames[index];
            if (frame.timing_pending && vkGetFenceStatus(impl.device, frame.in_flight) == VK_SUCCESS) {
                impl.collect_gpu_time(index);
            }
        }
    }
    std::vector<GpuFrameTime> taken = std::move(impl.gpu_times);
    impl.gpu_times.clear();
    std::sort(taken.begin(), taken.end(),
              [](const GpuFrameTime& a, const GpuFrameTime& b) { return a.frame < b.frame; });
    return taken;
}

ColorFormat Swapchain::color_format() const
{
    return impl_->format.format == VK_FORMAT_B8G8R8A8_UNORM ? ColorFormat::Bgra8Unorm : ColorFormat::Rgba8Unorm;
}

bool Swapchain::render_clear(const ClearColor& color, RhiError& error)
{
    error = {};
    return impl_->render_frame(color, VK_NULL_HANDLE, 0, error);
}

bool Swapchain::render_draw(const ClearColor& clear, const GraphicsPipeline& pipeline, std::uint32_t vertex_count,
                            RhiError& error)
{
    error = {};
    if (pipeline.color_format() != color_format()) {
        return fail(error, RhiFailure::InvalidArgument,
                    "the pipeline targets a different colour format than this swapchain");
    }
    return impl_->render_frame(clear, static_cast<VkPipeline>(pipeline.native_handle()), vertex_count, error);
}

bool Swapchain::has_depth() const
{
    return impl_->want_depth;
}

bool Swapchain::render_meshes(const ClearColor& clear, const GraphicsPipeline& pipeline,
                              std::span<const MeshDraw> meshes, RhiError& error)
{
    error = {};
    std::vector<detail::PreparedDraw> draws;
    if (!detail::prepare_mesh_draws(pipeline, color_format(), has_depth(), meshes, draws, error)) {
        return false;
    }
    return impl_->render_frame(clear, static_cast<VkPipeline>(pipeline.native_handle()), 0, error, &draws,
                               static_cast<VkPipelineLayout>(pipeline.native_layout()), pipeline.depth());
}

bool Swapchain::Impl::render_frame(const ClearColor& color, VkPipeline pipeline, std::uint32_t vertex_count,
                                   RhiError& error, std::vector<detail::PreparedDraw>* meshes,
                                   VkPipelineLayout layout, bool depth)
{
    Impl& impl = *this;

    // Minimised (ADR 0040): nothing is shown, and some platforms block presentation
    // until the window is visible again, so skip the frame and keep the swapchain.
    if (impl.window->minimized()) {
        return true;
    }

    VkExtent2D wanted{};
    if (!impl.desired_extent(wanted, error)) {
        return false;
    }
    if (wanted.width == 0 || wanted.height == 0) {
        // No area (some platforms report a minimised window this way): release
        // the swapchain once, then skip frames until the window has an area again.
        if (impl.swapchain != VK_NULL_HANDLE && !impl.rebuild(error)) {
            return false;
        }
        return true;
    }
    const bool resized = wanted.width != impl.extent.width || wanted.height != impl.extent.height;
    if ((impl.needs_rebuild || resized || impl.swapchain == VK_NULL_HANDLE) && !impl.rebuild(error)) {
        return false;
    }

    Frame& frame = impl.frames[impl.frame_index];
    VkResult result = vkWaitForFences(impl.device, 1, &frame.in_flight, VK_TRUE, wait_timeout_ns);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkWaitForFences", result));
    }
    impl.collect_gpu_time(impl.frame_index);
    // The slot is idle. Upload before acquiring an image/resetting the fence, so an upload
    // refusal leaves synchronization reusable on the next call.
    if (meshes != nullptr && !frame.skin.prepare(*impl.owner_device, *meshes, error)) { return false; }


    std::uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(impl.device, impl.swapchain, wait_timeout_ns, frame.image_available,
                                   VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        impl.needs_rebuild = true; // nothing acquired, semaphore untouched
        return true;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkAcquireNextImageKHR", result));
    }
    const bool suboptimal = result == VK_SUBOPTIMAL_KHR;

    result = vkResetFences(impl.device, 1, &frame.in_flight);
    if (result == VK_SUCCESS) {
        result = vkResetCommandPool(impl.device, frame.pool, 0);
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (result == VK_SUCCESS) {
        result = vkBeginCommandBuffer(frame.commands, &begin);
    }
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("frame command setup", result));
    }
    const std::uint32_t first_query = impl.frame_index * 2;
    if (impl.timestamps != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, impl.timestamps, first_query, 2);
        detail::write_timestamp(frame.commands, impl.timestamps, first_query, false);
    }

    const VkImage image = impl.images[image_index];
    detail::image_barrier(frame.commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    const float rgba[4] = {color.r, color.g, color.b, color.a};
    if (meshes != nullptr) {
        const VkImageView pass_depth = depth ? impl.depth_view : VK_NULL_HANDLE;
        if (pass_depth != VK_NULL_HANDLE) {
            detail::depth_barrier(frame.commands, impl.depth_image);
        }
        detail::record_mesh_pass(frame.commands, impl.views[image_index], pass_depth, impl.extent, rgba, pipeline,
                                 layout, meshes->data(), meshes->size());
    } else {
        detail::record_draw_pass(frame.commands, impl.views[image_index], impl.extent, rgba, pipeline, vertex_count);
    }
    detail::image_barrier(frame.commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    if (impl.timestamps != VK_NULL_HANDLE) {
        detail::write_timestamp(frame.commands, impl.timestamps, first_query + 1, true);
    }
    result = vkEndCommandBuffer(frame.commands);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkEndCommandBuffer", result));
    }

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame.image_available;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = impl.render_finished[image_index];
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = frame.commands;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait_info;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal_info;
    result = vkQueueSubmit2(impl.queue, 1, &submit, frame.in_flight);
    if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkQueueSubmit2", result));
    }
    frame.timing_pending = impl.timestamps != VK_NULL_HANDLE;
    frame.timed_frame = impl.submitted++;
    frame.submitted_at = std::chrono::steady_clock::now();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &impl.render_finished[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &impl.swapchain;
    present.pImageIndices = &image_index;
    result = vkQueuePresentKHR(impl.queue, &present);
    impl.frame_index = (impl.frame_index + 1) % frames_in_flight;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal) {
        impl.needs_rebuild = true;
    } else if (result != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vkQueuePresentKHR", result));
    }
    if (result != VK_ERROR_OUT_OF_DATE_KHR) {
        ++impl.presented;
    }
    return true;
}

}
