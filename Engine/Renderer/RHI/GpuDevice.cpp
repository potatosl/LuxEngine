#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace LuxEngine {
namespace {
constexpr uint64 AcquireTimeoutNs = 1'000'000'000ull;
constexpr uint32 MaxSwapchainFailures = 3;
constexpr uint32 MaxFrameFailures = 3;
const std::string NoError;
const GpuDeviceInfo NoInfo;

struct ObjectUniform {
    float model[16];
    float albedo[4];
    float colorOptions[4];
};
static_assert(sizeof(ObjectUniform) == Vk::UniformObjectBytes);

struct LightUniform {
    float direction[4];
    float colorIntensity[4];
    uint32 count[4];
    float camera[4];
    struct {
        float positionRadius[4];
        float colorIntensity[4];
    } points[Vk::MaxPointLights];
};
static_assert(sizeof(LightUniform) == Vk::UniformLightBytes);

VkResult SubmitRecoveryFrame(Vk::DeviceState& s, Vk::FrameData& frame, uint64& serial) {
    VkResult result = vkResetCommandPool(s.device, frame.pool, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (result == VK_SUCCESS)
        result = vkBeginCommandBuffer(frame.cmd, &begin);
    if (result != VK_SUCCESS)
        return result;
    s.BeginRenderPass();
    vkCmdEndRenderPass(frame.cmd);
    s.renderPassActive = false;
    result = vkEndCommandBuffer(frame.cmd);
    if (result != VK_SUCCESS)
        return result;
    return s.Submit(frame.cmd, frame.imageAvailable, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    s.swapchain.renderFinished[s.imageIndex], serial);
}

std::string VersionString(uint32 version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
           std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}
} // namespace

const char* ToString(GpuStatus status) {
    switch (status) {
    case GpuStatus::Uninitialized:
        return "Uninitialized";
    case GpuStatus::Ready:
        return "Ready";
    case GpuStatus::OutOfMemory:
        return "OutOfMemory";
    case GpuStatus::SurfaceLost:
        return "SurfaceLost";
    case GpuStatus::Timeout:
        return "Timeout";
    case GpuStatus::DeviceLost:
        return "DeviceLost";
    case GpuStatus::Error:
        return "Error";
    }
    return "Unknown";
}

GpuDevice::~GpuDevice() {
    Shutdown();
}

bool GpuDevice::Initialize(Window& window, const GpuDeviceConfig& config) {
    if (state_ && state_->status != GpuStatus::Uninitialized)
        return false;
    state_.reset();
    if (!window.GetNativeHandle())
        return false;
    auto state = CreateRef<Vk::DeviceState>();
    state->window = &window;
    state->config = config;
    state->config.stagingFlushBytes = std::max<uint64>(config.stagingFlushBytes, 1ull << 20);
    state->config.maxFrameVertexBytes = std::max<uint64>(config.maxFrameVertexBytes, 1ull << 16);
    state->frameCount = std::clamp<uint32>(config.framesInFlight, 1, Vk::MaxFramesInFlight);
    state->vsync = config.vsync;
    state->info.framesInFlight = state->frameCount;
    state_ = state;

    bool ready = state->CreateInstance() && state->SelectPhysicalDevice() &&
                 state->CreateDevice() && state->CreateSync() && state->LoadShaders() &&
                 state->CreateFrames();
    if (ready) {
        const VkFormat format = state->SurfaceFormat();
        ready = format != VK_FORMAT_UNDEFINED && state->CreateRenderPass(format) &&
                state->CreatePipelines();
        if (format == VK_FORMAT_UNDEFINED)
            state->Fail(GpuStatus::Error, "Surface has no usable format");
    }
    if (ready) {
        state->status = GpuStatus::Ready;
        ready = state->CreateSwapchain() != Vk::SwapchainResult::Failed;
    }
    if (ready) {
        const uint32 white = 0xffffffffu;
        Vk::TextureRecord record;
        ready = state->CreateTextureResource(1, 1, &white, record);
        if (ready)
            state->whiteTexture = state->textures.Insert(record);
        ready = ready && state->FlushUploads();
    }
    if (!ready) {
        const std::string error = state->lastError;
        const GpuStatus failure = state->lastFailure;
        state->Shutdown();
        state->lastError = error;
        state->lastFailure = failure;
        Log::Error("GPU device initialization failed: " + error);
        return false;
    }
    Log::Info("Vulkan device: " + state->info.deviceName + " (" + state->info.deviceType +
              ", Vulkan " + VersionString(state->info.apiVersion) + ", timeline " +
              (state->info.timelineSemaphores ? "on" : "off") + ", validation " +
              (state->info.validation ? "on" : "off") + ")");
    return true;
}

void GpuDevice::Shutdown() {
    Renderer2D::DetachDevice(this);
    Renderer3D::DetachDevice(this);
    if (!state_)
        return;
    if (state_->frameActive && state_->Usable())
        EndFrame();
    state_->Shutdown();
    state_.reset();
}

bool GpuDevice::IsReady() const {
    return state_ && state_->Usable();
}

GpuStatus GpuDevice::GetStatus() const {
    return state_ ? state_->status : GpuStatus::Uninitialized;
}

GpuStatus GpuDevice::GetLastFailure() const {
    return state_ ? state_->lastFailure : GpuStatus::Ready;
}

const std::string& GpuDevice::GetLastError() const {
    return state_ ? state_->lastError : NoError;
}

const GpuDeviceInfo& GpuDevice::GetInfo() const {
    return state_ ? state_->info : NoInfo;
}

GpuFrameStats GpuDevice::GetFrameStats() const {
    return state_ ? state_->lastFrameStats : GpuFrameStats{};
}

GpuDeviceCounters GpuDevice::GetCounters() const {
    if (!state_)
        return {};
    GpuDeviceCounters counters = state_->counters;
    counters.validationErrors =
        state_->instanceValidation->errors.load() - state_->validationErrorsAtInitialization;
    counters.validationWarnings =
        state_->instanceValidation->warnings.load() - state_->validationWarningsAtInitialization;
    return counters;
}

GpuMemoryStats GpuDevice::GetMemoryStats() const {
    GpuMemoryStats stats;
    if (!state_ || !state_->allocator)
        return stats;
    auto& s = *state_;
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(s.physical, &memory);
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(s.allocator, budgets);
    for (uint32 heap = 0; heap < memory.memoryHeapCount; ++heap) {
        stats.allocationBytes += budgets[heap].statistics.allocationBytes;
        stats.allocationCount += budgets[heap].statistics.allocationCount;
        if (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            stats.deviceLocalUsage += budgets[heap].usage;
            stats.deviceLocalBudget += budgets[heap].budget;
        }
    }
    stats.liveBuffers = s.buffers.Live();
    stats.liveTextures = s.textures.Live();
    stats.pendingDestructions = static_cast<uint32>(s.retired.size() + s.pendingRetire.size());
    stats.descriptorPools = static_cast<uint32>(s.descriptorPools.size());
    stats.pendingStagingBytes = s.liveStagingBytes;
    return stats;
}

GpuDevice::FrameStart GpuDevice::BeginFrame() {
    if (!state_ || !state_->Usable() || state_->frameActive)
        return FrameStart::Error;
    auto& s = *state_;
    s.PollCompleted();
    if (!s.Usable())
        return FrameStart::Error;
    uint32 width = 0, height = 0;
    if (s.window->IsMinimized() || !s.window->GetFramebufferSize(width, height) || width == 0 ||
        height == 0) {
        ++s.counters.framesSkipped;
        return FrameStart::Skipped;
    }
    if (!s.swapchain.handle || s.swapchainDirty) {
        if (!s.RecreateSwapchain()) {
            if (!s.Usable() || ++s.swapchainFailures >= MaxSwapchainFailures)
                return FrameStart::Error;
            ++s.counters.framesSkipped;
            return FrameStart::Skipped;
        }
        s.swapchainFailures = 0;
        if (!s.swapchain.handle || s.swapchainDirty) {
            ++s.counters.framesSkipped;
            return FrameStart::Skipped;
        }
    }

    Vk::FrameData& frame = s.frames[s.frameSlot];
    s.frameStats = {};
    const auto waitStart = std::chrono::steady_clock::now();
    // Drain this slot's previous present before acquiring another image. If a bounded wait
    // times out, no new acquire semaphore has been signaled and the same slot remains retryable.
    if (!s.WaitForSerial(frame.serial) || !s.WaitForPresent(frame, s.config.gpuTimeoutNs))
        return FrameStart::Error;
    frame.retained.clear();
    s.frameStats.cpuWaitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waitStart)
            .count();
    s.frameStats.gpuTimeMs = s.lastFrameStats.gpuTimeMs;
    if (frame.queries && frame.queriesWritten) {
        uint64 ticks[2]{};
        if (vkGetQueryPoolResults(s.device, frame.queries, 0, 2, sizeof(ticks), ticks,
                                  sizeof(uint64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0])
            s.frameStats.gpuTimeMs =
                static_cast<double>(ticks[1] - ticks[0]) * s.timestampPeriod / 1'000'000.0;
        frame.queriesWritten = false;
    }
    s.CollectGarbage();

    VkResult acquired =
        Vk::ConsumeFault(Vk::Fault::AcquireOutOfDate)
            ? VK_ERROR_OUT_OF_DATE_KHR
            : vkAcquireNextImageKHR(s.device, s.swapchain.handle, AcquireTimeoutNs,
                                    frame.imageAvailable, VK_NULL_HANDLE, &s.imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        s.swapchainDirty = true;
        ++s.counters.framesSkipped;
        return FrameStart::Skipped;
    }
    if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
        ++s.counters.framesSkipped;
        return FrameStart::Skipped;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
        s.swapchainDirty = true;
    } else if (acquired != VK_SUCCESS) {
        s.Fail(acquired, "vkAcquireNextImageKHR");
        return FrameStart::Error;
    }

    VkResult result = vkResetCommandPool(s.device, frame.pool, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (result == VK_SUCCESS)
        result = Vk::ConsumeFault(Vk::Fault::CommandBuffer)
                     ? VK_ERROR_OUT_OF_HOST_MEMORY
                     : vkBeginCommandBuffer(frame.cmd, &begin);
    s.frameActive = true;
    s.renderPassActive = false;
    s.commandsOpen = result == VK_SUCCESS;
    if (result != VK_SUCCESS) {
        s.Fail(result, "Frame command buffer begin");
        EndFrame();
        return s.Usable() ? FrameStart::Skipped : FrameStart::Error;
    }
    if (frame.queries) {
        vkCmdResetQueryPool(frame.cmd, frame.queries, 0, 2);
        vkCmdWriteTimestamp(frame.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queries, 0);
    }
    frame.ring.used = 0;
    if (!s.ResetFrameUniforms()) {
        s.commandsOpen = false;
        EndFrame();
        return s.Usable() ? FrameStart::Skipped : FrameStart::Error;
    }
    s.frameStats.frameIndex = ++s.frameIndex;
    s.clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
    s.boundPipeline = VK_NULL_HANDLE;
    s.boundSet = VK_NULL_HANDLE;
    s.boundVertexBuffer = VK_NULL_HANDLE;
    s.boundIndexBuffer = VK_NULL_HANDLE;
    s.matrixBound = false;
    return FrameStart::Ready;
}

bool Vk::DeviceState::BeginRenderPass() {
    FrameData& frame = frames[frameSlot];
    VkClearValue clears[2]{};
    clears[0].color = clearColor;
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = renderPass;
    begin.framebuffer = swapchain.framebuffers[imageIndex];
    begin.renderArea = {{0, 0}, swapchain.extent};
    begin.clearValueCount = 2;
    begin.pClearValues = clears;
    vkCmdBeginRenderPass(frame.cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const float width = static_cast<float>(swapchain.extent.width);
    const float height = static_cast<float>(swapchain.extent.height);
    const VkViewport viewport{0.0f, height, width, -height, 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, swapchain.extent};
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    renderPassActive = true;
    return true;
}

bool Vk::DeviceState::ResetFrameUniforms() {
    frames[frameSlot].uniforms.used = 0;
    boundObjectOffset = boundLightOffset = UINT32_MAX;
    ObjectUniform object{};
    const Mat4 identity;
    std::memcpy(object.model, identity.elements, sizeof(object.model));
    object.albedo[0] = object.albedo[1] = object.albedo[2] = object.albedo[3] = 1.0f;
    uint32 objectOffset = 0;
    const LightUniform lights{};
    return PushUniform(&object, sizeof(object), objectOffset) &&
           PushUniform(&lights, sizeof(lights), lightOffset);
}

void Vk::DeviceState::BindFrameSet(uint32 objectOffset) {
    if (objectOffset == boundObjectOffset && lightOffset == boundLightOffset)
        return;
    const uint32 offsets[2] = {objectOffset, lightOffset};
    vkCmdBindDescriptorSets(frames[frameSlot].cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            1, 1, &frames[frameSlot].uniformSet, 2, offsets);
    boundObjectOffset = objectOffset;
    boundLightOffset = lightOffset;
    ++frameStats.uniformBinds;
}

bool GpuDevice::Clear(float r, float g, float b, float a) {
    if (!state_ || !state_->frameActive)
        return false;
    auto& s = *state_;
    s.clearColor = {{r, g, b, a}};
    if (s.renderPassActive) {
        VkClearAttachment attachment{};
        attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachment.colorAttachment = 0;
        attachment.clearValue.color = s.clearColor;
        const VkClearRect rect{{{0, 0}, s.swapchain.extent}, 0, 1};
        vkCmdClearAttachments(s.frames[s.frameSlot].cmd, 1, &attachment, 1, &rect);
    }
    return true;
}

bool GpuDevice::DrawTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                              GpuTexture* texture, bool depth) {
    if (texture && !texture->BelongsTo(*this))
        return false;
    return DrawTexturedTriangles(vertices, viewProjection, texture ? texture->handle_ : 0, depth);
}

bool GpuDevice::DrawTexturedTriangles(std::span<const RenderVertex> vertices,
                                      const Mat4& viewProjection, uint64 textureHandle,
                                      bool depth) {
    if (!state_ || !state_->frameActive || vertices.empty() || vertices.size() % 3 != 0 ||
        vertices.size() > std::numeric_limits<uint32>::max())
        return false;
    auto& s = *state_;
    Vk::TextureRecord* record = s.textures.Get(textureHandle ? textureHandle : s.whiteTexture);
    if (!record)
        return false;
    Vk::FrameData& frame = s.frames[s.frameSlot];
    const VkDeviceSize bytes = vertices.size_bytes();
    if (!s.EnsureVertexCapacity(frame, bytes))
        return false;
    const auto firstVertex = static_cast<uint32>(frame.ring.used / sizeof(RenderVertex));
    std::memcpy(frame.ring.mapped + frame.ring.used, vertices.data(), static_cast<size_t>(bytes));
    frame.ring.used += bytes;

    if (!s.renderPassActive)
        s.BeginRenderPass();
    VkCommandBuffer cmd = frame.cmd;
    VkPipeline pipeline = depth ? s.pipeline3D : s.pipeline2D;
    if (pipeline != s.boundPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        s.boundPipeline = pipeline;
        ++s.frameStats.pipelineBinds;
    }
    if (frame.ring.buffer != s.boundVertexBuffer) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.ring.buffer, &offset);
        s.boundVertexBuffer = frame.ring.buffer;
    }
    if (record->set != s.boundSet) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1,
                                &record->set, 0, nullptr);
        s.boundSet = record->set;
        ++s.frameStats.descriptorBinds;
    }
    if (!s.matrixBound ||
        std::memcmp(s.boundMatrix.elements, viewProjection.elements, sizeof(Mat4::elements)) != 0) {
        vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(Mat4::elements), viewProjection.elements);
        s.boundMatrix = viewProjection;
        s.matrixBound = true;
        ++s.frameStats.pushConstantUpdates;
    }
    s.BindFrameSet(0);
    vkCmdDraw(cmd, static_cast<uint32>(vertices.size()), 1, firstVertex, 0);
    ++s.frameStats.drawCalls;
    s.frameStats.vertexBytes += bytes;
    return true;
}

bool GpuDevice::SetLighting(const GpuLighting& lighting) {
    if (!state_ || !state_->frameActive || !state_->commandsOpen)
        return false;
    LightUniform lights{};
    lights.camera[0] = lighting.camera.x;
    lights.camera[1] = lighting.camera.y;
    lights.camera[2] = lighting.camera.z;
    lights.camera[3] = lighting.pbr ? 1.f : 0.f;
    const Vec3 direction = lighting.direction.Normalized();
    lights.direction[0] = direction.x;
    lights.direction[1] = direction.y;
    lights.direction[2] = direction.z;
    lights.colorIntensity[0] = lighting.color.x;
    lights.colorIntensity[1] = lighting.color.y;
    lights.colorIntensity[2] = lighting.color.z;
    lights.colorIntensity[3] = lighting.intensity;
    const size_t count = std::min<size_t>(lighting.points.size(), Vk::MaxPointLights);
    lights.count[0] = static_cast<uint32>(count);
    for (size_t index = 0; index < count; ++index) {
        const GpuPointLight& point = lighting.points[index];
        auto& target = lights.points[index];
        target.positionRadius[0] = point.position.x;
        target.positionRadius[1] = point.position.y;
        target.positionRadius[2] = point.position.z;
        target.positionRadius[3] = point.radius;
        target.colorIntensity[0] = point.color.x;
        target.colorIntensity[1] = point.color.y;
        target.colorIntensity[2] = point.color.z;
        target.colorIntensity[3] = point.intensity;
    }
    return state_->PushUniform(&lights, sizeof(lights), state_->lightOffset);
}

bool GpuDevice::DrawIndexed(const GpuBuffer& vertices, const GpuBuffer& indices, uint32 indexCount,
                            const Mat4& viewProjection, const Mat4& model, const Vec4& albedo,
                            GpuTexture* texture, float metallic, float roughness, bool pbr) {
    if (!state_ || !state_->frameActive || !state_->commandsOpen || indexCount == 0 ||
        indexCount % 3 != 0 || vertices.type_ != BufferType::Vertex ||
        indices.type_ != BufferType::Index || !vertices.BelongsTo(*this) ||
        !indices.BelongsTo(*this) ||
        static_cast<uint64>(indexCount) * sizeof(uint32) > indices.sizeBytes_)
        return false;
    auto& s = *state_;
    Vk::BufferRecord* vertexRecord = s.buffers.Get(vertices.handle_);
    Vk::BufferRecord* indexRecord = s.buffers.Get(indices.handle_);
    if (texture && !texture->BelongsTo(*this))
        return false;
    Vk::TextureRecord* white = s.textures.Get(texture ? texture->handle_ : s.whiteTexture);
    if (!vertexRecord || !indexRecord || !white)
        return false;
    ObjectUniform object{};
    std::memcpy(object.model, model.elements, sizeof(object.model));
    object.albedo[0] = albedo.x;
    object.albedo[1] = albedo.y;
    object.albedo[2] = albedo.z;
    object.albedo[3] = albedo.w;
    object.colorOptions[0] =
        texture && texture->GetColorSpace() == TextureColorSpace::Srgb ? 1.f : 0.f;
    object.colorOptions[1] = std::isfinite(metallic) ? std::clamp(metallic, 0.f, 1.f) : 0.f;
    object.colorOptions[2] = std::isfinite(roughness) ? std::clamp(roughness, .045f, 1.f) : .5f;
    object.colorOptions[3] = pbr ? 1.f : 0.f;
    uint32 objectOffset = 0;
    if (!s.PushUniform(&object, sizeof(object), objectOffset))
        return false;

    if (!s.renderPassActive)
        s.BeginRenderPass();
    VkCommandBuffer cmd = s.frames[s.frameSlot].cmd;
    if (s.pipelineMesh != s.boundPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineMesh);
        s.boundPipeline = s.pipelineMesh;
        ++s.frameStats.pipelineBinds;
    }
    if (vertexRecord->buffer != s.boundVertexBuffer) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexRecord->buffer, &offset);
        s.boundVertexBuffer = vertexRecord->buffer;
    }
    if (indexRecord->buffer != s.boundIndexBuffer) {
        vkCmdBindIndexBuffer(cmd, indexRecord->buffer, 0, VK_INDEX_TYPE_UINT32);
        s.boundIndexBuffer = indexRecord->buffer;
    }
    if (white->set != s.boundSet) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipelineLayout, 0, 1,
                                &white->set, 0, nullptr);
        s.boundSet = white->set;
        ++s.frameStats.descriptorBinds;
    }
    if (!s.matrixBound ||
        std::memcmp(s.boundMatrix.elements, viewProjection.elements, sizeof(Mat4::elements)) != 0) {
        vkCmdPushConstants(cmd, s.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(Mat4::elements), viewProjection.elements);
        s.boundMatrix = viewProjection;
        s.matrixBound = true;
        ++s.frameStats.pushConstantUpdates;
    }
    s.BindFrameSet(objectOffset);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    ++s.frameStats.drawCalls;
    return true;
}

bool GpuDevice::EndFrame() {
    if (!state_ || !state_->frameActive)
        return false;
    auto& s = *state_;
    Vk::FrameData& frame = s.frames[s.frameSlot];
    VkCommandBuffer cmd = frame.cmd;
    const bool recording = s.Usable() && s.commandsOpen;
    s.commandsOpen = false;
    if (recording) {
        if (!s.renderPassActive)
            s.BeginRenderPass();
        vkCmdEndRenderPass(cmd);
        s.renderPassActive = false;
        if (s.capture.requested) {
            s.capture.requested = false;
            const VkExtent2D extent = s.swapchain.extent;
            const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
            if (!s.swapchain.transferSource)
                s.Fail(GpuStatus::Error, "Swapchain images cannot be read back");
            else if (s.EnsureCaptureBuffer(size)) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = s.swapchain.images[s.imageIndex];
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                VkBufferMemoryBarrier reuse{};
                reuse.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                reuse.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
                reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                reuse.srcQueueFamilyIndex = reuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                reuse.buffer = s.capture.buffer;
                reuse.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT |
                                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &reuse, 1,
                                     &barrier);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {extent.width, extent.height, 1};
                vkCmdCopyImageToBuffer(cmd, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       s.capture.buffer, 1, &region);
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = 0;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &barrier);
                VkBufferMemoryBarrier host{};
                host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                host.buffer = s.capture.buffer;
                host.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0,
                                     nullptr);
                s.capture.recorded = true;
                s.capture.serial = 0;
                s.capture.width = extent.width;
                s.capture.height = extent.height;
                s.capture.format = s.swapchain.format;
            }
        }
        if (frame.queries) {
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queries, 1);
            frame.queriesWritten = true;
        }
        if (frame.ring.used)
            vmaFlushAllocation(s.allocator, frame.ring.allocation, 0, frame.ring.used);
        if (frame.uniforms.used)
            vmaFlushAllocation(s.allocator, frame.uniforms.allocation, 0, frame.uniforms.used);
    }

    VkResult result = recording ? vkEndCommandBuffer(cmd) : VK_ERROR_UNKNOWN;
    if (result == VK_SUCCESS && !s.FlushUploads())
        result = VK_ERROR_UNKNOWN;
    uint64 serial = 0;
    if (result == VK_SUCCESS)
        result = s.Submit(cmd, frame.imageAvailable, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          s.swapchain.renderFinished[s.imageIndex], serial);
    bool rendered = result == VK_SUCCESS;
    if (!rendered) {
        frame.queriesWritten = false;
        s.capture.recorded = false;
        if (result == VK_ERROR_DEVICE_LOST && serial != 0)
            frame.serial = serial;
        if (recording || result != VK_ERROR_UNKNOWN)
            s.Fail(result, "Frame submission");
        result = s.Usable() ? SubmitRecoveryFrame(s, frame, serial) : VK_ERROR_UNKNOWN;
        if (result != VK_SUCCESS) {
            if (s.Usable()) {
                s.Fail(result, "Frame recovery submission");
                s.status = GpuStatus::Error;
            }
            s.frameActive = false;
            s.StampPending(s.submittedSerial);
            s.frameSlot = (s.frameSlot + 1) % s.frameCount;
            return false;
        }
        s.swapchainDirty = true;
    }
    frame.serial = serial;
    s.frameActive = false;
    s.StampPending(serial);
    if (s.capture.recorded && s.capture.serial == 0)
        s.capture.serial = serial;
    s.frameSlot = (s.frameSlot + 1) % s.frameCount;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &s.swapchain.renderFinished[s.imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &s.swapchain.handle;
    present.pImageIndices = &s.imageIndex;
#if defined(VK_EXT_swapchain_maintenance1)
    VkSwapchainPresentFenceInfoEXT presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    presentFenceInfo.swapchainCount = 1;
    presentFenceInfo.pFences = &frame.presentFence;
    if (s.info.presentFences)
        present.pNext = &presentFenceInfo;
#endif
    const bool skippedPresent = Vk::ConsumeFault(Vk::Fault::PresentOutOfHostMemory);
    VkResult presented =
        skippedPresent ? VK_ERROR_OUT_OF_HOST_MEMORY : vkQueuePresentKHR(s.presentQueue, &present);
    // These WSI errors still enqueue the semaphore wait and present-fence signal. Allocation
    // failures leave every synchronization primitive unaffected, so their fences stay unowned.
    bool queued = !skippedPresent && (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR ||
                                      presented == VK_ERROR_OUT_OF_DATE_KHR ||
                                      presented == VK_ERROR_SURFACE_LOST_KHR ||
                                      presented == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
#if defined(VK_EXT_present_timing)
    queued = queued || (!skippedPresent && presented == VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT);
#endif
    if (queued && s.info.presentFences) {
        frame.presentPending = true;
        ++s.counters.presentFencesSubmitted;
    }
    // Fault injection preserves a real enqueue; fabricating OUT_OF_DATE before the call would
    // leave an unsignaled, unsubmitted fence and could deadlock teardown.
    if (queued && Vk::ConsumeFault(Vk::Fault::PresentOutOfDate))
        presented = VK_ERROR_OUT_OF_DATE_KHR;
    if (rendered) {
        s.lastFrameStats = s.frameStats;
        ++s.counters.framesSubmitted;
        s.frameFailures = 0;
    } else if (++s.frameFailures >= MaxFrameFailures) {
        s.status = GpuStatus::Error;
        s.lastError = "Repeated frame submission failures: " + s.lastError;
    }
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        s.swapchainDirty = true;
    else if (presented != VK_SUCCESS) {
        // An unqueued present retains the acquired image and its signaled render semaphore.
        // Reinitialize instead of trying to reuse that incomplete frame on this device.
        if (!queued && presented != VK_ERROR_DEVICE_LOST)
            s.status = GpuStatus::Error;
        return s.Fail(presented, "vkQueuePresentKHR");
    }
    return rendered;
}

bool GpuDevice::SetVSync(bool enabled) {
    if (!IsReady() || state_->frameActive)
        return false;
    auto& s = *state_;
    if (!enabled) {
        uint32 count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(s.physical, s.surface, &count, modes.data());
        const bool tearing = std::any_of(modes.begin(), modes.end(), [](VkPresentModeKHR mode) {
            return mode == VK_PRESENT_MODE_MAILBOX_KHR || mode == VK_PRESENT_MODE_IMMEDIATE_KHR;
        });
        if (!tearing)
            return false;
    }
    if (s.vsync != enabled) {
        s.vsync = enabled;
        s.swapchainDirty = true;
    }
    return true;
}

void GpuDevice::NotifyResize() {
    if (state_)
        state_->swapchainDirty = true;
}

bool GpuDevice::FlushUploads() {
    return IsReady() && state_->FlushUploads();
}

bool GpuDevice::WaitIdle() {
    if (!IsReady() || state_->frameActive || !state_->FlushUploads())
        return false;
    if (const VkResult result = vkDeviceWaitIdle(state_->device); result != VK_SUCCESS)
        return state_->Fail(result, "vkDeviceWaitIdle");
    if (!state_->WaitForPresents(state_->config.gpuTimeoutNs))
        return false;
    state_->CollectGarbage();
    return state_->Usable();
}

bool GpuDevice::RequestFrameCapture() {
    if (!IsReady())
        return false;
    state_->capture.requested = true;
    return true;
}

bool GpuDevice::ReadFrameCapture(GpuFrameCapture& capture) {
    if (!IsReady() || !state_->capture.recorded || state_->capture.serial == 0)
        return false;
    auto& s = *state_;
    if (!s.WaitForSerial(s.capture.serial))
        return false;
    if (const VkResult result =
            vmaInvalidateAllocation(s.allocator, s.capture.allocation, 0, VK_WHOLE_SIZE);
        result != VK_SUCCESS)
        return s.Fail(result, "Frame capture invalidate");
    VmaAllocationInfo allocation{};
    vmaGetAllocationInfo(s.allocator, s.capture.allocation, &allocation);
    const auto* source = static_cast<const uint8*>(allocation.pMappedData);
    if (!source)
        return false;
    capture.width = s.capture.width;
    capture.height = s.capture.height;
    capture.rgba.assign(source,
                        source + static_cast<size_t>(s.capture.width) * s.capture.height * 4);
    const bool bgra =
        s.capture.format == VK_FORMAT_B8G8R8A8_UNORM || s.capture.format == VK_FORMAT_B8G8R8A8_SRGB;
    if (bgra)
        for (size_t index = 0; index < capture.rgba.size(); index += 4)
            std::swap(capture.rgba[index], capture.rgba[index + 2]);
    s.capture.recorded = false;
    return true;
}

} // namespace LuxEngine
