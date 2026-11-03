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

std::unique_ptr<Shader> Shader::load_file(
    const Device& device, const std::filesystem::path& path, ShaderStage stage, RhiError& error)
{
    error = {};
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        fail(error, RhiFailure::InvalidArgument, "cannot open shader file " + path.string());
        return nullptr;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        fail(error, RhiFailure::InvalidArgument,
             "shader file " + path.string() + " is not a whole number of 32-bit SPIR-V words");
        return nullptr;
    }
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
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
    for (const auto& [shader, expected] : {std::pair{desc.vertex, ShaderStage::Vertex},
                                           std::pair{desc.fragment, ShaderStage::Fragment}}) {
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

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->format = desc.color_format;
    const VkDevice vk_device = DeviceAccess::device(device);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkResult result = vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &impl->layout);
    if (result != VK_SUCCESS) {
        impl->layout = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vkCreatePipelineLayout", result));
        return nullptr;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    const Shader* shaders[2] = {desc.vertex, desc.fragment};
    for (int index = 0; index < 2; ++index) {
        stages[index].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[index].stage = vk_stage(shaders[index]->stage());
        stages[index].module = static_cast<VkShaderModule>(shaders[index]->native_handle());
        stages[index].pName = shader_entry_point;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
    raster.cullMode = VK_CULL_MODE_NONE;
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
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    const VkFormat format = detail::vk_format(desc.color_format);
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
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

void* GraphicsPipeline::native_handle() const
{
    return static_cast<void*>(impl_->pipeline);
}

}
