#include "common.hpp"

#include <utility>

namespace lykoware::rhi::detail {

std::string_view result_name(VkResult result) noexcept
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    default: return "VkResult error";
    }
}

std::string driver_failure(std::string_view call, VkResult result)
{
    std::string message{call};
    message += " failed: ";
    message += result_name(result);
    message += " (";
    message += std::to_string(static_cast<int>(result));
    message += ')';
    return message;
}

VkImageSubresourceRange colour_range() noexcept
{
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    return range;
}

void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
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
    barrier.subresourceRange = colour_range();
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void record_draw_pass(VkCommandBuffer commands, VkImageView view, VkExtent2D extent, const float (&rgba)[4],
                      VkPipeline pipeline, std::uint32_t vertex_count)
{
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    for (int channel = 0; channel < 4; ++channel) {
        attachment.clearValue.color.float32[channel] = rgba[channel];
    }
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(commands, &rendering);
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(commands, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = extent;
        vkCmdSetScissor(commands, 0, 1, &scissor);
        vkCmdDraw(commands, vertex_count, 1, 0, 0);
    }
    vkCmdEndRendering(commands);
}

bool fail(RhiError& error, RhiFailure failure, std::string message)
{
    error.failure = failure;
    error.message = std::move(message);
    return false;
}

}
