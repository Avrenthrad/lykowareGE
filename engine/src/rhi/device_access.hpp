#pragma once

// Internal to engine/src/rhi: what other RHI objects need from a Device.
// Not installed and not included by public headers.

#include <lykoware/rhi/device.hpp>

#include "common.hpp"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace lykoware::rhi::detail {

struct DeviceAccess {
    [[nodiscard]] static VkDevice device(const Device& device);
    [[nodiscard]] static VmaAllocator allocator(const Device& device);
    [[nodiscard]] static VkQueue queue(const Device& device);
    // The descriptor set layout of every texture: one combined image sampler at
    // binding 0, read by the fragment stage (ADR 0037). Owned by the Device.
    [[nodiscard]] static VkDescriptorSetLayout texture_set_layout(const Device& device);
    [[nodiscard]] static VkDescriptorSetLayout skin_set_layout(const Device& device);
    [[nodiscard]] static VkDescriptorSetLayout shadow_set_layout(const Device& device);
    [[nodiscard]] static VkDeviceSize uniform_alignment(const Device& device);
    // VkPhysicalDeviceLimits::maxImageDimension2D.
    [[nodiscard]] static std::uint32_t max_image_dimension(const Device& device);

    // Records commands with `record` into the device's one-shot command
    // buffer, submits them to the graphics queue and waits for completion.
    // Single-threaded like the rest of the Device (see device.hpp). On
    // failure, fills error and returns false; the command buffer is reset
    // on the next call. With `gpu_ns`, the recorded work is timed with GPU
    // timestamps (ADR 0039): *gpu_ns becomes its duration, or nullopt when
    // the device has no timestamps.
    [[nodiscard]] static bool submit_and_wait(const Device& device, const std::function<void(VkCommandBuffer)>& record,
                                              RhiError& error, std::optional<std::uint64_t>* gpu_ns = nullptr);
    // The queue's timestamp bits (0: no timestamps) and nanoseconds per tick.
    [[nodiscard]] static std::uint32_t timestamp_bits(const Device& device);
    [[nodiscard]] static float timestamp_period(const Device& device);
};

}
