#pragma once

// Internal to engine/src/rhi. Not installed and not included by public headers.

#include <lykoware/rhi/instance.hpp>

#include <volk.h>

#include <string>
#include <string_view>

namespace lykoware::rhi::detail {

[[nodiscard]] std::string_view result_name(VkResult result) noexcept;

// "<call> failed: VK_ERROR_... (-n)"
[[nodiscard]] std::string driver_failure(std::string_view call, VkResult result);

// Fills error and returns false, for one-line early exits.
bool fail(RhiError& error, RhiFailure failure, std::string message);

// Whole-image colour subresource: one mip level, one layer.
[[nodiscard]] VkImageSubresourceRange colour_range() noexcept;

// Records one synchronization2 image layout transition on the colour range.
void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

// Records a dynamic-rendering pass on one colour view whose only work is a
// load-op clear of `extent`. The view must be in COLOR_ATTACHMENT_OPTIMAL.
void record_clear_pass(VkCommandBuffer commands, VkImageView view, VkExtent2D extent,
                       const float (&rgba)[4]);

// True while an rhi::Device is alive. Defined in device.cpp; the Instance
// destructor uses it to report a Device that outlived its Instance.
[[nodiscard]] bool device_alive() noexcept;

}
