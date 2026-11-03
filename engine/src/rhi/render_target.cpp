#include <lykoware/rhi/render_target.hpp>

#include "common.hpp"
#include "device_access.hpp"
#include "pipeline_access.hpp"

#include <lykoware/log.hpp>

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace lykoware::rhi {
namespace {

using detail::colour_range;
using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;
using detail::image_barrier;

constexpr VkFormat target_format = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t bytes_per_texel = 4;

}

struct RenderTarget::Impl {
    ~Impl()
    {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(DeviceAccess::device(*device), view, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(DeviceAccess::allocator(*device), image, image_allocation);
        }
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(DeviceAccess::allocator(*device), buffer, buffer_allocation);
        }
    }

    const Device* device = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation image_allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation buffer_allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    // Layout the image is in after the last completed submission.
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool rendered = false;
};

RenderTarget::RenderTarget(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

RenderTarget::~RenderTarget() = default;

std::unique_ptr<RenderTarget> RenderTarget::create(const Device& device, const RenderTargetDesc& desc, RhiError& error)
{
    error = {};
    if (desc.width < 1 || desc.height < 1 || desc.width > render_target_max_extent
        || desc.height > render_target_max_extent) {
        fail(error, RhiFailure::InvalidArgument,
             "render target extent must be 1.." + std::to_string(render_target_max_extent) + " in each axis");
        return nullptr;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(static_cast<VkPhysicalDevice>(device.native_physical_device()), &properties);
    if (desc.width > properties.limits.maxImageDimension2D || desc.height > properties.limits.maxImageDimension2D) {
        fail(error, RhiFailure::InvalidArgument,
             "render target extent exceeds the device limit of "
                 + std::to_string(properties.limits.maxImageDimension2D));
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->width = desc.width;
    impl->height = desc.height;
    VmaAllocator allocator = DeviceAccess::allocator(device);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = target_format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo image_allocation_info{};
    image_allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    VkResult result = vmaCreateImage(allocator, &image_info, &image_allocation_info, &impl->image,
                                     &impl->image_allocation, nullptr);
    if (result != VK_SUCCESS) {
        impl->image = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateImage", result));
        return nullptr;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = impl->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = target_format;
    view_info.subresourceRange = colour_range();
    result = vkCreateImageView(DeviceAccess::device(device), &view_info, nullptr, &impl->view);
    if (result != VK_SUCCESS) {
        impl->view = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView", result));
        return nullptr;
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = static_cast<VkDeviceSize>(desc.width) * desc.height * bytes_per_texel;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo buffer_allocation_info{};
    buffer_allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    buffer_allocation_info.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mapped_info{};
    result = vmaCreateBuffer(allocator, &buffer_info, &buffer_allocation_info, &impl->buffer,
                             &impl->buffer_allocation, &mapped_info);
    if (result != VK_SUCCESS) {
        impl->buffer = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateBuffer", result));
        return nullptr;
    }
    impl->mapped = mapped_info.pMappedData;
    if (impl->mapped == nullptr) {
        fail(error, RhiFailure::DriverError, "read-back buffer could not be mapped");
        return nullptr;
    }

    return std::unique_ptr<RenderTarget>(new RenderTarget(std::move(impl)));
}

std::uint32_t RenderTarget::width() const
{
    return impl_->width;
}

std::uint32_t RenderTarget::height() const
{
    return impl_->height;
}

bool RenderTarget::clear(const ClearColor& color, RhiError& error)
{
    return render(color, VK_NULL_HANDLE, 0, error);
}

ColorFormat RenderTarget::color_format() const
{
    return ColorFormat::Rgba8Unorm;
}

bool RenderTarget::draw(const ClearColor& clear, const GraphicsPipeline& pipeline, std::uint32_t vertex_count,
                        RhiError& error)
{
    error = {};
    if (pipeline.color_format() != color_format()) {
        return fail(error, RhiFailure::InvalidArgument,
                    "the pipeline targets a different colour format than this render target");
    }
    return render(clear, static_cast<VkPipeline>(pipeline.native_handle()), vertex_count, error);
}

bool RenderTarget::render(const ClearColor& color, void* pipeline_handle, std::uint32_t vertex_count,
                          RhiError& error)
{
    Impl& impl = *impl_;
    const auto pipeline = static_cast<VkPipeline>(pipeline_handle);
    const bool ok = DeviceAccess::submit_and_wait(*impl.device, [&impl, &color, pipeline,
                                                                 vertex_count](VkCommandBuffer commands) {
        // Previous contents are discarded by the clear, so the old layout is
        // treated as UNDEFINED.
        image_barrier(commands, impl.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        const float rgba[4] = {color.r, color.g, color.b, color.a};
        detail::record_draw_pass(commands, impl.view, {impl.width, impl.height}, rgba, pipeline, vertex_count);
    }, error);
    if (!ok) {
        impl.rendered = false;
        impl.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return false;
    }
    impl.rendered = true;
    impl.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

bool RenderTarget::read_back(Image& image, RhiError& error) const
{
    error = {};
    image.clear();
    Impl& impl = *impl_;
    if (!impl.rendered) {
        return fail(error, RhiFailure::InvalidArgument, "render target has not been rendered; contents are undefined");
    }

    const bool ok = DeviceAccess::submit_and_wait(*impl.device, [&impl](VkCommandBuffer commands) {
        if (impl.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            image_barrier(commands, impl.image, impl.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        }
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0; // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {impl.width, impl.height, 1};
        vkCmdCopyImageToBuffer(commands, impl.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, impl.buffer, 1, &region);

        // Make the copy visible to the host read below.
        VkBufferMemoryBarrier2 to_host{};
        to_host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        to_host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        to_host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        to_host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        to_host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_host.buffer = impl.buffer;
        to_host.offset = 0;
        to_host.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &to_host;
        vkCmdPipelineBarrier2(commands, &dependency);
    }, error);
    if (!ok) {
        return false;
    }
    impl.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    const VkResult invalidated =
        vmaInvalidateAllocation(DeviceAccess::allocator(*impl.device), impl.buffer_allocation, 0, VK_WHOLE_SIZE);
    if (invalidated != VK_SUCCESS) {
        return fail(error, RhiFailure::DriverError, driver_failure("vmaInvalidateAllocation", invalidated));
    }
    if (!image.reset(impl.width, impl.height)) {
        return fail(error, RhiFailure::InvalidArgument, "render target is larger than the image harness allows");
    }
    const auto* source = static_cast<const std::uint8_t*>(impl.mapped);
    auto destination = image.pixels();
    const std::size_t texels = static_cast<std::size_t>(impl.width) * impl.height;
    for (std::size_t texel = 0; texel < texels; ++texel) {
        destination[texel * 3 + 0] = source[texel * bytes_per_texel + 0];
        destination[texel * 3 + 1] = source[texel * bytes_per_texel + 1];
        destination[texel * 3 + 2] = source[texel * bytes_per_texel + 2];
    }
    return true;
}

RenderTargetCapture::RenderTargetCapture(const RenderTarget& target) noexcept
    : target_{target}
{
}

bool RenderTargetCapture::capture(Image& destination) const
{
    RhiError error;
    if (!target_.read_back(destination, error)) {
        log(LogLevel::error, "RenderTargetCapture: " + error.message);
        destination.clear();
        return false;
    }
    return true;
}

}
