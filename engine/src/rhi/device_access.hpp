#pragma once

// Internal to engine/src/rhi: what other RHI objects need from a Device.
// Not installed and not included by public headers.

#include <lykoware/rhi/device.hpp>

#include "common.hpp"

#include <vk_mem_alloc.h>

#include <functional>

namespace lykoware::rhi::detail {

struct DeviceAccess {
    [[nodiscard]] static VkDevice device(const Device& device);
    [[nodiscard]] static VmaAllocator allocator(const Device& device);
    [[nodiscard]] static VkQueue queue(const Device& device);

    // Records commands with `record` into the device's one-shot command
    // buffer, submits them to the graphics queue and waits for completion.
    // Single-threaded like the rest of the Device (see device.hpp). On
    // failure, fills error and returns false; the command buffer is reset
    // on the next call.
    [[nodiscard]] static bool submit_and_wait(
        const Device& device, const std::function<void(VkCommandBuffer)>& record, RhiError& error);
};

}
