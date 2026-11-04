#pragma once

// Internal to engine/src/rhi: Vulkan details of the pipeline types.
// Not installed and not included by public headers.

#include <lykoware/rhi/pipeline.hpp>

#include <volk.h>

namespace lykoware::rhi::detail {

[[nodiscard]] VkFormat vk_format(ColorFormat format) noexcept;

// The depth buffer format of render targets and depth pipelines (ADR 0025).
inline constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

}
