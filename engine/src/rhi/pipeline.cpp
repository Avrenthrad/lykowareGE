#include <lykoware/rhi/pipeline.hpp>

#include "common.hpp"
#include "device_access.hpp"
#include "pipeline_access.hpp"

#include <volk.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace lykoware::rhi {
namespace {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

constexpr std::uint32_t spirv_magic = 0x07230203U;

VkShaderStageFlagBits vk_stage(ShaderStage stage) noexcept
{
    return stage == ShaderStage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
}

const char* stage_name(ShaderStage stage) noexcept
{
    return stage == ShaderStage::Vertex ? "vertex" : "fragment";
}

}

VkFormat detail::vk_format(ColorFormat format) noexcept
{
    return format == ColorFormat::Bgra8Unorm ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}

struct Shader::Impl {
    ~Impl()
    {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(DeviceAccess::device(*device), module, nullptr);
        }
    }

    const Device* device = nullptr;
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
};

Shader::Shader(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Shader::~Shader() = default;

std::unique_ptr<Shader> Shader::load(
    const Device& device, std::span<const std::uint32_t> spirv, ShaderStage stage, RhiError& error)
{
    error = {};
    if (spirv.empty() || spirv.front() != spirv_magic) {
        fail(error, RhiFailure::InvalidArgument, "shader data is not SPIR-V (missing magic number)");
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->stage = stage;
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size_bytes();
    info.pCode = spirv.data();
    const VkResult result = vkCreateShaderModule(DeviceAccess::device(device), &info, nullptr, &impl->module);
    if (result != VK_SUCCESS) {
        impl->module = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateShaderModule", result));
        return nullptr;
    }
    return std::unique_ptr<Shader>(new Shader(std::move(impl)));
}

bool read_spirv_file(const std::filesystem::path& path, std::vector<std::uint32_t>& words, RhiError& error)
{
    error = {};
    words.clear();
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return fail(error, RhiFailure::InvalidArgument, "cannot open shader file " + path.string());
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        return fail(error, RhiFailure::InvalidArgument,
                    "shader file " + path.string() + " is not a whole number of 32-bit SPIR-V words");
    }
    std::vector<std::uint32_t> read(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(read.data(), bytes.data(), bytes.size());
    if (read.front() != spirv_magic) {
        return fail(error, RhiFailure::InvalidArgument,
                    "shader file " + path.string() + " is not SPIR-V (missing magic number)");
    }
    words = std::move(read);
    return true;
}

std::unique_ptr<Shader> Shader::load_file(
    const Device& device, const std::filesystem::path& path, ShaderStage stage, RhiError& error)
{
    std::vector<std::uint32_t> words;
    if (!read_spirv_file(path, words, error)) {
        return nullptr;
    }
    auto shader = load(device, words, stage, error);
    if (!shader) {
        error.message += " (" + path.string() + ")";
    }
    return shader;
}

ShaderStage Shader::stage() const
{
    return impl_->stage;
}

void* Shader::native_handle() const
{
    return static_cast<void*>(impl_->module);
}

struct GraphicsPipeline::Impl {
    ~Impl()
    {
        const VkDevice vk_device = DeviceAccess::device(*device);
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk_device, pipeline, nullptr);
        }
        if (layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vk_device, layout, nullptr);
        }
    }

    const Device* device = nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    ColorFormat format = ColorFormat::Rgba8Unorm;
    VertexLayout vertex_layout = VertexLayout::None;
    bool depth = false;
    std::uint32_t push_constant_bytes = 0;
    bool texture = false;
    bool depth_only = false;
    bool shadow = false;
    bool material = false;
};

GraphicsPipeline::GraphicsPipeline(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

GraphicsPipeline::~GraphicsPipeline() = default;

std::unique_ptr<GraphicsPipeline> GraphicsPipeline::create(
    const Device& device, const GraphicsPipelineDesc& desc, RhiError& error)
{
    error = {};
    const bool skinned = desc.vertex_layout == VertexLayout::SkinnedPositionNormalUv;
    if (desc.depth_only
        && (desc.fragment != nullptr || !desc.depth || desc.texture || desc.shadow || skinned
            || desc.vertex_layout == VertexLayout::None)) {
        fail(error, RhiFailure::InvalidArgument,
             "a depth-only pipeline takes no fragment shader, texture or shadow, needs depth and an unskinned mesh layout");
        return nullptr;
    }
    if (desc.shadow && (!desc.texture || skinned)) {
        fail(error, RhiFailure::InvalidArgument, "a shadow-receiving pipeline needs the texture set and no skinning");
        return nullptr;
    }
    for (const auto& [shader, expected] : {std::pair{desc.vertex, ShaderStage::Vertex},
                                           std::pair{desc.fragment, ShaderStage::Fragment}}) {
        if (shader == nullptr && expected == ShaderStage::Fragment && desc.depth_only) {
            continue;
        }
        if (shader == nullptr) {
            fail(error, RhiFailure::InvalidArgument, std::string{"a "} + stage_name(expected) + " shader is required");
            return nullptr;
        }
        if (shader->stage() != expected) {
            fail(error, RhiFailure::InvalidArgument,
                 std::string{"the "} + stage_name(expected) + " slot holds a " + stage_name(shader->stage())
                     + " shader");
            return nullptr;
        }
    }

    if (desc.material && (!desc.texture || skinned || desc.depth_only)) {
        fail(error, RhiFailure::InvalidArgument, "a material pipeline needs the texture set and no skinning");
        return nullptr;
    }
    const std::uint32_t push_limit = desc.material ? push_constant_material_max_bytes : push_constant_max_bytes;
    if (desc.push_constant_bytes > push_limit || desc.push_constant_bytes % 4 != 0) {
        fail(error, RhiFailure::InvalidArgument,
             "push_constant_bytes must be a multiple of 4 from 0 to " + std::to_string(push_limit));
        return nullptr;
    }
    if (desc.push_constant_bytes > DeviceAccess::max_push_constant_bytes(device)) {
        fail(error, RhiFailure::NoSuitableDevice,
             "the device allows only " + std::to_string(DeviceAccess::max_push_constant_bytes(device))
                 + " bytes of push constants");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->format = desc.color_format;
    impl->vertex_layout = desc.vertex_layout;
    impl->depth = desc.depth;
    impl->push_constant_bytes = desc.push_constant_bytes;
    impl->texture = desc.texture;
    impl->depth_only = desc.depth_only;
    impl->shadow = desc.shadow;
    impl->material = desc.material;
    const VkDevice vk_device = DeviceAccess::device(device);

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = desc.push_constant_bytes;
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (desc.push_constant_bytes > 0) {
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
    }
    // A texture is set 0, with the layout every rhi::Texture's descriptor set shares.
    if (skinned && !desc.texture) {
        fail(error, RhiFailure::InvalidArgument, "skinned pipelines require the texture layout at set 0");
        return nullptr;
    }
    const VkDescriptorSetLayout set_layouts[] = {DeviceAccess::texture_set_layout(device),
                                                 desc.shadow || desc.material ? DeviceAccess::shadow_set_layout(device)
                                                                              : DeviceAccess::skin_set_layout(device),
                                                 DeviceAccess::texture_set_layout(device),
                                                 DeviceAccess::texture_set_layout(device)};
    if (desc.texture) {
        layout_info.setLayoutCount = desc.material ? 4U : (skinned || desc.shadow ? 2U : 1U);
        layout_info.pSetLayouts = set_layouts;
    }
    VkResult result = vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &impl->layout);
    if (result != VK_SUCCESS) {
        impl->layout = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreatePipelineLayout", result));
        return nullptr;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    const Shader* shaders[2] = {desc.vertex, desc.fragment};
    const std::uint32_t stage_count = desc.depth_only ? 1U : 2U;
    for (std::uint32_t index = 0; index < stage_count; ++index) {
        stages[index].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[index].stage = vk_stage(shaders[index]->stage());
        stages[index].module = static_cast<VkShaderModule>(shaders[index]->native_handle());
        stages[index].pName = shader_entry_point;
    }

    const bool with_uv = skinned || desc.vertex_layout == VertexLayout::PositionNormalUv;
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = skinned ? skinned_position_normal_uv_stride : (with_uv ? position_normal_uv_stride : position_normal_stride);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[5]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = 12;
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = 24;
    attributes[3] = {3, 0, VK_FORMAT_R16G16B16A16_UINT, 32};
    attributes[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertex_layout != VertexLayout::None) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = skinned ? 5U : (with_uv ? 3U : 2U);
        vertex_input.pVertexAttributeDescriptions = attributes;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull_back_faces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    // Vulkan judges winding in framebuffer space (Y down) as the image is seen.
    // math::perspective flips Y so the image is upright, not mirrored, so a
    // triangle counter-clockwise as seen by the camera stays counter-clockwise.
    // rhi.mesh checks this with culling on.
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = desc.depth_only ? 0U : 1U;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER; // reversed depth: nearer is larger

    const VkFormat format = detail::vk_format(desc.color_format);
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = desc.depth_only ? 0U : 1U;
    rendering.pColorAttachmentFormats = desc.depth_only ? nullptr : &format;
    rendering.depthAttachmentFormat = desc.depth ? detail::depth_format : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDepthStencilState = &depth;
    info.pDynamicState = &dynamic;
    info.layout = impl->layout;
    result = vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &info, nullptr, &impl->pipeline);
    if (result != VK_SUCCESS) {
        impl->pipeline = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreateGraphicsPipelines", result));
        return nullptr;
    }
    return std::unique_ptr<GraphicsPipeline>(new GraphicsPipeline(std::move(impl)));
}

ColorFormat GraphicsPipeline::color_format() const
{
    return impl_->format;
}

VertexLayout GraphicsPipeline::vertex_layout() const
{
    return impl_->vertex_layout;
}

bool GraphicsPipeline::depth() const
{
    return impl_->depth;
}

std::uint32_t GraphicsPipeline::push_constant_bytes() const
{
    return impl_->push_constant_bytes;
}

bool GraphicsPipeline::texture() const
{
    return impl_->texture;
}

bool GraphicsPipeline::depth_only() const
{
    return impl_->depth_only;
}

bool GraphicsPipeline::shadow() const
{
    return impl_->shadow;
}

bool GraphicsPipeline::material() const
{
    return impl_->material;
}

void* GraphicsPipeline::native_handle() const
{
    return static_cast<void*>(impl_->pipeline);
}

void* GraphicsPipeline::native_layout() const
{
    return static_cast<void*>(impl_->layout);
}

}
