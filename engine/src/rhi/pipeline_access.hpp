#pragma once

// Internal to engine/src/rhi: Vulkan details of the pipeline types.
// Not installed and not included by public headers.

#include <lykoware/rhi/pipeline.hpp>

#include <volk.h>

namespace lykoware::rhi::detail {

[[nodiscard]] VkFormat vk_format(ColorFormat format) noexcept;

}
