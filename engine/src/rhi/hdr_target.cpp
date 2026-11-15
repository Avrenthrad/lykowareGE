#include <lykoware/rhi/hdr_target.hpp>

#include "common.hpp"
#include "device_access.hpp"
#include "pipeline_access.hpp"

#include <lykoware/rhi/device.hpp>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lykoware::rhi {

namespace {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

constexpr VkFormat hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;

bool make_image(VmaAllocator allocator, VkFormat format, std::uint32_t width, std::uint32_t height,
                VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation, RhiError& error)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    const VkResult result = vmaCreateImage(allocator, &info, &allocation_info, &image, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        image = VK_NULL_HANDLE;
        return fail(error, RhiFailure::DriverError, driver_failure("vmaCreateImage (HDR target)", result));
    }
    return true;
}

bool make_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect, VkImageView& view,
               RhiError& error)
{
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.layerCount = 1;
    const VkResult result = vkCreateImageView(device, &info, nullptr, &view);
    if (result != VK_SUCCESS) {
        view = VK_NULL_HANDLE;
        return fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView (HDR target)", result));
    }
    return true;
}

// A layout transition of the whole depth image. From UNDEFINED the old contents are discarded.
void depth_transition(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
                      VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

}

struct HdrTarget::Impl {
    ~Impl()
    {
        if (device == nullptr) {
            return;
        }
        const VkDevice vk_device = DeviceAccess::device(*device);
        const VmaAllocator allocator = DeviceAccess::allocator(*device);
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(vk_device, pool, nullptr); // frees the set
        }
        for (VkSampler made : {sampler, depth_sampler}) {
            if (made != VK_NULL_HANDLE) {
                vkDestroySampler(vk_device, made, nullptr);
            }
        }
        for (VkImageView view : {colour_view, depth_view}) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(vk_device, view, nullptr);
            }
        }
        if (colour != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, colour, colour_allocation);
        }
        if (depth != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, depth, depth_allocation);
        }
    }

    // Clears and draws `draws` (none clears only), leaving the colour ready to sample.
    bool record(const ClearColor& clear, VkPipeline pipeline, VkPipelineLayout layout,
                const std::vector<detail::PreparedDraw>& draws, RhiError& error)
    {
        return DeviceAccess::submit_and_wait(*device, [&](VkCommandBuffer commands) {
            // Earlier frames may still sample the colour: order after them.
            detail::image_barrier(commands, colour, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
            // Likewise the depth: an earlier post pass may still read it.
            depth_transition(commands, depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                 | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            const float rgba[4] = {clear.r, clear.g, clear.b, clear.a};
            detail::record_mesh_pass(commands, colour_view, depth_view, {width, height}, rgba, pipeline, layout,
                                     draws.data(), draws.size(), true);
            detail::image_barrier(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            // The depth too, for a post pass that reads it (ADR 0099).
            depth_transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }, error);
    }

    const Device* device = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImage colour = VK_NULL_HANDLE;
    VmaAllocation colour_allocation = VK_NULL_HANDLE;
    VkImageView colour_view = VK_NULL_HANDLE;
    VkImage depth = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler depth_sampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSet depth_set = VK_NULL_HANDLE;
};

HdrTarget::HdrTarget(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

HdrTarget::~HdrTarget() = default;

std::unique_ptr<HdrTarget> HdrTarget::create(const Device& device, std::uint32_t width, std::uint32_t height,
                                             RhiError& error)
{
    error = {};
    const std::uint32_t limit = DeviceAccess::max_image_dimension(device);
    if (width < 1 || height < 1 || width > render_target_max_extent || height > render_target_max_extent
        || width > limit || height > limit) {
        fail(error, RhiFailure::InvalidArgument,
             "HDR target extent must be 1.." + std::to_string(render_target_max_extent)
                 + " in each axis and within the device's 2D limit");
        return nullptr;
    }
    const auto physical = static_cast<VkPhysicalDevice>(device.native_physical_device());
    VkFormatProperties colour_properties{};
    vkGetPhysicalDeviceFormatProperties(physical, hdr_format, &colour_properties);
    const VkFormatFeatureFlags colour_needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
        | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    VkFormatProperties depth_properties{};
    vkGetPhysicalDeviceFormatProperties(physical, detail::depth_format, &depth_properties);
    const VkFormatFeatureFlags depth_needed =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((colour_properties.optimalTilingFeatures & colour_needed) != colour_needed
        || (depth_properties.optimalTilingFeatures & depth_needed) != depth_needed) {
        fail(error, RhiFailure::NoSuitableDevice,
             "the device cannot render, blend and sample 16-bit float colour, or sample its depth");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->width = width;
    impl->height = height;
    const VkDevice vk_device = DeviceAccess::device(device);
    const VmaAllocator allocator = DeviceAccess::allocator(device);
    if (!make_image(allocator, hdr_format, width, height,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, impl->colour,
                    impl->colour_allocation, error)
        || !make_view(vk_device, impl->colour, hdr_format, VK_IMAGE_ASPECT_COLOR_BIT, impl->colour_view, error)
        || !make_image(allocator, detail::depth_format, width, height,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, impl->depth,
                       impl->depth_allocation, error)
        || !make_view(vk_device, impl->depth, detail::depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, impl->depth_view,
                      error)) {
        return nullptr;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkResult result = vkCreateSampler(vk_device, &sampler_info, nullptr, &impl->sampler);
    if (result != VK_SUCCESS) {
        impl->sampler = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateSampler (HDR target)", result));
        return nullptr;
    }

    // Depth is read texel for texel: no filtering, which the format need not support.
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    result = vkCreateSampler(vk_device, &sampler_info, nullptr, &impl->depth_sampler);
    if (result != VK_SUCCESS) {
        impl->depth_sampler = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateSampler (HDR target depth)", result));
        return nullptr;
    }

    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 2;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    result = vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &impl->pool);
    if (result != VK_SUCCESS) {
        impl->pool = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateDescriptorPool (HDR target)", result));
        return nullptr;
    }
    const VkDescriptorSetLayout layouts[2] = {DeviceAccess::texture_set_layout(device),
                                              DeviceAccess::texture_set_layout(device)};
    VkDescriptorSet sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = impl->pool;
    set_info.descriptorSetCount = 2;
    set_info.pSetLayouts = layouts;
    result = vkAllocateDescriptorSets(vk_device, &set_info, sets);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkAllocateDescriptorSets (HDR target)", result));
        return nullptr;
    }
    impl->set = sets[0];
    impl->depth_set = sets[1];
    VkDescriptorImageInfo descriptors[2]{};
    descriptors[0].sampler = impl->sampler;
    descriptors[0].imageView = impl->colour_view;
    descriptors[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptors[1].sampler = impl->depth_sampler;
    descriptors[1].imageView = impl->depth_view;
    descriptors[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[2]{};
    for (int index = 0; index < 2; ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = sets[index];
        writes[index].dstBinding = 0;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[index].pImageInfo = &descriptors[index];
    }
    vkUpdateDescriptorSets(vk_device, 2, writes, 0, nullptr);

    // Cleared to black, so the colour is defined and sampleable before the first render().
    if (!impl->record({0.F, 0.F, 0.F, 1.F}, VK_NULL_HANDLE, VK_NULL_HANDLE, {}, error)) {
        return nullptr;
    }
    return std::unique_ptr<HdrTarget>(new HdrTarget(std::move(impl)));
}

std::uint32_t HdrTarget::width() const
{
    return impl_->width;
}

std::uint32_t HdrTarget::height() const
{
    return impl_->height;
}

bool HdrTarget::render(const ClearColor& clear, const GraphicsPipeline& pipeline, std::span<const MeshDraw> meshes,
                       RhiError& error)
{
    error = {};
    std::vector<detail::PreparedDraw> draws;
    if (!detail::prepare_mesh_draws(pipeline, ColorFormat::Rgba16Float, true, meshes, draws, error)) {
        return false;
    }
    for (const detail::PreparedDraw& draw : draws) {
        if (draw.skinned) {
            return fail(error, RhiFailure::InvalidArgument, "skinned drawing into an HDR target is a later slice");
        }
    }
    return impl_->record(clear, static_cast<VkPipeline>(pipeline.native_handle()),
                         static_cast<VkPipelineLayout>(pipeline.native_layout()), draws, error);
}

void* HdrTarget::native_descriptor_set() const
{
    return static_cast<void*>(impl_->set);
}

void* HdrTarget::native_depth_descriptor_set() const
{
    return static_cast<void*>(impl_->depth_set);
}

}
