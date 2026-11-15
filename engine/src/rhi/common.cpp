#include "common.hpp"

#include <lykoware/rhi/shadow_map.hpp>
#include <lykoware/rhi/hdr_target.hpp>
#include <lykoware/rhi/texture.hpp>

#include <cmath>

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

bool prepare_mesh_draws(const GraphicsPipeline& pass_pipeline, ColorFormat target_format, bool target_has_depth,
                        std::span<const MeshDraw> meshes, std::vector<PreparedDraw>& draws, RhiError& error)
{
    draws.clear();
    if (pass_pipeline.color_format() != target_format) {
        return fail(error, RhiFailure::InvalidArgument, "the pipeline targets a different colour format");
    }
    if (pass_pipeline.vertex_layout() == VertexLayout::None) {
        return fail(error, RhiFailure::InvalidArgument,
                    "drawing meshes needs a pipeline with a mesh vertex layout (PositionNormal or PositionNormalUv)");
    }
    if (pass_pipeline.depth() && !target_has_depth) {
        return fail(error, RhiFailure::InvalidArgument, "the pipeline uses depth but the target has none");
    }
    draws.reserve(meshes.size());
    for (std::size_t index = 0; index < meshes.size(); ++index) {
        const MeshDraw& mesh = meshes[index];
        const std::string which = "mesh " + std::to_string(index) + ": ";
        // A draw's own pipeline (ADR 0086) must fit the pass the first pipeline began.
        const GraphicsPipeline& pipeline = mesh.pipeline != nullptr ? *mesh.pipeline : pass_pipeline;
        if (mesh.pipeline != nullptr
            && (pipeline.color_format() != target_format || pipeline.depth() != pass_pipeline.depth()
                || pipeline.depth_only() != pass_pipeline.depth_only())) {
            return fail(error, RhiFailure::InvalidArgument,
                        which + "its pipeline must match the pass's colour format, depth and depth-only use");
        }
        if (pipeline.vertex_layout() == VertexLayout::None) {
            return fail(error, RhiFailure::InvalidArgument, which + "its pipeline has no mesh vertex layout");
        }
        const bool skinned = pipeline.vertex_layout() == VertexLayout::SkinnedPositionNormalUv;
        const std::size_t stride = skinned ? skinned_position_normal_uv_stride
            : (pipeline.vertex_layout() == VertexLayout::PositionNormalUv ? position_normal_uv_stride
                                                                           : position_normal_stride);
        if (mesh.vertices == nullptr || mesh.vertices->usage() != BufferUsage::Vertex) {
            return fail(error, RhiFailure::InvalidArgument, which + "needs a vertex buffer");
        }
        if (mesh.indices == nullptr || mesh.indices->usage() != BufferUsage::Index) {
            return fail(error, RhiFailure::InvalidArgument, which + "needs an index buffer");
        }
        const std::size_t available = mesh.indices->size_bytes() / sizeof(std::uint32_t);
        const std::size_t first = mesh.first_index;
        const std::size_t count = mesh.index_count == 0 ? (first < available ? available - first : 0) : mesh.index_count;
        if (count == 0 || count % 3 != 0 || first > available || count > available - first) {
            return fail(error, RhiFailure::InvalidArgument,
                        which + "the indices drawn must be a non-zero multiple of 3 within the index buffer");
        }
        const std::size_t vertex_count = mesh.vertices->size_bytes() / stride;
        if (mesh.indices->max_index() >= vertex_count) {
            return fail(error, RhiFailure::InvalidArgument, which + "an index reaches past the last vertex");
        }
        if (mesh.push_constants.size() != pipeline.push_constant_bytes()) {
            return fail(error, RhiFailure::InvalidArgument,
                        which + "push constants must be exactly " + std::to_string(pipeline.push_constant_bytes())
                            + " bytes for this pipeline");
        }
        if (mesh.texture != nullptr && mesh.source != nullptr) {
            return fail(error, RhiFailure::InvalidArgument, which + "the mesh names both a texture and an HDR source");
        }
        const bool names_texture = mesh.texture != nullptr || mesh.source != nullptr;
        if (pipeline.texture() && !names_texture) {
            return fail(error, RhiFailure::InvalidArgument, which + "the pipeline reads a texture but the mesh names none");
        }
        if (!pipeline.texture() && names_texture) {
            return fail(error, RhiFailure::InvalidArgument, which + "the mesh names a texture but the pipeline reads none");
        }
        if (pipeline.shadow() != (mesh.shadow != nullptr)) {
            return fail(error, RhiFailure::InvalidArgument,
                        which + (pipeline.shadow() ? "the pipeline receives a shadow but the mesh names no shadow map"
                                                   : "the mesh names a shadow map but the pipeline receives none"));
        }
        const bool names_material = mesh.normal_map != nullptr || mesh.metal_roughness != nullptr;
        if (pipeline.material() && (mesh.normal_map == nullptr || mesh.metal_roughness == nullptr)) {
            return fail(error, RhiFailure::InvalidArgument,
                        which + "the pipeline shades a material but the mesh names no normal map or metal-roughness texture");
        }
        if (!pipeline.material() && names_material) {
            return fail(error, RhiFailure::InvalidArgument, which + "the mesh names material textures but the pipeline reads none");
        }
        if ((!skinned && !mesh.skin_matrices.empty()) || mesh.skin_matrices.size() > skin_palette_max_joints) {
            return fail(error, RhiFailure::InvalidArgument, which + "invalid skin palette for this pipeline");
        }
        for (const auto& matrix : mesh.skin_matrices) {
            for (const auto& column : matrix.m) {
                for (float value : column) {
                    if (!std::isfinite(value)) {
                        return fail(error, RhiFailure::InvalidArgument, which + "skin matrices must be finite");
                    }
                }
            }
            if (matrix.m[0][3] != 0.F || matrix.m[1][3] != 0.F || matrix.m[2][3] != 0.F || matrix.m[3][3] != 1.F) {
                return fail(error, RhiFailure::InvalidArgument, which + "skin matrices must be affine");
            }
        }
        draws.push_back({static_cast<VkBuffer>(mesh.vertices->native_handle()),
                         static_cast<VkBuffer>(mesh.indices->native_handle()), static_cast<std::uint32_t>(count),
                         mesh.push_constants.data(), static_cast<std::uint32_t>(mesh.push_constants.size()),
                         mesh.texture != nullptr ? static_cast<VkDescriptorSet>(mesh.texture->native_descriptor_set())
                         : mesh.source != nullptr ? static_cast<VkDescriptorSet>(mesh.source->native_descriptor_set())
                                                  : VK_NULL_HANDLE,
                         mesh.first_index, skinned, mesh.skin_matrices});
        if (mesh.shadow != nullptr) {
            draws.back().shadow = static_cast<VkDescriptorSet>(mesh.shadow->native_descriptor_set());
        }
        if (names_material) {
            draws.back().material[0] = static_cast<VkDescriptorSet>(mesh.normal_map->native_descriptor_set());
            draws.back().material[1] = static_cast<VkDescriptorSet>(mesh.metal_roughness->native_descriptor_set());
        }
        if (mesh.pipeline != nullptr) {
            draws.back().pipeline = static_cast<VkPipeline>(mesh.pipeline->native_handle());
            draws.back().layout = static_cast<VkPipelineLayout>(mesh.pipeline->native_layout());
        }
    }
    return true;
}

void depth_barrier(VkCommandBuffer commands, VkImage image)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void record_mesh_pass(VkCommandBuffer commands, VkImageView colour_view, VkImageView depth_view, VkExtent2D extent,
                      const float (&rgba)[4], VkPipeline pipeline, VkPipelineLayout pass_layout,
                      const PreparedDraw* draws, std::size_t draw_count)
{
    VkPipelineLayout layout = pass_layout;
    VkRenderingAttachmentInfo colour{};
    colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour.imageView = colour_view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    for (int channel = 0; channel < 4; ++channel) {
        colour.clearValue.color.float32[channel] = rgba[channel];
    }
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // A depth-only pass (a shadow map, ADR 0084) keeps its depth; a colour pass does not.
    depth.storeOp = colour_view == VK_NULL_HANDLE ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth = 0.0F; // reversed depth: 0 is infinitely far
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = colour_view != VK_NULL_HANDLE ? 1U : 0U;
    rendering.pColorAttachments = colour_view != VK_NULL_HANDLE ? &colour : nullptr;
    rendering.pDepthAttachment = depth_view != VK_NULL_HANDLE ? &depth : nullptr;
    vkCmdBeginRendering(commands, &rendering);
    if (pipeline != VK_NULL_HANDLE) { // null: a clear with no draws (an HdrTarget's first clear)
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(commands, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commands, 0, 1, &scissor);
    VkDescriptorSet bound = VK_NULL_HANDLE;
    VkDescriptorSet bound_shadow = VK_NULL_HANDLE;
    VkDescriptorSet bound_material[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipeline bound_pipeline = pipeline;
    VkBuffer bound_vertices = VK_NULL_HANDLE;
    VkBuffer bound_indices = VK_NULL_HANDLE;
    for (std::size_t index = 0; index < draw_count; ++index) {
        const PreparedDraw& draw = draws[index];
        // A draw may name its own pipeline (ADR 0086); switching forgets the bound sets, since
        // the new layout may not be compatible with the old one.
        const VkPipeline wanted = draw.pipeline != VK_NULL_HANDLE ? draw.pipeline : pipeline;
        layout = draw.pipeline != VK_NULL_HANDLE ? draw.layout : pass_layout;
        if (wanted != bound_pipeline) {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            bound_pipeline = wanted;
            bound = VK_NULL_HANDLE;
            bound_shadow = VK_NULL_HANDLE;
            bound_material[0] = VK_NULL_HANDLE;
            bound_material[1] = VK_NULL_HANDLE;
        }
        if (draw.texture != VK_NULL_HANDLE && draw.texture != bound) {
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &draw.texture, 0, nullptr);
            bound = draw.texture;
        }
        if (draw.shadow != VK_NULL_HANDLE && draw.shadow != bound_shadow) {
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &draw.shadow, 0, nullptr);
            bound_shadow = draw.shadow;
        }
        if (draw.material[0] != VK_NULL_HANDLE
            && (draw.material[0] != bound_material[0] || draw.material[1] != bound_material[1])) {
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 2, draw.material, 0, nullptr);
            bound_material[0] = draw.material[0];
            bound_material[1] = draw.material[1];
        }
        if (draw.skin != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &draw.skin,
                                    1, &draw.skin_offset);
        }
        if (draw.push_constant_bytes > 0) {
            vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               draw.push_constant_bytes, draw.push_constants);
        }
        // Vertex and index bindings survive pipeline changes; consecutive draws of one mesh
        // (a batch, ADR 0088) bind them once.
        if (draw.vertices != bound_vertices) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commands, 0, 1, &draw.vertices, &offset);
            bound_vertices = draw.vertices;
        }
        if (draw.indices != bound_indices) {
            vkCmdBindIndexBuffer(commands, draw.indices, 0, VK_INDEX_TYPE_UINT32);
            bound_indices = draw.indices;
        }
        vkCmdDrawIndexed(commands, draw.index_count, 1, draw.first_index, 0, 0);
    }
    vkCmdEndRendering(commands);
}

void write_timestamp(VkCommandBuffer commands, VkQueryPool pool, std::uint32_t query, bool end)
{
    const VkPipelineStageFlags2 stage = end ? VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    vkCmdWriteTimestamp2(commands, stage, pool, query);
}

std::optional<std::uint64_t> read_gpu_interval(VkDevice device, VkQueryPool pool, std::uint32_t first,
                                               std::uint32_t valid_bits, float period_ns)
{
    // Each query reports its value and then whether it is available.
    std::uint64_t raw[4] = {};
    const VkResult result = vkGetQueryPoolResults(device, pool, first, 2, sizeof(raw), raw, 2 * sizeof(std::uint64_t),
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((result != VK_SUCCESS && result != VK_NOT_READY) || raw[1] == 0 || raw[3] == 0) {
        return std::nullopt;
    }
    const std::uint64_t mask = valid_bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << valid_bits) - 1;
    const std::uint64_t ticks = (raw[2] - raw[0]) & mask;
    return static_cast<std::uint64_t>(std::llround(static_cast<double>(ticks) * static_cast<double>(period_ns)));
}

bool fail(RhiError& error, RhiFailure failure, std::string message)
{
    error.failure = failure;
    error.message = std::move(message);
    return false;
}

}
