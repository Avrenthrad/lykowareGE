#include "skin_arena.hpp"

#include "device_access.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace lykoware::rhi::detail {

namespace {
constexpr VkDeviceSize palette_bytes = sizeof(math::Mat4) * skin_palette_max_joints;
static_assert(palette_bytes == 16384);
}

SkinArena::~SkinArena()
{
    if (device_ != nullptr) {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(DeviceAccess::device(*device_), pool_, nullptr);
        }
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(DeviceAccess::allocator(*device_), buffer_, allocation_);
        }
    }
}

bool SkinArena::prepare(const Device& device, std::span<PreparedDraw> draws, RhiError& error)
{
    std::size_t count = 0;
    bool needed = false;
    for (const auto& draw : draws) {
        needed = needed || draw.skinned;
        count += draw.skinned && !draw.skin_matrices.empty() ? 1 : 0;
    }
    if (!needed) {
        return true;
    }
    if (count > skin_palette_max_draws) {
        return fail(error, RhiFailure::InvalidArgument, "too many skin palettes in one frame (maximum 4096)");
    }
    device_ = &device;
    const VkDevice vk_device = DeviceAccess::device(device);
    const VmaAllocator allocator = DeviceAccess::allocator(device);
    const VkDeviceSize alignment = std::max<VkDeviceSize>(1, DeviceAccess::uniform_alignment(device));
    stride_ = ((palette_bytes + alignment - 1) / alignment) * alignment;
    const std::size_t wanted = count + 1; // slot zero is an identity palette, shared by static meshes
    if (wanted > capacity_) {
        std::size_t capacity = 1;
        while (capacity < wanted) { capacity *= 2; }
        capacity = std::min(capacity, skin_palette_max_draws + 1);
        const VkDeviceSize size = stride_ * capacity;
        if (size > buffer_max_bytes || size > std::numeric_limits<std::uint32_t>::max()) {
            return fail(error, RhiFailure::InvalidArgument, "skin palette arena exceeds the buffer/dynamic-offset limit");
        }
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo memory{};
        memory.usage = VMA_MEMORY_USAGE_AUTO;
        memory.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        VkBuffer next = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkResult result = vmaCreateBuffer(allocator, &info, &memory, &next, &allocation, nullptr);
        if (result != VK_SUCCESS) {
            return fail(error, RhiFailure::DriverError, driver_failure("skin buffer allocation", result));
        }
        if (pool_ == VK_NULL_HANDLE) {
            const VkDescriptorPoolSize sizes{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1};
            VkDescriptorPoolCreateInfo pool{};
            pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool.maxSets = 1;
            pool.poolSizeCount = 1;
            pool.pPoolSizes = &sizes;
            result = vkCreateDescriptorPool(vk_device, &pool, nullptr, &pool_);
            if (result == VK_SUCCESS) {
                const VkDescriptorSetLayout layout = DeviceAccess::skin_set_layout(device);
                VkDescriptorSetAllocateInfo sets{};
                sets.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                sets.descriptorPool = pool_;
                sets.descriptorSetCount = 1;
                sets.pSetLayouts = &layout;
                result = vkAllocateDescriptorSets(vk_device, &sets, &set_);
                if (result != VK_SUCCESS) {
                    vkDestroyDescriptorPool(vk_device, pool_, nullptr);
                    pool_ = VK_NULL_HANDLE;
                }
            }
            if (result != VK_SUCCESS) {
                vmaDestroyBuffer(allocator, next, allocation);
                return fail(error, RhiFailure::DriverError, driver_failure("skin descriptor allocation", result));
            }
        }
        std::array<math::Mat4, skin_palette_max_joints> identity{};
        result = vmaCopyMemoryToAllocation(allocator, identity.data(), allocation, 0, palette_bytes);
        if (result != VK_SUCCESS) {
            vmaDestroyBuffer(allocator, next, allocation);
            return fail(error, RhiFailure::DriverError, driver_failure("identity palette upload", result));
        }
        const VkDescriptorBufferInfo descriptor{next, 0, palette_bytes};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &descriptor;
        vkUpdateDescriptorSets(vk_device, 1, &write, 0, nullptr);
        if (buffer_ != VK_NULL_HANDLE) { vmaDestroyBuffer(allocator, buffer_, allocation_); }
        buffer_ = next;
        allocation_ = allocation;
        capacity_ = capacity;
    }
    std::size_t slot = 1;
    for (auto& draw : draws) {
        if (!draw.skinned) { continue; }
        draw.skin = set_;
        draw.skin_offset = 0;
        if (!draw.skin_matrices.empty()) {
            std::array<math::Mat4, skin_palette_max_joints> palette{};
            std::copy(draw.skin_matrices.begin(), draw.skin_matrices.end(), palette.begin());
            const VkDeviceSize offset = stride_ * slot++;
            const auto result = vmaCopyMemoryToAllocation(allocator, palette.data(), allocation_, offset, palette_bytes);
            if (result != VK_SUCCESS) {
                return fail(error, RhiFailure::DriverError, driver_failure("pose palette upload", result));
            }
            draw.skin_offset = static_cast<std::uint32_t>(offset);
        }
    }
    return true;
}

}
