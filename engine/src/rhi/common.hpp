#pragma once

// Internal to engine/src/rhi. Not installed and not included by public headers.

#include <lykoware/rhi/instance.hpp>

#include <volk.h>

#include <cstddef>
#include <cstdint>
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

// Records a dynamic-rendering pass on one colour view: a load-op clear of
// `extent`, then (when pipeline is not null) binds the graphics pipeline,
// sets a full-extent viewport and scissor and draws vertex_count vertices
// with no vertex buffers. The view must be in COLOR_ATTACHMENT_OPTIMAL.
void record_draw_pass(VkCommandBuffer commands, VkImageView view, VkExtent2D extent, const float (&rgba)[4],
                      VkPipeline pipeline, std::uint32_t vertex_count);

// One indexed mesh draw, already validated, for record_mesh_pass.
struct PreparedDraw {
    VkBuffer vertices = VK_NULL_HANDLE;
    VkBuffer indices = VK_NULL_HANDLE;
    std::uint32_t index_count = 0;
    const void* push_constants = nullptr;
    std::uint32_t push_constant_bytes = 0;
};

// Records a layout transition of a whole depth image, discarding its
// contents, into DEPTH_ATTACHMENT_OPTIMAL.
void depth_barrier(VkCommandBuffer commands, VkImage image);

// Records a dynamic-rendering pass: clear the colour view to rgba and, when
// depth_view is not null, the depth view to 0 (reversed depth: far); then
// bind `pipeline` and draw each mesh with its push constants. Both views
// must already be in their attachment layouts.
void record_mesh_pass(VkCommandBuffer commands, VkImageView colour_view, VkImageView depth_view, VkExtent2D extent,
                      const float (&rgba)[4], VkPipeline pipeline, VkPipelineLayout layout,
                      const PreparedDraw* draws, std::size_t draw_count);

// True while an rhi::Device is alive. Defined in device.cpp; the Instance
// destructor uses it to report a Device that outlived its Instance.
[[nodiscard]] bool device_alive() noexcept;

}
