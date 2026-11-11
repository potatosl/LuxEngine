#include "VulkanScene3D.hpp"
#include "Engine/Assets/EnvironmentMap.hpp"
#include "Engine/Platform/VulkanPlatform.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <numbers>

namespace LuxEngine::Vk {
namespace {
struct SceneUniform {
    float camera[4];
    float environment[4];
    float options[4];
    float right[4], up[4], forward[4];
    float shadowMatrix[16];
    float shadowOptions[4];
};
struct SceneObject {
    float model[16], albedo[4], options[4];
};
static_assert(sizeof(SceneObject) == UniformObjectBytes);

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
};
void Destroy(DeviceState& s, Image& image) {
    if (image.view)
        vkDestroyImageView(s.device, image.view, nullptr);
    if (image.image)
        vmaDestroyImage(s.allocator, image.image, image.allocation);
    image = {};
}
void Check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("3D Vulkan resource creation failed: " + std::to_string(result));
}
Image CreateImage(DeviceState& s, uint32 width, uint32 height, uint32 levels, uint32 layers,
                  VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image image;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = levels;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocation.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    Check(ConsumeFault(Fault::ImageAllocation)
              ? VK_ERROR_OUT_OF_DEVICE_MEMORY
              : vmaCreateImage(s.allocator, &info, &allocation, &image.image, &image.allocation,
                               nullptr));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, levels, 0, layers};
    const auto result = vkCreateImageView(s.device, &view, nullptr, &image.view);
    if (result != VK_SUCCESS) {
        Destroy(s, image);
        Check(result);
    }
    return image;
}
VkShaderModule Shader(DeviceState& s, const char* name) {
    const auto directory = s.config.shaderDirectory.empty()
                               ? Platform::ExecutableDirectory() + "shaders/"
                               : s.config.shaderDirectory;
    auto path = std::filesystem::path(std::u8string(
                    reinterpret_cast<const char8_t*>(directory.data()), directory.size())) /
                name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < 20 || size > (16 << 20) || size % 4)
        throw std::runtime_error(std::string("Invalid 3D shader: ") + name);
    std::vector<uint32> code(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), size) || code[0] != 0x07230203)
        throw std::runtime_error(std::string("Cannot read 3D shader: ") + name);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<size_t>(size);
    info.pCode = code.data();
    VkShaderModule module{};
    Check(ConsumeFault(Fault::ShaderModule)
              ? VK_ERROR_INVALID_SHADER_NV
              : vkCreateShaderModule(s.device, &info, nullptr, &module));
    return module;
}
VkPipeline Pipeline(DeviceState& s, VkPipelineLayout layout, VkRenderPass pass, const char* vertex,
                    const char* fragment, bool mesh, bool depth, bool shadow = false) {
    VkShaderModule modules[2]{};
    try {
        modules[0] = Shader(s, vertex);
        modules[1] = Shader(s, fragment);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        const VkBool32 lit = VK_TRUE;
        const VkSpecializationMapEntry entry{0, 0, sizeof(lit)};
        const VkSpecializationInfo special{1, &entry, sizeof(lit), &lit};
        if (mesh && !shadow)
            stages[1].pSpecializationInfo = &special;
        const VkVertexInputBindingDescription binding{0, sizeof(RenderVertex),
                                                      VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[4] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderVertex, color)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(RenderVertex, uv)},
            {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, normal)}};
        VkPipelineVertexInputStateCreateInfo input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = mesh ? 1 : 0;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = mesh ? (shadow ? 3 : 4) : 0;
        input.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo z{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        z.depthTestEnable = z.depthWriteEnable = depth;
        z.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = shadow ? 0 : 1;
        blend.pAttachments = &color;
        const VkDynamicState states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &z;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        info.renderPass = pass;
        VkPipeline result{};
        Check(
            ConsumeFault(Fault::PipelineCreation)
                ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                : vkCreateGraphicsPipelines(s.device, s.pipelineCache, 1, &info, nullptr, &result));
        for (auto module : modules)
            vkDestroyShaderModule(s.device, module, nullptr);
        ++s.counters.pipelinesCreated;
        return result;
    } catch (...) {
        for (auto module : modules)
            if (module)
                vkDestroyShaderModule(s.device, module, nullptr);
        throw;
    }
}
void Viewport(VkCommandBuffer cmd, uint32 width, uint32 height) {
    const VkViewport viewport{
        0, static_cast<float>(height), static_cast<float>(width), -static_cast<float>(height), 0,
        1};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}
}

struct Scene3DRenderer::Impl {
    std::weak_ptr<DeviceState> owner;
    VkDescriptorSetLayout sceneLayout{}, toneLayout{};
    VkPipelineLayout meshLayout{}, postLayout{};
    VkRenderPass hdrPass{}, loadPass{};
    VkPipeline meshPipeline{}, postPipeline{}, skyPipeline{};
    VkSampler sampler{};
    VkRenderPass shadowPass{};
    VkPipeline shadowPipeline{};
    VkFormat shadowFormat = VK_FORMAT_UNDEFINED;
    bool shadowInitializationAttempted = false;
    std::string shadowError;
    VkFormat outputFormat = VK_FORMAT_UNDEFINED;
    struct Environment {
        std::weak_ptr<DeviceState> owner;
        Image sky, diffuse, specular, brdf;
        ~Environment() {
            if (auto s = owner.lock(); s && s->device)
                for (auto* image : {&sky, &diffuse, &specular, &brdf})
                    if (image->image)
                        s->Retire(
                            {0, VK_NULL_HANDLE, image->image, image->view, image->allocation});
        }
    };
    struct Frame {
        Image hdr, depth, shadow;
        VkFramebuffer shadowFramebuffer{};
        uint32 shadowResolution = 0;
        VkFramebuffer framebuffer{};
        VkDescriptorPool pool{};
        VkDescriptorSet sceneSet{}, toneSet{};
        uint32 width = 0, height = 0;
        std::shared_ptr<Environment> environment;
    };
    std::array<std::vector<std::unique_ptr<Frame>>, MaxFramesInFlight> frames;
    std::array<uint64, MaxFramesInFlight> frameIndices{};
    std::array<size_t, MaxFramesInFlight> frameUses{};
    std::shared_ptr<Environment> environment, neutral;
    std::future<std::shared_ptr<EnvironmentData>> pending;
    std::string requestedPath, attemptedPath, activePath;
    uint64 requestedRevision = UINT64_MAX, attemptedRevision = UINT64_MAX,
           activeRevision = UINT64_MAX;
    bool hasAttempt = false, ready = false, initializationAttempted = false;
    double preparationMs = 0;
    std::string environmentError;

    explicit Impl(const std::shared_ptr<DeviceState>& state) : owner(state) {}
    void DestroyFrame(DeviceState& s, Frame& f) {
        if (f.framebuffer)
            vkDestroyFramebuffer(s.device, f.framebuffer, nullptr);
        if (f.pool)
            vkDestroyDescriptorPool(s.device, f.pool, nullptr);
        if (f.shadowFramebuffer)
            vkDestroyFramebuffer(s.device, f.shadowFramebuffer, nullptr);
        Destroy(s, f.shadow);
        Destroy(s, f.hdr);
        Destroy(s, f.depth);
        f = {};
    }
    ~Impl() {
        if (auto s = owner.lock(); s && s->device) {
            vkDeviceWaitIdle(s->device);
            for (auto& list : frames)
                for (auto& f : list)
                    DestroyFrame(*s, *f);
            environment.reset();
            neutral.reset();
            for (auto p : {meshPipeline, postPipeline, skyPipeline, shadowPipeline})
                if (p)
                    vkDestroyPipeline(s->device, p, nullptr);
            for (auto p : {hdrPass, loadPass, shadowPass})
                if (p)
                    vkDestroyRenderPass(s->device, p, nullptr);
            for (auto p : {meshLayout, postLayout})
                if (p)
                    vkDestroyPipelineLayout(s->device, p, nullptr);
            for (auto p : {sceneLayout, toneLayout})
                if (p)
                    vkDestroyDescriptorSetLayout(s->device, p, nullptr);
            if (sampler)
                vkDestroySampler(s->device, sampler, nullptr);
        }
    }

    std::shared_ptr<Environment> Upload(DeviceState& s, const EnvironmentData& data) {
        auto result = std::make_shared<Environment>();
        result->owner = owner;
        const EnvironmentImage* sources[] = {&data.sky, &data.diffuse, &data.specular, &data.brdf};
        Image* targets[] = {&result->sky, &result->diffuse, &result->specular, &result->brdf};
        for (size_t i = 0; i < 4; ++i) {
            const auto& source = *sources[i];
            *targets[i] = CreateImage(s, source.size, source.size, source.levels, source.layers,
                                      VK_FORMAT_R32G32B32A32_SFLOAT,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                      VK_IMAGE_ASPECT_COLOR_BIT);
            Retired staging;
            VkBuffer buffer{};
            if (!s.CreateStaging(source.pixels.data(), source.pixels.size() * sizeof(float),
                                 staging, buffer))
                throw std::runtime_error("Environment staging allocation failed");
            const auto cmd = s.UploadCommands();
            if (!cmd) {
                s.DestroyRetired(staging);
                throw std::runtime_error("Environment upload command failed");
            }
            s.uploadStaging.push_back(staging);
            s.batchStagingBytes += staging.stagingBytes;
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = targets[i]->image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, source.levels, 0,
                                        source.layers};
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &barrier);
            VkDeviceSize offset = 0;
            for (uint32 level = 0; level < source.levels; ++level) {
                const uint32 size = std::max(1u, source.size >> level);
                VkBufferImageCopy region{};
                region.bufferOffset = offset;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, source.layers};
                region.imageExtent = {size, size, 1};
                vkCmdCopyBufferToImage(cmd, buffer, targets[i]->image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                offset +=
                    static_cast<VkDeviceSize>(size) * size * source.layers * 4 * sizeof(float);
            }
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                                 1, &barrier);
            s.frameStats.uploadBytes += offset;
        }
        return result;
    }

    void PollEnvironment(DeviceState& s, const Render3DSettings& settings) {
        if (!neutral)
            neutral = Upload(s, *EnvironmentData::Neutral());
        const auto revision = EnvironmentData::Revision(settings.environmentPath);
        if (pending.valid() &&
            pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto data = pending.get();
            if (requestedPath == settings.environmentPath && requestedRevision == revision) {
                if (data->error.empty()) {
                    try {
                        auto uploaded = Upload(s, *data);
                        environment = std::move(uploaded);
                        activePath = requestedPath;
                        activeRevision = requestedRevision;
                        ready = true;
                        preparationMs = data->preparationMs;
                        environmentError.clear();
                    } catch (const std::exception& e) {
                        environmentError = e.what();
                    }
                } else
                    environmentError = data->error;
            }
        }
        if ((settings.imageBasedLighting || settings.showSkybox) && !pending.valid() &&
            (!hasAttempt || attemptedPath != settings.environmentPath ||
             attemptedRevision != revision)) {
            requestedPath = attemptedPath = settings.environmentPath;
            requestedRevision = attemptedRevision = revision;
            hasAttempt = true;
            pending = std::async(std::launch::async,
                                 [path = requestedPath] { return EnvironmentData::Prepare(path); });
        }
    }

    void Initialize(DeviceState& s) {
        if (meshPipeline)
            return;
        if (initializationAttempted)
            throw std::runtime_error("HDR initialization failed; using legacy rendering");
        initializationAttempted = true;
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(s.physical, VK_FORMAT_R16G16B16A16_SFLOAT, &props);
        if ((props.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
            (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("HDR color attachment format unsupported");
        VkDescriptorSetLayoutBinding bindings[7]{};
        for (uint32 i = 0; i < 7; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[i].descriptorType = i == 0   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                         : i == 6 ? VK_DESCRIPTOR_TYPE_SAMPLER
                                                  : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        }
        VkDescriptorSetLayoutCreateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = 7;
        setInfo.pBindings = bindings;
        Check(vkCreateDescriptorSetLayout(s.device, &setInfo, nullptr, &sceneLayout));
        const VkDescriptorSetLayout layouts[3] = {s.setLayout, s.frameSetLayout, sceneLayout};
        const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 3;
        layout.pSetLayouts = layouts;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        Check(vkCreatePipelineLayout(s.device, &layout, nullptr, &meshLayout));
        VkDescriptorSetLayoutBinding postBindings[3] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
             nullptr}};
        setInfo.bindingCount = 3;
        setInfo.pBindings = postBindings;
        Check(vkCreateDescriptorSetLayout(s.device, &setInfo, nullptr, &toneLayout));
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &toneLayout;
        layout.pushConstantRangeCount = 0;
        Check(vkCreatePipelineLayout(s.device, &layout, nullptr, &postLayout));
        VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampling.magFilter = sampling.minFilter = VK_FILTER_LINEAR;
        sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampling.addressModeU = sampling.addressModeV = sampling.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampling.maxLod = 7;
        Check(vkCreateSampler(s.device, &sampling, nullptr, &sampler));
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        attachments[1].format = s.depthFormat;
        for (auto& a : attachments) {
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth;
        VkSubpassDependency dependencies[2]{};
        dependencies[0] = {
            VK_SUBPASS_EXTERNAL,
            0,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            0};
        dependencies[1] = {0,
                           VK_SUBPASS_EXTERNAL,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           0};
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 2;
        pass.pAttachments = attachments;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 2;
        pass.pDependencies = dependencies;
        Check(vkCreateRenderPass(s.device, &pass, nullptr, &hdrPass));
        attachments[0].format = s.swapchain.format;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].initialLayout = attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const auto stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0] = {VK_SUBPASS_EXTERNAL,
                           0,
                           static_cast<VkPipelineStageFlags>(stages),
                           static_cast<VkPipelineStageFlags>(stages),
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           0};
        pass.dependencyCount = 1;
        Check(vkCreateRenderPass(s.device, &pass, nullptr, &loadPass));
        postPipeline =
            Pipeline(s, postLayout, loadPass, "Tone.vert.spv", "Tone.frag.spv", false, false);
        skyPipeline =
            Pipeline(s, meshLayout, hdrPass, "Tone.vert.spv", "Sky.frag.spv", false, false);
        meshPipeline =
            Pipeline(s, meshLayout, hdrPass, "Basic.vert.spv", "Modern.frag.spv", true, true);
        outputFormat = s.swapchain.format;
    }

    Frame& GetFrame(DeviceState& s) {
        const auto slot = s.frameSlot;
        if (frameIndices[slot] != s.frameIndex) {
            frameIndices[slot] = s.frameIndex;
            frameUses[slot] = 0;
        }
        const size_t use = frameUses[slot]++;
        if (use >= 16)
            throw std::runtime_error("Too many 3D scene passes in one frame");
        auto& list = frames[slot];
        if (list.size() <= use)
            list.push_back(std::make_unique<Frame>());
        auto& f = *list[use];
        const auto extent = s.swapchain.extent;
        if (f.width != extent.width || f.height != extent.height) {
            DestroyFrame(s, f);
            try {
                f.hdr =
                    CreateImage(s, extent.width, extent.height, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT);
                f.depth = CreateImage(s, extent.width, extent.height, 1, 1, s.depthFormat,
                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                      VK_IMAGE_ASPECT_DEPTH_BIT);
                const VkImageView views[2] = {f.hdr.view, f.depth.view};
                VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                framebuffer.renderPass = hdrPass;
                framebuffer.attachmentCount = 2;
                framebuffer.pAttachments = views;
                framebuffer.width = extent.width;
                framebuffer.height = extent.height;
                framebuffer.layers = 1;
                Check(vkCreateFramebuffer(s.device, &framebuffer, nullptr, &f.framebuffer));
                VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6},
                                                 {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
                                                 {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2}};
                VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                pool.maxSets = 2;
                pool.poolSizeCount = 3;
                pool.pPoolSizes = sizes;
                Check(vkCreateDescriptorPool(s.device, &pool, nullptr, &f.pool));
                const VkDescriptorSetLayout layouts[2] = {sceneLayout, toneLayout};
                VkDescriptorSet sets[2]{};
                VkDescriptorSetAllocateInfo allocate{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocate.descriptorPool = f.pool;
                allocate.descriptorSetCount = 2;
                allocate.pSetLayouts = layouts;
                Check(vkAllocateDescriptorSets(s.device, &allocate, sets));
                f.sceneSet = sets[0];
                f.toneSet = sets[1];
                f.width = extent.width;
                f.height = extent.height;
            } catch (...) {
                DestroyFrame(s, f);
                throw;
            }
        }
        return f;
    }

    bool PrepareShadow(DeviceState& s, Frame& f, const Render3DSettings& settings) {
        if (!settings.shadows) {
            shadowError.clear();
            return false;
        }
        try {
            if (!shadowPipeline) {
                if (shadowInitializationAttempted)
                    return false;
                shadowInitializationAttempted = true;
                for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
                    VkFormatProperties props{};
                    vkGetPhysicalDeviceFormatProperties(s.physical, format, &props);
                    const auto flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                       VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
                    if ((props.optimalTilingFeatures & flags) == flags) {
                        shadowFormat = format;
                        break;
                    }
                }
                if (shadowFormat == VK_FORMAT_UNDEFINED)
                    throw std::runtime_error("Sampled shadow depth format unsupported");
                VkAttachmentDescription attachment{};
                attachment.format = shadowFormat;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                const VkAttachmentReference depth{0,
                                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                VkSubpassDescription subpass{};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.pDepthStencilAttachment = &depth;
                const VkSubpassDependency deps[2] = {
                    {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0},
                    {0, VK_SUBPASS_EXTERNAL,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}};
                VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
                pass.attachmentCount = 1;
                pass.pAttachments = &attachment;
                pass.subpassCount = 1;
                pass.pSubpasses = &subpass;
                pass.dependencyCount = 2;
                pass.pDependencies = deps;
                Check(vkCreateRenderPass(s.device, &pass, nullptr, &shadowPass));
                shadowPipeline = Pipeline(s, meshLayout, shadowPass, "Shadow.vert.spv",
                                          "Shadow.frag.spv", true, true, true);
            }
            if (f.shadowResolution != settings.shadowResolution) {
                if (f.shadowFramebuffer)
                    vkDestroyFramebuffer(s.device, f.shadowFramebuffer, nullptr);
                f.shadowFramebuffer = VK_NULL_HANDLE;
                Destroy(s, f.shadow);
                f.shadowResolution = 0;
                f.shadow = CreateImage(
                    s, settings.shadowResolution, settings.shadowResolution, 1, 1, shadowFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
                VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                framebuffer.renderPass = shadowPass;
                framebuffer.attachmentCount = 1;
                framebuffer.pAttachments = &f.shadow.view;
                framebuffer.width = framebuffer.height = settings.shadowResolution;
                framebuffer.layers = 1;
                Check(vkCreateFramebuffer(s.device, &framebuffer, nullptr, &f.shadowFramebuffer));
                f.shadowResolution = settings.shadowResolution;
            }
            shadowError.clear();
            return true;
        } catch (const std::exception& e) {
            shadowError = e.what();
            return false;
        }
    }

    void Descriptors(DeviceState& s, Frame& f) {
        const auto env = f.environment;
        VkDescriptorImageInfo images[6]{};
        const VkImageView views[6] = {env->diffuse.view, env->specular.view, env->sky.view,
                                      env->brdf.view,    env->brdf.view,     f.hdr.view};
        for (int i = 0; i < 6; ++i)
            images[i] = {VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if (f.shadowFramebuffer)
            images[4] = {VK_NULL_HANDLE, f.shadow.view,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo sampling{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        const VkDescriptorBufferInfo uniform{s.frames[s.frameSlot].uniforms.buffer, 0,
                                             sizeof(SceneUniform)};
        VkWriteDescriptorSet writes[10]{};
        uint32 index = 0;
        const auto write = [&](VkDescriptorSet set, uint32 binding, VkDescriptorType type,
                               const VkDescriptorImageInfo* image) {
            auto& w = writes[index++];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
            w.dstBinding = binding;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pImageInfo = image;
            if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
                w.pBufferInfo = &uniform;
        };
        write(f.sceneSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr);
        for (uint32 i = 0; i < 5; ++i)
            write(f.sceneSet, i + 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i]);
        write(f.sceneSet, 6, VK_DESCRIPTOR_TYPE_SAMPLER, &sampling);
        write(f.toneSet, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[5]);
        write(f.toneSet, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &sampling);
        write(f.toneSet, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr);
        vkUpdateDescriptorSets(s.device, index, writes, 0, nullptr);
    }
};

Scene3DRenderer::Scene3DRenderer(const std::shared_ptr<DeviceState>& state)
    : impl_(std::make_shared<Impl>(state)) {}
Scene3DRenderer::~Scene3DRenderer() = default;
bool Scene3DRenderer::EnvironmentReady() const {
    return impl_->ready && impl_->activePath == impl_->attemptedPath &&
           impl_->activeRevision == impl_->attemptedRevision && !impl_->pending.valid();
}
double Scene3DRenderer::EnvironmentPreparationMs() const {
    return impl_->preparationMs;
}

bool Scene3DRenderer::Render(const PerspectiveCamera& camera, const Render3DSettings& settings,
                             float exposure, std::span<const SceneDraw> draws, std::string& error) {
    auto locked = impl_->owner.lock();
    if (!locked || !locked->Usable() || !locked->frameActive)
        return false;
    auto& s = *locked;
    if (impl_->outputFormat != VK_FORMAT_UNDEFINED && impl_->outputFormat != s.swapchain.format)
        impl_ = std::make_shared<Impl>(locked);
    auto& p = *impl_;
    s.frames[s.frameSlot].retained.push_back(impl_);
    try {
        p.Initialize(s);
        if (p.outputFormat != s.swapchain.format)
            throw std::runtime_error("3D output format changed; reopen viewport");
        p.PollEnvironment(s, settings);
        auto& f = p.GetFrame(s);
        f.environment =
            p.environment && p.activePath == settings.environmentPath ? p.environment : p.neutral;
        const bool shadows = p.PrepareShadow(s, f, settings);
        p.Descriptors(s, f);
        SceneUniform uniform{};
        const auto position = camera.GetPosition();
        uniform.camera[0] = position.x;
        uniform.camera[1] = position.y;
        uniform.camera[2] = position.z;
        uniform.camera[3] = exposure;
        uniform.environment[0] = settings.environmentColor.x;
        uniform.environment[1] = settings.environmentColor.y;
        uniform.environment[2] = settings.environmentColor.z;
        uniform.environment[3] = settings.environmentIntensity;
        uniform.options[0] = settings.environmentRotation * std::numbers::pi_v<float> / 180;
        uniform.options[1] =
            settings.imageBasedLighting && p.ready && p.activePath == settings.environmentPath
                ? 1.f
                : 0.f;
        uniform.options[2] = settings.toneMapping ? 1.f : 0.f;
        uniform.options[3] =
            settings.showSkybox && p.ready && p.activePath == settings.environmentPath ? 1.f : 0.f;
        const auto& view = camera.GetViewMatrix().elements;
        const auto& projection = camera.GetProjectionMatrix().elements;
        for (int i = 0; i < 3; ++i) {
            uniform.right[i] = view[i * 4] / projection[0];
            uniform.up[i] = view[i * 4 + 1] / projection[5];
            uniform.forward[i] = -view[i * 4 + 2];
        }
        Mat4 shadowMatrix;
        if (shadows) {
            const auto direction = settings.sun.direction.Normalized();
            const auto target = camera.GetTarget();
            auto lightView =
                Mat4::LookAt(target - direction * (settings.shadowRange * 2), target,
                             std::abs(direction.y) > .99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
            const float texel = settings.shadowRange / settings.shadowResolution;
            lightView.elements[12] = std::round(lightView.elements[12] / texel) * texel;
            lightView.elements[13] = std::round(lightView.elements[13] / texel) * texel;
            const float half = settings.shadowRange * .5f;
            shadowMatrix =
                Mat4::Orthographic(-half, half, -half, half, .1f, settings.shadowRange * 4) *
                lightView;
            std::memcpy(uniform.shadowMatrix, shadowMatrix.elements, sizeof(Mat4));
            uniform.shadowOptions[0] = 1;
            uniform.shadowOptions[1] = 1.f / settings.shadowResolution;
            uniform.shadowOptions[2] = settings.shadowDepthBias;
            uniform.shadowOptions[3] = settings.shadowNormalBias;
        }
        uint32 offset = 0;
        if (!s.PushUniform(&uniform, sizeof(uniform), offset))
            throw std::runtime_error("3D uniform budget exceeded");
        const auto& ring = s.frames[s.frameSlot].uniforms;
        const auto alignment = s.uniformAlignment;
        const auto aligned = (ring.used + alignment - 1) / alignment * alignment;
        const auto stride = (sizeof(SceneObject) + alignment - 1) / alignment * alignment;
        const auto limit =
            aligned < ring.capacity ? static_cast<size_t>((ring.capacity - aligned) / stride) : 0;
        if (draws.size() > limit) {
            draws = draws.first(limit);
            error = "3D object uniform budget reached; excess objects skipped";
        }
        std::vector<uint32> objects;
        objects.reserve(draws.size());
        for (const auto& draw : draws) {
            SceneObject object{};
            std::memcpy(object.model, draw.model.elements, sizeof(object.model));
            object.albedo[0] = draw.albedo.x;
            object.albedo[1] = draw.albedo.y;
            object.albedo[2] = draw.albedo.z;
            object.albedo[3] = draw.albedo.w;
            object.options[0] = 1;
            object.options[1] =
                std::isfinite(draw.metallic) ? std::clamp(draw.metallic, 0.f, 1.f) : 0;
            object.options[2] =
                std::isfinite(draw.roughness) ? std::clamp(draw.roughness, .045f, 1.f) : .5f;
            object.options[3] = 1;
            uint32 objectOffset = 0;
            if (!s.PushUniform(&object, sizeof(object), objectOffset))
                throw std::runtime_error("3D object budget exceeded");
            objects.push_back(objectOffset);
        }
        if (!s.FlushUploads())
            throw std::runtime_error("3D uploads failed");
        auto cmd = s.frames[s.frameSlot].cmd;
        if (!s.renderPassActive)
            s.BeginRenderPass();
        vkCmdEndRenderPass(cmd);
        s.renderPassActive = false;
        if (shadows) {
            const VkClearValue clear{.depthStencil = {1, 0}};
            VkRenderPassBeginInfo shadowBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            shadowBegin.renderPass = p.shadowPass;
            shadowBegin.framebuffer = f.shadowFramebuffer;
            shadowBegin.renderArea = {{0, 0}, {f.shadowResolution, f.shadowResolution}};
            shadowBegin.clearValueCount = 1;
            shadowBegin.pClearValues = &clear;
            vkCmdBeginRenderPass(cmd, &shadowBegin, VK_SUBPASS_CONTENTS_INLINE);
            Viewport(cmd, f.shadowResolution, f.shadowResolution);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.shadowPipeline);
            ++s.frameStats.pipelineBinds;
            vkCmdPushConstants(cmd, p.meshLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4),
                               shadowMatrix.elements);
            ++s.frameStats.pushConstantUpdates;
            for (size_t i = 0; i < draws.size(); ++i) {
                const auto& draw = draws[i];
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &draw.vertices, &zero);
                vkCmdBindIndexBuffer(cmd, draw.indices, 0, VK_INDEX_TYPE_UINT32);
                const uint32 dynamic[2] = {objects[i], s.lightOffset};
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 0, 1,
                                        &draw.texture, 0, nullptr);
                ++s.frameStats.descriptorBinds;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 1, 1,
                                        &s.frames[s.frameSlot].uniformSet, 2, dynamic);
                ++s.frameStats.uniformBinds;
                vkCmdDrawIndexed(cmd, draw.indexCount, 1, 0, 0, 0);
                ++s.frameStats.drawCalls;
            }
            vkCmdEndRenderPass(cmd);
        }
        VkClearValue clears[2]{};
        clears[0].color = s.clearColor;
        clears[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = p.hdrPass;
        begin.framebuffer = f.framebuffer;
        begin.renderArea = {{0, 0}, {f.width, f.height}};
        begin.clearValueCount = 2;
        begin.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        Viewport(cmd, f.width, f.height);
        if (uniform.options[3] > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.skyPipeline);
            ++s.frameStats.pipelineBinds;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 2, 1,
                                    &f.sceneSet, 1, &offset);
            ++s.frameStats.descriptorBinds;
            vkCmdDraw(cmd, 3, 1, 0, 0);
            ++s.frameStats.drawCalls;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshPipeline);
        ++s.frameStats.pipelineBinds;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 2, 1,
                                &f.sceneSet, 1, &offset);
        ++s.frameStats.descriptorBinds;
        vkCmdPushConstants(cmd, p.meshLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4),
                           camera.GetViewProjectionMatrix().elements);
        ++s.frameStats.pushConstantUpdates;
        for (size_t i = 0; i < draws.size(); ++i) {
            const auto& draw = draws[i];
            const VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &draw.vertices, &zero);
            vkCmdBindIndexBuffer(cmd, draw.indices, 0, VK_INDEX_TYPE_UINT32);
            const uint32 dynamic[2] = {objects[i], s.lightOffset};
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 0, 1,
                                    &draw.texture, 0, nullptr);
            ++s.frameStats.descriptorBinds;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.meshLayout, 1, 1,
                                    &s.frames[s.frameSlot].uniformSet, 2, dynamic);
            ++s.frameStats.uniformBinds;
            vkCmdDrawIndexed(cmd, draw.indexCount, 1, 0, 0, 0);
            ++s.frameStats.drawCalls;
        }
        vkCmdEndRenderPass(cmd);
        VkMemoryBarrier outputBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        outputBarrier.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        outputBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 1, &outputBarrier, 0, nullptr, 0, nullptr);
        begin.renderPass = p.loadPass;
        begin.framebuffer = s.swapchain.framebuffers[s.imageIndex];
        begin.clearValueCount = 0;
        begin.pClearValues = nullptr;
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        Viewport(cmd, f.width, f.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.postPipeline);
        ++s.frameStats.pipelineBinds;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.postLayout, 0, 1,
                                &f.toneSet, 1, &offset);
        ++s.frameStats.descriptorBinds;
        vkCmdDraw(cmd, 3, 1, 0, 0);
        ++s.frameStats.drawCalls;
        s.renderPassActive = true;
        s.boundPipeline = VK_NULL_HANDLE;
        s.boundSet = VK_NULL_HANDLE;
        s.boundVertexBuffer = s.boundIndexBuffer = VK_NULL_HANDLE;
        s.matrixBound = false;
        s.boundObjectOffset = s.boundLightOffset = UINT32_MAX;
        if (!p.environmentError.empty())
            error = p.environmentError;
        if (!p.shadowError.empty())
            error += (error.empty() ? "" : "; ") + p.shadowError;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        if (s.Usable() && s.commandsOpen) {
            auto cmd = s.frames[s.frameSlot].cmd;
            if (!s.renderPassActive)
                s.BeginRenderPass();
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineMesh);
            ++s.frameStats.pipelineBinds;
            vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4),
                               camera.GetViewProjectionMatrix().elements);
            ++s.frameStats.pushConstantUpdates;
            for (const auto& draw : draws) {
                SceneObject object{};
                std::memcpy(object.model, draw.model.elements, sizeof(object.model));
                object.albedo[0] = draw.albedo.x * exposure;
                object.albedo[1] = draw.albedo.y * exposure;
                object.albedo[2] = draw.albedo.z * exposure;
                object.albedo[3] = draw.albedo.w;
                object.options[0] = 1;
                uint32 offset = 0;
                if (!s.PushUniform(&object, sizeof(object), offset))
                    break;
                const VkDeviceSize zero = 0;
                const uint32 dynamic[2] = {offset, s.lightOffset};
                vkCmdBindVertexBuffers(cmd, 0, 1, &draw.vertices, &zero);
                vkCmdBindIndexBuffer(cmd, draw.indices, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0,
                                        1, &draw.texture, 0, nullptr);
                ++s.frameStats.descriptorBinds;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 1,
                                        1, &s.frames[s.frameSlot].uniformSet, 2, dynamic);
                ++s.frameStats.uniformBinds;
                vkCmdDrawIndexed(cmd, draw.indexCount, 1, 0, 0, 0);
                ++s.frameStats.drawCalls;
            }
            s.boundPipeline = VK_NULL_HANDLE;
            s.boundSet = VK_NULL_HANDLE;
            s.matrixBound = false;
            s.boundVertexBuffer = s.boundIndexBuffer = VK_NULL_HANDLE;
            s.boundObjectOffset = s.boundLightOffset = UINT32_MAX;
        }
        return false;
    }
}
}
