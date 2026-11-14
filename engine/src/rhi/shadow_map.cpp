#include <lykoware/rhi/shadow_map.hpp>

#include "common.hpp"
#include "device_access.hpp"
#include "pipeline_access.hpp"

#include <lykoware/rhi/device.hpp>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lykoware::rhi {

namespace {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

// One synchronization2 transition on the depth aspect.
void depth_transition(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                      VkAccessFlags2 dst_access)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
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

void buffer_barrier(VkCommandBuffer commands, VkBuffer buffer, VkPipelineStageFlags2 src_stage,
                    VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

bool finite_block(const ShadowBlock& block)
{
    for (const auto& column : block.clip_to_light.m) {
        for (const float value : column) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return std::isfinite(block.params.x) && std::isfinite(block.params.y) && std::isfinite(block.params.z)
        && std::isfinite(block.params.w);
}

}

struct ShadowMap::Impl {
    ~Impl()
    {
        if (device == nullptr) {
            return;
        }
        const VkDevice vk_device = DeviceAccess::device(*device);
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(vk_device, pool, nullptr); // frees the set
        }
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(vk_device, sampler, nullptr);
        }
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk_device, view, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vmaDestroyImage(DeviceAccess::allocator(*device), image, image_allocation);
        }
        if (uniform != VK_NULL_HANDLE) {
            vmaDestroyBuffer(DeviceAccess::allocator(*device), uniform, uniform_allocation);
        }
    }

    // Clears the map and draws `draws` (none clears only), then stores `block`.
    bool record(VkPipeline pipeline, VkPipelineLayout layout, const std::vector<detail::PreparedDraw>& draws,
                const ShadowBlock& block, RhiError& error)
    {
        return DeviceAccess::submit_and_wait(*device, [&](VkCommandBuffer commands) {
            // Earlier frames may still read the block and the map: order after them.
            buffer_barrier(commands, uniform, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdUpdateBuffer(commands, uniform, 0, sizeof(ShadowBlock), &block);
            buffer_barrier(commands, uniform, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_UNIFORM_READ_BIT);
            depth_transition(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                 | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            const float unused[4] = {};
            if (!draws.empty()) {
                detail::record_mesh_pass(commands, VK_NULL_HANDLE, view, {size, size}, unused, pipeline, layout,
                                         draws.data(), draws.size());
            } else {
                VkRenderingAttachmentInfo depth{};
                depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depth.imageView = view;
                depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depth.clearValue.depthStencil.depth = 0.0F; // reversed depth: nothing casts
                VkRenderingInfo rendering{};
                rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                rendering.renderArea.extent = {size, size};
                rendering.layerCount = 1;
                rendering.pDepthAttachment = &depth;
                vkCmdBeginRendering(commands, &rendering);
                vkCmdEndRendering(commands);
            }
            depth_transition(commands, image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }, error);
    }

    const Device* device = nullptr;
    std::uint32_t size = 0;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation image_allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBuffer uniform = VK_NULL_HANDLE;
    VmaAllocation uniform_allocation = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::size_t casters = 0;
};

ShadowMap::ShadowMap(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

ShadowMap::~ShadowMap() = default;

std::unique_ptr<ShadowMap> ShadowMap::create(const Device& device, std::uint32_t size, RhiError& error)
{
    error = {};
    if (size < shadow_map_min_size || size > shadow_map_max_size || size > DeviceAccess::max_image_dimension(device)) {
        fail(error, RhiFailure::InvalidArgument,
             "shadow map size must be " + std::to_string(shadow_map_min_size) + ".."
                 + std::to_string(shadow_map_max_size) + " and within the device's 2D limit");
        return nullptr;
    }
    VkFormatProperties format_properties{};
    vkGetPhysicalDeviceFormatProperties(static_cast<VkPhysicalDevice>(device.native_physical_device()),
                                        detail::depth_format, &format_properties);
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((format_properties.optimalTilingFeatures & needed) != needed) {
        fail(error, RhiFailure::DriverError, "the device cannot render and sample 32-bit float depth");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->size = size;
    const VkDevice vk_device = DeviceAccess::device(device);
    const VmaAllocator allocator = DeviceAccess::allocator(device);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = detail::depth_format;
    image_info.extent = {size, size, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    VkResult result =
        vmaCreateImage(allocator, &image_info, &allocation_info, &impl->image, &impl->image_allocation, nullptr);
    if (result != VK_SUCCESS) {
        impl->image = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateImage (shadow map)", result));
        return nullptr;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = impl->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = detail::depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    result = vkCreateImageView(vk_device, &view_info, nullptr, &impl->view);
    if (result != VK_SUCCESS) {
        impl->view = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView (shadow map)", result));
        return nullptr;
    }

    // Exact texels: the receiver filters by hand (ADR 0084).
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    result = vkCreateSampler(vk_device, &sampler_info, nullptr, &impl->sampler);
    if (result != VK_SUCCESS) {
        impl->sampler = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateSampler (shadow map)", result));
        return nullptr;
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(ShadowBlock);
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vmaCreateBuffer(allocator, &buffer_info, &allocation_info, &impl->uniform, &impl->uniform_allocation,
                             nullptr);
    if (result != VK_SUCCESS) {
        impl->uniform = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateBuffer (shadow block)", result));
        return nullptr;
    }

    const VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    result = vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &impl->pool);
    if (result != VK_SUCCESS) {
        impl->pool = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateDescriptorPool (shadow map)", result));
        return nullptr;
    }
    const VkDescriptorSetLayout layout = DeviceAccess::shadow_set_layout(device);
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = impl->pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &layout;
    result = vkAllocateDescriptorSets(vk_device, &set_info, &impl->set);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkAllocateDescriptorSets (shadow map)", result));
        return nullptr;
    }
    VkDescriptorImageInfo image_descriptor{};
    image_descriptor.sampler = impl->sampler;
    image_descriptor.imageView = impl->view;
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo buffer_descriptor{};
    buffer_descriptor.buffer = impl->uniform;
    buffer_descriptor.range = sizeof(ShadowBlock);
    VkWriteDescriptorSet writes[2]{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = impl->set;
        write.descriptorCount = 1;
    }
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image_descriptor;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &buffer_descriptor;
    vkUpdateDescriptorSets(vk_device, 2, writes, 0, nullptr);

    // Cleared, with a block that puts every receiver outside the map: fully lit.
    if (!impl->record(VK_NULL_HANDLE, VK_NULL_HANDLE, {}, ShadowBlock{}, error)) {
        return nullptr;
    }
    return std::unique_ptr<ShadowMap>(new ShadowMap(std::move(impl)));
}

std::uint32_t ShadowMap::size() const
{
    return impl_->size;
}

bool ShadowMap::render(const GraphicsPipeline& casters, std::span<const MeshDraw> meshes, const ShadowBlock& block,
                       RhiError& error)
{
    error = {};
    if (!casters.depth_only()) {
        return fail(error, RhiFailure::InvalidArgument, "a shadow map is drawn with a depth-only pipeline");
    }
    if (!finite_block(block) || block.params.x < 0.F || block.params.x > 1.F) {
        return fail(error, RhiFailure::InvalidArgument, "the shadow block must be finite with a bias of 0..1");
    }
    std::vector<detail::PreparedDraw> draws;
    if (!detail::prepare_mesh_draws(casters, casters.color_format(), true, meshes, draws, error)) {
        return false;
    }
    if (!impl_->record(static_cast<VkPipeline>(casters.native_handle()),
                       static_cast<VkPipelineLayout>(casters.native_layout()), draws, block, error)) {
        return false;
    }
    impl_->casters = draws.size();
    return true;
}

std::size_t ShadowMap::last_caster_count() const
{
    return impl_->casters;
}

void* ShadowMap::native_descriptor_set() const
{
    return static_cast<void*>(impl_->set);
}

}
