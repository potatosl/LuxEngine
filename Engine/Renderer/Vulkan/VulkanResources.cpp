#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace LuxEngine::Vk {
namespace {
constexpr VkDeviceSize InitialVertexRing = 1ull << 20;
constexpr VkDeviceSize FrameUniformBytes = 2ull << 20;
constexpr size_t MaxUploadBatches = 64;
constexpr VkPipelineStageFlags ResourceReadStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
} // namespace

bool DeviceState::CreateSync() {
    if (ConsumeFault(Fault::SyncObject))
        return Fail(VK_ERROR_OUT_OF_HOST_MEMORY, "Timeline semaphore creation");
    if (!info.timelineSemaphores)
        return true;
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    VkSemaphoreCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create.pNext = &type;
    if (const VkResult result = vkCreateSemaphore(device, &create, nullptr, &timeline);
        result != VK_SUCCESS) {
        timeline = VK_NULL_HANDLE;
        return Fail(result, "Timeline semaphore creation");
    }
    return true;
}

void DeviceState::DestroySync() {
    if (!device)
        return;
    if (timeline)
        vkDestroySemaphore(device, timeline, nullptr);
    timeline = VK_NULL_HANDLE;
    for (auto& [serial, fence] : inflightFences)
        vkDestroyFence(device, fence, nullptr);
    for (VkFence fence : freeFences)
        vkDestroyFence(device, fence, nullptr);
    inflightFences.clear();
    freeFences.clear();
}

uint64 DeviceState::PollCompleted() {
    if (!device)
        return completedSerial;
    if (timeline) {
        uint64 value = 0;
        if (const VkResult result = vkGetSemaphoreCounterValue(device, timeline, &value);
            result != VK_SUCCESS)
            Fail(result, "vkGetSemaphoreCounterValue");
        else
            completedSerial = std::max(completedSerial, value);
        return completedSerial;
    }
    while (!inflightFences.empty()) {
        auto [serial, fence] = inflightFences.front();
        const VkResult result = vkGetFenceStatus(device, fence);
        if (result == VK_NOT_READY)
            break;
        if (result != VK_SUCCESS) {
            Fail(result, "vkGetFenceStatus");
            break;
        }
        vkResetFences(device, 1, &fence);
        freeFences.push_back(fence);
        inflightFences.pop_front();
        completedSerial = std::max(completedSerial, serial);
    }
    return completedSerial;
}

bool DeviceState::WaitForSerial(uint64 serial) {
    if (serial != 0 && ConsumeFault(Fault::GpuTimeout))
        return Fail(VK_TIMEOUT, "Waiting for GPU work");
    if (serial == 0 || serial <= PollCompleted())
        return true;
    if (serial > submittedSerial)
        return Fail(GpuStatus::Error, "Waiting for work that was never submitted");
    VkResult result = VK_SUCCESS;
    if (timeline) {
        VkSemaphoreWaitInfo wait{};
        wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait.semaphoreCount = 1;
        wait.pSemaphores = &timeline;
        wait.pValues = &serial;
        result = vkWaitSemaphores(device, &wait, config.gpuTimeoutNs);
    } else {
        auto it = std::find_if(inflightFences.begin(), inflightFences.end(),
                               [&](const auto& entry) { return entry.first >= serial; });
        if (it != inflightFences.end())
            result = vkWaitForFences(device, 1, &it->second, VK_TRUE, config.gpuTimeoutNs);
    }
    if (result != VK_SUCCESS)
        return Fail(result, "Waiting for GPU work");
    PollCompleted();
    return Usable();
}

bool DeviceState::WaitForPresent(FrameData& frame, uint64 timeout, bool reset) {
    if (!frame.presentPending || !frame.presentFence)
        return true;
    const auto waited = vkWaitForFences(device, 1, &frame.presentFence, VK_TRUE, timeout);
    if (waited != VK_SUCCESS)
        return Fail(waited, "Waiting for presentation resources");
    frame.presentPending = false;
    ++counters.presentFencesWaited;
    if (reset) {
        if (const auto result = vkResetFences(device, 1, &frame.presentFence);
            result != VK_SUCCESS) {
            status = GpuStatus::Error;
            return Fail(result, "Resetting presentation fence");
        }
    }
    return true;
}

bool DeviceState::WaitForPresents(uint64 timeout, bool reset) {
    bool completed = true;
    for (auto& frame : frames)
        if (!WaitForPresent(frame, timeout, reset))
            completed = false;
    return completed;
}

VkResult DeviceState::Submit(VkCommandBuffer cmd, VkSemaphore wait, VkPipelineStageFlags waitStage,
                             VkSemaphore signal, uint64& serial) {
    if (ConsumeFault(Fault::Submit))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    const uint64 value = submittedSerial + 1;
    VkSemaphore signals[2]{};
    uint64 values[2]{};
    uint32 signalCount = 0;
    if (signal)
        signals[signalCount++] = signal;
    if (timeline) {
        signals[signalCount] = timeline;
        values[signalCount++] = value;
    }
    const uint64 waitValue = 0;
    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount = wait ? 1 : 0;
    timelineInfo.pWaitSemaphoreValues = &waitValue;
    timelineInfo.signalSemaphoreValueCount = signalCount;
    timelineInfo.pSignalSemaphoreValues = values;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = timeline ? &timelineInfo : nullptr;
    submit.waitSemaphoreCount = wait ? 1 : 0;
    submit.pWaitSemaphores = &wait;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = cmd ? 1 : 0;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = signalCount;
    submit.pSignalSemaphores = signals;

    VkFence fence = VK_NULL_HANDLE;
    if (!timeline) {
        if (!freeFences.empty()) {
            fence = freeFences.back();
            freeFences.pop_back();
        } else {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (const VkResult result = vkCreateFence(device, &fenceInfo, nullptr, &fence);
                result != VK_SUCCESS)
                return result;
        }
    }
    const VkResult result = vkQueueSubmit(graphicsQueue, 1, &submit, fence);
    if (result != VK_SUCCESS) {
        if (fence)
            freeFences.push_back(fence);
        return result;
    }
    submittedSerial = value;
    serial = value;
    if (fence)
        inflightFences.emplace_back(value, fence);
    ++counters.submissions;
    ++frameStats.submissions;
    if (ConsumeFault(Fault::DeviceLost))
        return VK_ERROR_DEVICE_LOST;
    return VK_SUCCESS;
}

void DeviceState::Retire(const Retired& resource) {
    if (frameActive || uploadRecording) {
        pendingRetire.push_back(resource);
        return;
    }
    Retired stamped = resource;
    stamped.serial = submittedSerial;
    retired.push_back(stamped);
}

void DeviceState::StampPending(uint64 serial) {
    for (Retired& resource : pendingRetire) {
        resource.serial = serial;
        retired.push_back(resource);
    }
    pendingRetire.clear();
}

void DeviceState::DestroyRetired(const Retired& resource) {
    if (resource.set && resource.pool)
        vkFreeDescriptorSets(device, resource.pool, 1, &resource.set);
    if (resource.view)
        vkDestroyImageView(device, resource.view, nullptr);
    if (resource.image)
        vmaDestroyImage(allocator, resource.image, resource.allocation);
    if (resource.buffer)
        vmaDestroyBuffer(allocator, resource.buffer, resource.allocation);
    liveStagingBytes -= std::min(liveStagingBytes, static_cast<uint64>(resource.stagingBytes));
}

void DeviceState::CollectGarbage() {
    const uint64 completed = PollCompleted();
    while (!retired.empty() && retired.front().serial <= completed) {
        DestroyRetired(retired.front());
        retired.pop_front();
    }
}

VkCommandBuffer DeviceState::UploadCommands() {
    if (uploadRecording)
        return uploadRecording;
    if (!Usable())
        return VK_NULL_HANDLE;
    const uint64 completed = PollCompleted();
    UploadBatch* batch = nullptr;
    for (auto& candidate : uploadBatches)
        if (candidate.serial != UINT64_MAX && candidate.serial <= completed) {
            batch = &candidate;
            break;
        }
    if (!batch && uploadBatches.size() >= MaxUploadBatches) {
        if (!WaitForSerial(submittedSerial))
            return VK_NULL_HANDLE;
        return UploadCommands();
    }
    if (!batch) {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = uploadPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        const VkResult result = ConsumeFault(Fault::CommandBuffer)
                                    ? VK_ERROR_OUT_OF_HOST_MEMORY
                                    : vkAllocateCommandBuffers(device, &allocate, &cmd);
        if (result != VK_SUCCESS) {
            Fail(result, "vkAllocateCommandBuffers (upload)");
            return VK_NULL_HANDLE;
        }
        uploadBatches.push_back({cmd, 0});
        batch = &uploadBatches.back();
    } else if (const VkResult result = vkResetCommandBuffer(batch->cmd, 0); result != VK_SUCCESS) {
        Fail(result, "vkResetCommandBuffer (upload)");
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(batch->cmd, &begin); result != VK_SUCCESS) {
        Fail(result, "vkBeginCommandBuffer (upload)");
        return VK_NULL_HANDLE;
    }
    batch->serial = UINT64_MAX;
    uploadRecording = batch->cmd;
    return uploadRecording;
}

bool DeviceState::FlushUploads() {
    if (!uploadRecording)
        return true;
    VkCommandBuffer cmd = uploadRecording;
    uploadRecording = VK_NULL_HANDLE;
    auto batch = std::find_if(uploadBatches.begin(), uploadBatches.end(),
                              [&](const UploadBatch& entry) { return entry.cmd == cmd; });
    uint64 serial = submittedSerial;
    VkResult result = vkEndCommandBuffer(cmd);
    if (result == VK_SUCCESS)
        result = Submit(cmd, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, serial);
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        serial = submittedSerial;
    if (batch != uploadBatches.end())
        batch->serial = serial;
    for (Retired& staging : uploadStaging) {
        staging.serial = serial;
        retired.push_back(staging);
    }
    uploadStaging.clear();
    batchStagingBytes = 0;
    if (!frameActive)
        StampPending(serial);
    if (result != VK_SUCCESS)
        return Fail(result, "Upload submission");
    return true;
}

bool DeviceState::CreateStaging(const void* data, VkDeviceSize size, Retired& staging,
                                VkBuffer& buffer) {
    if (uploadRecording && batchStagingBytes + size > config.stagingFlushBytes && !FlushUploads())
        return false;
    if (liveStagingBytes + size > config.stagingFlushBytes * 4) {
        if (!WaitForSerial(submittedSerial))
            return false;
        CollectGarbage();
    }
    if (ConsumeFault(Fault::BufferAllocation))
        return Fail(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Staging buffer allocation");
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mapped{};
    if (const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                                &staging.buffer, &staging.allocation, &mapped);
        result != VK_SUCCESS) {
        staging = {};
        return Fail(result, "Staging buffer allocation");
    }
    std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
    if (const VkResult result = vmaFlushAllocation(allocator, staging.allocation, 0, size);
        result != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
        staging = {};
        return Fail(result, "Staging buffer flush");
    }
    staging.stagingBytes = size;
    liveStagingBytes += size;
    buffer = staging.buffer;
    return true;
}

bool DeviceState::CreateBufferResource(VkDeviceSize size, VkBufferUsageFlags usage,
                                       BufferRecord& record) {
    if (ConsumeFault(Fault::BufferAllocation))
        return Fail(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Buffer allocation");
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    if (const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                                &record.buffer, &record.allocation, nullptr);
        result != VK_SUCCESS) {
        record = {};
        return Fail(result, "Buffer allocation");
    }
    record.size = size;
    return true;
}

bool DeviceState::UploadBuffer(BufferRecord& record, const void* data, VkDeviceSize size,
                               VkDeviceSize offset) {
    Retired staging;
    VkBuffer source = VK_NULL_HANDLE;
    if (!CreateStaging(data, size, staging, source))
        return false;
    VkCommandBuffer cmd = UploadCommands();
    if (!cmd) {
        vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
        liveStagingBytes -= size;
        return false;
    }
    VkMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, ResourceReadStages | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
    const VkBufferCopy region{0, offset, size};
    vkCmdCopyBuffer(cmd, source, record.buffer, 1, &region);
    VkMemoryBarrier after{};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                          VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, ResourceReadStages, 0, 1, &after, 0,
                         nullptr, 0, nullptr);
    uploadStaging.push_back(staging);
    batchStagingBytes += size;
    frameStats.uploadBytes += size;
    return true;
}

bool DeviceState::CreateTextureResource(uint32 width, uint32 height, const void* pixels,
                                        TextureRecord& record, TextureFilter filter,
                                        TextureColorSpace colorSpace) {
    if (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb)
        return Fail(GpuStatus::Error, "Texture color space is invalid");
    if (filter != TextureFilter::Linear && filter != TextureFilter::Nearest)
        return Fail(GpuStatus::Error, "Texture filter is invalid");
    if (width == 0 || height == 0 || width > info.maxTextureSize || height > info.maxTextureSize)
        return Fail(GpuStatus::Error, "Texture size is zero or exceeds maxImageDimension2D");
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    if (ConsumeFault(Fault::ImageAllocation))
        return Fail(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Texture image allocation");
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format =
        colorSpace == TextureColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    if (const VkResult result = vmaCreateImage(allocator, &imageInfo, &allocationInfo,
                                               &record.image, &record.allocation, nullptr);
        result != VK_SUCCESS) {
        record = {};
        return Fail(result, "Texture image allocation");
    }
    auto destroyPartial = [&] {
        if (record.set)
            vkFreeDescriptorSets(device, record.pool, 1, &record.set);
        if (record.view)
            vkDestroyImageView(device, record.view, nullptr);
        vmaDestroyImage(allocator, record.image, record.allocation);
        record = {};
        return false;
    };
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = record.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (const VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &record.view);
        result != VK_SUCCESS) {
        record.view = VK_NULL_HANDLE;
        Fail(result, "Texture image view");
        return destroyPartial();
    }
    if (!AllocateTextureSet(record.view, record.pool, record.set, filter)) {
        record.set = VK_NULL_HANDLE;
        return destroyPartial();
    }
    Retired staging;
    VkBuffer source = VK_NULL_HANDLE;
    if (pixels && !CreateStaging(pixels, bytes, staging, source))
        return destroyPartial();
    VkCommandBuffer cmd = UploadCommands();
    if (!cmd) {
        if (staging.buffer) {
            vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
            liveStagingBytes -= bytes;
        }
        return destroyPartial();
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = record.image;
    barrier.subresourceRange = viewInfo.subresourceRange;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    if (pixels) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, source, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        uploadStaging.push_back(staging);
        batchStagingBytes += bytes;
        frameStats.uploadBytes += bytes;
    } else {
        const VkClearColorValue transparent{{0.0f, 0.0f, 0.0f, 0.0f}};
        vkCmdClearColorImage(cmd, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent,
                             1, &viewInfo.subresourceRange);
    }
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    record.width = width;
    record.height = height;
    return true;
}

bool DeviceState::EnsureVertexCapacity(FrameData& frame, VkDeviceSize bytes) {
    if (frameStats.vertexBytes + bytes > config.maxFrameVertexBytes)
        return Fail(GpuStatus::OutOfMemory, "Per-frame vertex budget exceeded");
    const VkDeviceSize needed = frame.ring.used + bytes;
    if (needed <= frame.ring.capacity)
        return true;
    const VkDeviceSize capacity = std::min<VkDeviceSize>(
        config.maxFrameVertexBytes, std::max({needed, InitialVertexRing, frame.ring.capacity * 2}));
    if (ConsumeFault(Fault::BufferAllocation))
        return Fail(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Vertex ring allocation");
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacity;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT |
                           VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
    VertexRing next;
    VmaAllocationInfo mapped{};
    if (const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                                &next.buffer, &next.allocation, &mapped);
        result != VK_SUCCESS)
        return Fail(result, "Vertex ring allocation");
    next.mapped = static_cast<uint8*>(mapped.pMappedData);
    next.capacity = capacity;
    if (frame.ring.buffer) {
        if (frame.ring.used)
            vmaFlushAllocation(allocator, frame.ring.allocation, 0, frame.ring.used);
        Retire({0, frame.ring.buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, frame.ring.allocation});
    }
    frame.ring = next;
    return true;
}

bool DeviceState::PushUniform(const void* data, VkDeviceSize size, uint32& offset) {
    VertexRing& ring = frames[frameSlot].uniforms;
    const VkDeviceSize aligned =
        (ring.used + uniformAlignment - 1) / uniformAlignment * uniformAlignment;
    if (!ring.mapped || aligned + size > ring.capacity)
        return Fail(GpuStatus::OutOfMemory, "Per-frame uniform budget exceeded");
    std::memcpy(ring.mapped + aligned, data, static_cast<size_t>(size));
    ring.used = aligned + size;
    offset = static_cast<uint32>(aligned);
    return true;
}

bool DeviceState::EnsureCaptureBuffer(VkDeviceSize size) {
    if (capture.buffer && capture.size == size)
        return true;
    if (capture.buffer) {
        Retire({0, capture.buffer, VK_NULL_HANDLE, VK_NULL_HANDLE, capture.allocation});
        capture.buffer = VK_NULL_HANDLE;
        capture.allocation = nullptr;
        capture.size = 0;
    }
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                                &capture.buffer, &capture.allocation, nullptr);
        result != VK_SUCCESS) {
        capture.buffer = VK_NULL_HANDLE;
        capture.allocation = nullptr;
        return Fail(result, "Frame capture buffer");
    }
    capture.size = size;
    return true;
}

bool DeviceState::CreateFrames() {
    uniformAlignment =
        std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 16);
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[1] = bindings[0];
    bindings[1].binding = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (const VkResult result =
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &frameSetLayout);
        result != VK_SUCCESS) {
        frameSetLayout = VK_NULL_HANDLE;
        return Fail(result, "Frame descriptor set layout");
    }
    const VkDescriptorPoolSize uniformSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                           2 * frameCount};
    VkDescriptorPoolCreateInfo framePoolInfo{};
    framePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    framePoolInfo.maxSets = frameCount;
    framePoolInfo.poolSizeCount = 1;
    framePoolInfo.pPoolSizes = &uniformSize;
    if (const VkResult result = vkCreateDescriptorPool(device, &framePoolInfo, nullptr, &framePool);
        result != VK_SUCCESS) {
        framePool = VK_NULL_HANDLE;
        return Fail(result, "Frame descriptor pool");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = info.graphicsQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    for (uint32 index = 0; index < frameCount; ++index) {
        FrameData& frame = frames[index];
        if (ConsumeFault(Fault::CommandBuffer))
            return Fail(VK_ERROR_OUT_OF_HOST_MEMORY, "Frame command pool");
        if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool);
            result != VK_SUCCESS) {
            frame.pool = VK_NULL_HANDLE;
            return Fail(result, "Frame command pool");
        }
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = frame.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (const VkResult result = vkAllocateCommandBuffers(device, &allocate, &frame.cmd);
            result != VK_SUCCESS)
            return Fail(result, "Frame command buffer");
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (ConsumeFault(Fault::SyncObject))
            return Fail(VK_ERROR_OUT_OF_HOST_MEMORY, "Image-available semaphore");
        if (const VkResult result =
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable);
            result != VK_SUCCESS) {
            frame.imageAvailable = VK_NULL_HANDLE;
            return Fail(result, "Image-available semaphore");
        }
        if (info.presentFences) {
            if (ConsumeFault(Fault::PresentFenceAllocation))
                return Fail(VK_ERROR_OUT_OF_HOST_MEMORY, "Presentation fence allocation");
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (const auto result = vkCreateFence(device, &fenceInfo, nullptr, &frame.presentFence);
                result != VK_SUCCESS) {
                frame.presentFence = VK_NULL_HANDLE;
                return Fail(result, "Presentation fence allocation");
            }
        }
        if (ConsumeFault(Fault::BufferAllocation))
            return Fail(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Frame uniform buffer");
        VkBufferCreateInfo uniformInfo{};
        uniformInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uniformInfo.size = FrameUniformBytes;
        uniformInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo uniformAllocation{};
        uniformAllocation.usage = VMA_MEMORY_USAGE_AUTO;
        uniformAllocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapped{};
        if (const VkResult result =
                vmaCreateBuffer(allocator, &uniformInfo, &uniformAllocation, &frame.uniforms.buffer,
                                &frame.uniforms.allocation, &mapped);
            result != VK_SUCCESS) {
            frame.uniforms = {};
            return Fail(result, "Frame uniform buffer");
        }
        frame.uniforms.mapped = static_cast<uint8*>(mapped.pMappedData);
        frame.uniforms.capacity = FrameUniformBytes;
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = framePool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &frameSetLayout;
        if (const VkResult result = vkAllocateDescriptorSets(device, &setInfo, &frame.uniformSet);
            result != VK_SUCCESS)
            return Fail(result, "Frame descriptor set");
        const VkDescriptorBufferInfo objectRange{frame.uniforms.buffer, 0, UniformObjectBytes};
        const VkDescriptorBufferInfo lightRange{frame.uniforms.buffer, 0, UniformLightBytes};
        VkWriteDescriptorSet writes[2]{};
        for (uint32 binding = 0; binding < 2; ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = frame.uniformSet;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[binding].pBufferInfo = binding == 0 ? &objectRange : &lightRange;
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        if (info.gpuTimestamps) {
            VkQueryPoolCreateInfo queryInfo{};
            queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = 2;
            if (vkCreateQueryPool(device, &queryInfo, nullptr, &frame.queries) != VK_SUCCESS)
                frame.queries = VK_NULL_HANDLE;
        }
    }
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &uploadPool);
        result != VK_SUCCESS) {
        uploadPool = VK_NULL_HANDLE;
        return Fail(result, "Upload command pool");
    }
    return true;
}

void DeviceState::DestroyFrames() {
    if (!device)
        return;
    for (FrameData& frame : frames) {
        if (frame.ring.buffer)
            vmaDestroyBuffer(allocator, frame.ring.buffer, frame.ring.allocation);
        if (frame.uniforms.buffer)
            vmaDestroyBuffer(allocator, frame.uniforms.buffer, frame.uniforms.allocation);
        if (frame.queries)
            vkDestroyQueryPool(device, frame.queries, nullptr);
        if (frame.imageAvailable)
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        if (frame.presentFence)
            vkDestroyFence(device, frame.presentFence, nullptr);
        if (frame.pool)
            vkDestroyCommandPool(device, frame.pool, nullptr);
        frame = {};
    }
    if (uploadPool)
        vkDestroyCommandPool(device, uploadPool, nullptr);
    uploadPool = VK_NULL_HANDLE;
    uploadBatches.clear();
    uploadRecording = VK_NULL_HANDLE;
    if (framePool)
        vkDestroyDescriptorPool(device, framePool, nullptr);
    if (frameSetLayout)
        vkDestroyDescriptorSetLayout(device, frameSetLayout, nullptr);
    framePool = VK_NULL_HANDLE;
    frameSetLayout = VK_NULL_HANDLE;
}

void DeviceState::Shutdown() {
    if (device) {
        if (uploadRecording) {
            vkEndCommandBuffer(uploadRecording);
            uploadRecording = VK_NULL_HANDLE;
        }
        vkDeviceWaitIdle(device);
        // Queue/device idle does not cover the presentation engine's references. Only present
        // fences prove these semaphores and the swapchain are safe to release. A truly lost
        // device makes vkWaitForFences return DEVICE_LOST, which also permits lost cleanup.
        while (!WaitForPresents(UINT64_MAX, false) && status != GpuStatus::DeviceLost) {
            // An unexpected host-allocation failure is not completion evidence. Retry rather
            // than release presentation payloads that the implementation may still reference.
        }
        for (auto& frame : frames)
            frame.retained.clear();
        for (const Retired& resource : uploadStaging)
            DestroyRetired(resource);
        for (const Retired& resource : pendingRetire)
            DestroyRetired(resource);
        for (const Retired& resource : retired)
            DestroyRetired(resource);
        uploadStaging.clear();
        pendingRetire.clear();
        retired.clear();
        buffers.Drain([&](BufferRecord& record) {
            vmaDestroyBuffer(allocator, record.buffer, record.allocation);
        });
        textures.Drain([&](TextureRecord& record) {
            DestroyRetired({0, VK_NULL_HANDLE, record.image, record.view, record.allocation,
                            record.pool, record.set});
        });
        if (capture.buffer)
            vmaDestroyBuffer(allocator, capture.buffer, capture.allocation);
        capture = {};
        DestroyFrames();
        DestroySwapchain(swapchain);
        DestroyPipelines();
        if (sampler)
            vkDestroySampler(device, sampler, nullptr);
        if (nearestSampler)
            vkDestroySampler(device, nearestSampler, nullptr);
        if (setLayout)
            vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        if (pipelineLayout)
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (pipelineCache)
            vkDestroyPipelineCache(device, pipelineCache, nullptr);
        for (VkDescriptorPool pool : descriptorPools)
            vkDestroyDescriptorPool(device, pool, nullptr);
        sampler = nearestSampler = VK_NULL_HANDLE;
        setLayout = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        pipelineCache = VK_NULL_HANDLE;
        descriptorPools.clear();
        DestroySync();
    }
    DestroyContext();
    frameActive = renderPassActive = false;
    whiteTexture = 0;
    liveStagingBytes = batchStagingBytes = 0;
    status = GpuStatus::Uninitialized;
}

} // namespace LuxEngine::Vk
