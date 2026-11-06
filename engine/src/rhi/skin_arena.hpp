#pragma once

#include "common.hpp"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <span>

namespace lykoware::rhi::detail {

// One arena per fenced frame slot (or synchronous offscreen target). Only write/grow after
// that slot's previous work completes. Destroy before its Device. No copy/move.
class SkinArena {
public:
    SkinArena() = default;
    ~SkinArena();
    SkinArena(const SkinArena&) = delete;
    SkinArena& operator=(const SkinArena&) = delete;
    [[nodiscard]] bool prepare(const Device& device, std::span<PreparedDraw> draws, RhiError& error);

private:
    const Device* device_ = nullptr;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    std::size_t capacity_ = 0;
    VkDeviceSize stride_ = 0;
};

}
