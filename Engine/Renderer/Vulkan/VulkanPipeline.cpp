#include "Engine/Platform/VulkanPlatform.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace LuxEngine::Vk {
namespace {
constexpr uint32 SpirvMagic = 0x07230203u;
constexpr uint32 SetsPerPool = 256;
constexpr size_t MaxDescriptorPools = 4096;
constexpr std::streamoff MaxShaderBytes = 16 << 20;

bool ReadSpirv(const std::filesystem::path& path, std::vector<uint32>& code, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Shader file missing: " + path.generic_string();
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size <= 0 || size % 4 != 0 || size > MaxShaderBytes) {
        error = "Shader file has an invalid SPIR-V size: " + path.generic_string();
        return false;
    }
    code.assign(static_cast<size_t>(size / 4), 0);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(code.data()), size)) {
        error = "Shader file could not be read: " + path.generic_string();
        return false;
    }
    if (code.size() < 5 || code[0] != SpirvMagic) {
        error = "Shader file is not SPIR-V: " + path.generic_string();
        return false;
    }
    return true;
}
} // namespace

bool DeviceState::LoadShaders() {
    std::string directory = config.shaderDirectory;
    if (directory.empty())
        directory = Platform::ExecutableDirectory() + "shaders/";
    const std::filesystem::path root(
        std::u8string(reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
    std::string error;
    if (!ReadSpirv(root / "Basic.vert.spv", vertexCode, error) ||
        !ReadSpirv(root / "Basic.frag.spv", fragmentCode, error))
        return Fail(GpuStatus::Error, error);
    return true;
}

bool DeviceState::CreateRenderPass(VkFormat colorFormat) {
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        for (VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical, candidate, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = candidate;
                break;
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED)
            return Fail(VK_ERROR_FORMAT_NOT_SUPPORTED, "No depth attachment format");
    }
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create.attachmentCount = 2;
    create.pAttachments = attachments;
    create.subpassCount = 1;
    create.pSubpasses = &subpass;
    create.dependencyCount = 1;
    create.pDependencies = &dependency;
    if (const VkResult result = vkCreateRenderPass(device, &create, nullptr, &renderPass);
        result != VK_SUCCESS) {
        renderPass = VK_NULL_HANDLE;
        return Fail(result, "vkCreateRenderPass");
    }
    renderPassFormat = colorFormat;
    return true;
}

bool DeviceState::CreatePipelines() {
    if (!sampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0f;
        if (const VkResult result = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
            result != VK_SUCCESS) {
            sampler = VK_NULL_HANDLE;
            return Fail(result, "vkCreateSampler");
        }
    }
    if (!setLayout) {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = &sampler;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        if (const VkResult result =
                vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout);
            result != VK_SUCCESS) {
            setLayout = VK_NULL_HANDLE;
            return Fail(result, "vkCreateDescriptorSetLayout");
        }
    }
    if (!pipelineLayout) {
        const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4::elements)};
        const VkDescriptorSetLayout setLayouts[2] = {setLayout, frameSetLayout};
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &range;
        if (const VkResult result =
                vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
            result != VK_SUCCESS) {
            pipelineLayout = VK_NULL_HANDLE;
            return Fail(result, "vkCreatePipelineLayout");
        }
    }
    if (!pipelineCache) {
        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) != VK_SUCCESS)
            pipelineCache = VK_NULL_HANDLE;
    }

    VkShaderModule modules[2]{};
    const std::vector<uint32>* code[2] = {&vertexCode, &fragmentCode};
    for (int stage = 0; stage < 2; ++stage) {
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = code[stage]->size() * sizeof(uint32);
        moduleInfo.pCode = code[stage]->data();
        const VkResult result =
            ConsumeFault(Fault::ShaderModule)
                ? VK_ERROR_INVALID_SHADER_NV
                : vkCreateShaderModule(device, &moduleInfo, nullptr, &modules[stage]);
        if (result != VK_SUCCESS) {
            if (modules[0])
                vkDestroyShaderModule(device, modules[0], nullptr);
            return Fail(result, "vkCreateShaderModule");
        }
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int stage = 0; stage < 2; ++stage) {
        stages[stage].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stage].stage =
            stage == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage].module = modules[stage];
        stages[stage].pName = "main";
    }
    const VkVertexInputBindingDescription binding{0, sizeof(RenderVertex),
                                                  VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderVertex, color)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(RenderVertex, uv)}};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;
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
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState{};
    depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create.stageCount = 2;
    create.pStages = stages;
    create.pVertexInputState = &vertexInput;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &multisample;
    create.pDepthStencilState = &depthState;
    create.pColorBlendState = &blendState;
    create.pDynamicState = &dynamic;
    create.layout = pipelineLayout;
    create.renderPass = renderPass;

    VkResult result =
        ConsumeFault(Fault::PipelineCreation)
            ? VK_ERROR_OUT_OF_DEVICE_MEMORY
            : vkCreateGraphicsPipelines(device, pipelineCache, 1, &create, nullptr, &pipeline2D);
    if (result == VK_SUCCESS) {
        blend.blendEnable = VK_FALSE;
        depthState.depthTestEnable = VK_TRUE;
        depthState.depthWriteEnable = VK_TRUE;
        result = ConsumeFault(Fault::PipelineCreation)
                     ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                     : vkCreateGraphicsPipelines(device, pipelineCache, 1, &create, nullptr,
                                                 &pipeline3D);
    }
    if (result == VK_SUCCESS) {
        const VkBool32 lit = VK_TRUE;
        const VkSpecializationMapEntry entry{0, 0, sizeof(lit)};
        const VkSpecializationInfo specialization{1, &entry, sizeof(lit), &lit};
        stages[1].pSpecializationInfo = &specialization;
        result = ConsumeFault(Fault::PipelineCreation)
                     ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                     : vkCreateGraphicsPipelines(device, pipelineCache, 1, &create, nullptr,
                                                 &pipelineMesh);
    }
    vkDestroyShaderModule(device, modules[0], nullptr);
    vkDestroyShaderModule(device, modules[1], nullptr);
    if (result != VK_SUCCESS) {
        for (VkPipeline pipeline : {pipeline2D, pipeline3D})
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
        pipeline2D = pipeline3D = pipelineMesh = VK_NULL_HANDLE;
        return Fail(result, "vkCreateGraphicsPipelines");
    }
    counters.pipelinesCreated += 3;
    return true;
}

void DeviceState::DestroyPipelines() {
    if (!device)
        return;
    if (pipeline2D)
        vkDestroyPipeline(device, pipeline2D, nullptr);
    if (pipeline3D)
        vkDestroyPipeline(device, pipeline3D, nullptr);
    if (pipelineMesh)
        vkDestroyPipeline(device, pipelineMesh, nullptr);
    if (renderPass)
        vkDestroyRenderPass(device, renderPass, nullptr);
    pipeline2D = pipeline3D = pipelineMesh = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;
    renderPassFormat = VK_FORMAT_UNDEFINED;
}

bool DeviceState::AllocateTextureSet(VkImageView view, VkDescriptorPool& pool,
                                     VkDescriptorSet& set) {
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &setLayout;
    VkResult result = VK_ERROR_OUT_OF_POOL_MEMORY;
    for (size_t attempt = descriptorPools.size(); attempt > 0 && result != VK_SUCCESS; --attempt) {
        allocate.descriptorPool = descriptorPools[attempt - 1];
        result = vkAllocateDescriptorSets(device, &allocate, &set);
        if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_POOL_MEMORY &&
            result != VK_ERROR_FRAGMENTED_POOL)
            return Fail(result, "vkAllocateDescriptorSets");
    }
    if (result != VK_SUCCESS) {
        if (descriptorPools.size() >= MaxDescriptorPools)
            return Fail(VK_ERROR_OUT_OF_POOL_MEMORY, "Descriptor pool limit reached");
        const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SetsPerPool},
                                               {VK_DESCRIPTOR_TYPE_SAMPLER, SetsPerPool}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = SetsPerPool;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        VkDescriptorPool created = VK_NULL_HANDLE;
        if (const VkResult poolResult =
                vkCreateDescriptorPool(device, &poolInfo, nullptr, &created);
            poolResult != VK_SUCCESS)
            return Fail(poolResult, "vkCreateDescriptorPool");
        descriptorPools.push_back(created);
        allocate.descriptorPool = created;
        if (const VkResult setResult = vkAllocateDescriptorSets(device, &allocate, &set);
            setResult != VK_SUCCESS)
            return Fail(setResult, "vkAllocateDescriptorSets");
    }
    pool = allocate.descriptorPool;
    VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

} // namespace LuxEngine::Vk
