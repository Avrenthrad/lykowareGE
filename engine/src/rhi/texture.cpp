#include <lykoware/rhi/texture.hpp>

#include "common.hpp"
#include "device_access.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace lykoware::rhi {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

namespace {

// sRGB <-> linear light for the mip average. 8-bit sRGB to linear is a table; linear back to
// 8-bit sRGB rounds the standard curve to nearest.
const std::array<float, 256>& srgb_to_linear_table()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double c = static_cast<double>(i) / 255.0;
            values[i] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return values;
    }();
    return table;
}

std::uint8_t linear_to_srgb(float linear)
{
    const double c = std::clamp(static_cast<double>(linear), 0.0, 1.0);
    const double encoded = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

// The next mip level: each texel averages up to 2x2 texels of the level above (at an odd edge,
// the last row or column is used once more). Colour channels of an sRGB texture are averaged
// in linear light; alpha, and every channel of a Unorm texture, as stored.
std::vector<std::uint8_t> downsample(const std::uint8_t* above, std::uint32_t width, std::uint32_t height, bool srgb)
{
    const std::uint32_t w = std::max(1U, width / 2);
    const std::uint32_t h = std::max(1U, height / 2);
    const auto& to_linear = srgb_to_linear_table();
    std::vector<std::uint8_t> level(static_cast<std::size_t>(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint32_t y0 = std::min(2 * y, height - 1);
        const std::uint32_t y1 = std::min(2 * y + 1, height - 1);
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint32_t x0 = std::min(2 * x, width - 1);
            const std::uint32_t x1 = std::min(2 * x + 1, width - 1);
            const std::uint8_t* corners[4] = {
                above + (static_cast<std::size_t>(y0) * width + x0) * 4, above + (static_cast<std::size_t>(y0) * width + x1) * 4,
                above + (static_cast<std::size_t>(y1) * width + x0) * 4, above + (static_cast<std::size_t>(y1) * width + x1) * 4};
            std::uint8_t* out = level.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                if (srgb && channel < 3) {
                    float sum = 0.F;
                    for (const std::uint8_t* corner : corners) {
                        sum += to_linear[corner[channel]];
                    }
                    out[channel] = linear_to_srgb(sum * 0.25F);
                } else {
                    unsigned sum = 0;
                    for (const std::uint8_t* corner : corners) {
                        sum += corner[channel];
                    }
                    out[channel] = static_cast<std::uint8_t>((sum + 2) / 4); // round half up
                }
            }
        }
    }
    return level;
}

VkSamplerAddressMode address_mode(TextureWrap wrap)
{
    switch (wrap) {
    case TextureWrap::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureWrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureWrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkImageSubresourceRange all_levels(std::uint32_t levels)
{
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = levels;
    range.layerCount = 1;
    return range;
}

void barrier(VkCommandBuffer commands, VkImage image, std::uint32_t levels, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
             VkAccessFlags2 dst_access)
{
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = all_levels(levels);
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(commands, &dependency);
}

}

std::uint32_t full_mip_levels(std::uint32_t width, std::uint32_t height) noexcept
{
    std::uint32_t levels = 1;
    for (std::uint32_t side = std::max(width, height); side > 1; side /= 2) {
        ++levels;
    }
    return levels;
}

struct Texture::Impl {
    ~Impl()
    {
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
            vmaDestroyImage(DeviceAccess::allocator(*device), image, allocation);
        }
    }

    const Device* device = nullptr;
    TextureDesc desc;
    std::uint32_t levels = 1;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

Texture::Texture(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Texture::~Texture() = default;

std::unique_ptr<Texture> Texture::create(const Device& device, const TextureDesc& desc, std::span<const std::byte> rgba,
                                         RhiError& error)
{
    error = {};
    const std::uint32_t max_side = DeviceAccess::max_image_dimension(device);
    if (desc.width == 0 || desc.height == 0 || desc.width > max_side || desc.height > max_side) {
        fail(error, RhiFailure::InvalidArgument, "a texture's sides must be 1.." + std::to_string(max_side) + " texels");
        return nullptr;
    }
    const std::size_t base_bytes = static_cast<std::size_t>(desc.width) * desc.height * 4;
    if (base_bytes > buffer_max_bytes) {
        fail(error, RhiFailure::InvalidArgument, "a texture may hold at most " + std::to_string(buffer_max_bytes) + " bytes");
        return nullptr;
    }
    if (rgba.size() != base_bytes) {
        fail(error, RhiFailure::InvalidArgument, "texture data must be width * height * 4 bytes");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->desc = desc;
    impl->levels = desc.mipmaps ? full_mip_levels(desc.width, desc.height) : 1;
    const bool srgb = desc.format == TextureFormat::Rgba8Srgb;
    const VkFormat vk_format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Every level, one after another, in a staging buffer.
    std::vector<std::uint8_t> staged(rgba.size());
    std::memcpy(staged.data(), rgba.data(), rgba.size());
    std::vector<VkBufferImageCopy> regions;
    std::uint32_t level_width = desc.width;
    std::uint32_t level_height = desc.height;
    std::size_t offset = 0;
    for (std::uint32_t level = 0; level < impl->levels; ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {level_width, level_height, 1};
        regions.push_back(region);
        if (level + 1 < impl->levels) {
            const std::vector<std::uint8_t> next = downsample(staged.data() + offset, level_width, level_height, srgb);
            offset += static_cast<std::size_t>(level_width) * level_height * 4;
            staged.insert(staged.end(), next.begin(), next.end());
            level_width = std::max(1U, level_width / 2);
            level_height = std::max(1U, level_height / 2);
        }
    }

    const VmaAllocator allocator = DeviceAccess::allocator(device);
    const VkDevice vk_device = DeviceAccess::device(device);
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = staged.size();
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo staging_allocation{};
    staging_allocation.usage = VMA_MEMORY_USAGE_AUTO;
    staging_allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_memory = VK_NULL_HANDLE;
    VkResult result = vmaCreateBuffer(allocator, &buffer_info, &staging_allocation, &staging, &staging_memory, nullptr);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateBuffer", result));
        return nullptr;
    }
    struct StagingGuard {
        VmaAllocator allocator;
        VkBuffer buffer;
        VmaAllocation memory;
        ~StagingGuard() { vmaDestroyBuffer(allocator, buffer, memory); }
    } staging_guard{allocator, staging, staging_memory};
    result = vmaCopyMemoryToAllocation(allocator, staged.data(), staging_memory, 0, staged.size());
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vmaCopyMemoryToAllocation", result));
        return nullptr;
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = vk_format;
    image_info.extent = {desc.width, desc.height, 1};
    image_info.mipLevels = impl->levels;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo image_allocation{};
    image_allocation.usage = VMA_MEMORY_USAGE_AUTO;
    result = vmaCreateImage(allocator, &image_info, &image_allocation, &impl->image, &impl->allocation, nullptr);
    if (result != VK_SUCCESS) {
        impl->image = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateImage", result));
        return nullptr;
    }

    const VkImage vk_image = impl->image;
    const std::uint32_t level_count = impl->levels;
    if (!DeviceAccess::submit_and_wait(
            device,
            [&](VkCommandBuffer commands) {
                barrier(commands, vk_image, level_count, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
                vkCmdCopyBufferToImage(commands, staging, vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       static_cast<std::uint32_t>(regions.size()), regions.data());
                barrier(commands, vk_image, level_count, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            },
            error)) {
        return nullptr;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = impl->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = vk_format;
    view_info.subresourceRange = all_levels(impl->levels);
    result = vkCreateImageView(vk_device, &view_info, nullptr, &impl->view);
    if (result != VK_SUCCESS) {
        impl->view = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateImageView", result));
        return nullptr;
    }

    const bool linear = desc.filter == TextureFilter::Linear;
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_info.minFilter = sampler_info.magFilter;
    sampler_info.mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = address_mode(desc.wrap);
    sampler_info.addressModeV = sampler_info.addressModeU;
    sampler_info.addressModeW = sampler_info.addressModeU;
    sampler_info.maxLod = static_cast<float>(impl->levels);
    result = vkCreateSampler(vk_device, &sampler_info, nullptr, &impl->sampler);
    if (result != VK_SUCCESS) {
        impl->sampler = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateSampler", result));
        return nullptr;
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    result = vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &impl->pool);
    if (result != VK_SUCCESS) {
        impl->pool = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateDescriptorPool", result));
        return nullptr;
    }
    const VkDescriptorSetLayout layout = DeviceAccess::texture_set_layout(device);
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = impl->pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &layout;
    result = vkAllocateDescriptorSets(vk_device, &set_info, &impl->set);
    if (result != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vkAllocateDescriptorSets", result));
        return nullptr;
    }
    VkDescriptorImageInfo image_descriptor{};
    image_descriptor.sampler = impl->sampler;
    image_descriptor.imageView = impl->view;
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = impl->set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_descriptor;
    vkUpdateDescriptorSets(vk_device, 1, &write, 0, nullptr);
    return std::unique_ptr<Texture>(new Texture(std::move(impl)));
}

std::uint32_t Texture::width() const
{
    return impl_->desc.width;
}

std::uint32_t Texture::height() const
{
    return impl_->desc.height;
}

std::uint32_t Texture::mip_levels() const
{
    return impl_->levels;
}

TextureFormat Texture::format() const
{
    return impl_->desc.format;
}

void* Texture::native_descriptor_set() const
{
    return static_cast<void*>(impl_->set);
}

}
