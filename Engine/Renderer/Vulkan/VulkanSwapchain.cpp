#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <algorithm>

namespace LuxEngine::Vk {
namespace {
VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
        for (const auto& format : formats)
            if (format.format == preferred &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return format;
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return formats.front();
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (vsync)
        return VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
        if (std::find(modes.begin(), modes.end(), preferred) != modes.end())
            return preferred;
    return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace

void DeviceState::DestroySwapchain(Swapchain& target) {
    if (!device)
        return;
    for (VkFramebuffer framebuffer : target.framebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    for (VkImageView view : target.views)
        vkDestroyImageView(device, view, nullptr);
    for (VkSemaphore semaphore : target.renderFinished)
        vkDestroySemaphore(device, semaphore, nullptr);
    if (target.depthView)
        vkDestroyImageView(device, target.depthView, nullptr);
    if (target.depth)
        vmaDestroyImage(allocator, target.depth, target.depthAllocation);
    if (target.handle)
        vkDestroySwapchainKHR(device, target.handle, nullptr);
    target = {};
}

VkFormat DeviceState::SurfaceFormat() {
    uint32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
    return formats.empty() ? VK_FORMAT_UNDEFINED : ChooseFormat(formats).format;
}

SwapchainResult DeviceState::CreateSwapchain() {
    uint32 pixelWidth = 0, pixelHeight = 0;
    if (!window || window->IsMinimized() || !window->GetFramebufferSize(pixelWidth, pixelHeight) ||
        pixelWidth == 0 || pixelHeight == 0)
        return SwapchainResult::Minimized;
    if (ConsumeFault(Fault::SwapchainCreation)) {
        Fail(VK_ERROR_INITIALIZATION_FAILED, "vkCreateSwapchainKHR");
        return SwapchainResult::Failed;
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        result != VK_SUCCESS) {
        Fail(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        return SwapchainResult::Failed;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(pixelWidth, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height =
            std::clamp(pixelHeight, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ConsumeFault(Fault::SurfaceZeroExtent))
        extent = {0, 0};
    if (extent.width == 0 || extent.height == 0)
        return SwapchainResult::Minimized;

    uint32 count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
    if (formats.empty() || modes.empty()) {
        Fail(GpuStatus::Error, "Surface reports no formats or present modes");
        return SwapchainResult::Failed;
    }
    const VkSurfaceFormatKHR format = ChooseFormat(formats);

    uint32 imageCount = std::max(caps.minImageCount + 1, frameCount);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR candidate :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        if (caps.supportedCompositeAlpha & candidate) {
            alpha = candidate;
            break;
        }

    Swapchain next;
    next.format = format.format;
    next.colorSpace = format.colorSpace;
    next.extent = extent;
    next.presentMode = ChoosePresentMode(modes, vsync);
    next.transferSource = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    const uint32 families[2] = {info.graphicsQueueFamily, info.presentQueueFamily};
    VkSwapchainCreateInfoKHR create{};
    create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create.surface = surface;
    create.minImageCount = imageCount;
    create.imageFormat = format.format;
    create.imageColorSpace = format.colorSpace;
    create.imageExtent = extent;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        (next.transferSource ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    if (families[0] != families[1]) {
        create.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create.queueFamilyIndexCount = 2;
        create.pQueueFamilyIndices = families;
    } else {
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    create.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                              ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                              : caps.currentTransform;
    create.compositeAlpha = alpha;
    create.presentMode = next.presentMode;
    create.clipped = VK_TRUE;
    create.oldSwapchain = swapchain.handle;
    const VkResult created = vkCreateSwapchainKHR(device, &create, nullptr, &next.handle);
    DestroySwapchain(swapchain);
    if (created != VK_SUCCESS) {
        next.handle = VK_NULL_HANDLE;
        Fail(created, "vkCreateSwapchainKHR");
        return SwapchainResult::Failed;
    }

    auto failed = [&](VkResult result, const char* what) {
        DestroySwapchain(next);
        Fail(result, what);
        return SwapchainResult::Failed;
    };
    if (next.format != renderPassFormat) {
        DestroyPipelines();
        if (!CreateRenderPass(next.format) || !CreatePipelines()) {
            DestroySwapchain(next);
            return SwapchainResult::Failed;
        }
    }

    vkGetSwapchainImagesKHR(device, next.handle, &count, nullptr);
    next.images.resize(count);
    if (const VkResult result =
            vkGetSwapchainImagesKHR(device, next.handle, &count, next.images.data());
        result != VK_SUCCESS)
        return failed(result, "vkGetSwapchainImagesKHR");

    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = depthFormat;
    depthInfo.extent = {extent.width, extent.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VmaAllocationCreateInfo depthAllocation{};
    depthAllocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    depthAllocation.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    if (ConsumeFault(Fault::ImageAllocation))
        return failed(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Depth image allocation");
    if (const VkResult result = vmaCreateImage(allocator, &depthInfo, &depthAllocation, &next.depth,
                                               &next.depthAllocation, nullptr);
        result != VK_SUCCESS) {
        next.depth = VK_NULL_HANDLE;
        return failed(result, "Depth image allocation");
    }
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depthFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    view.image = next.depth;
    if (const VkResult result = vkCreateImageView(device, &view, nullptr, &next.depthView);
        result != VK_SUCCESS) {
        next.depthView = VK_NULL_HANDLE;
        return failed(result, "Depth image view");
    }

    view.format = next.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkImage image : next.images) {
        view.image = image;
        VkImageView colorView = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateImageView(device, &view, nullptr, &colorView);
            result != VK_SUCCESS)
            return failed(result, "Swapchain image view");
        next.views.push_back(colorView);
        const VkImageView attachments[2] = {colorView, next.depthView};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (const VkResult result =
                vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);
            result != VK_SUCCESS)
            return failed(result, "Swapchain framebuffer");
        next.framebuffers.push_back(framebuffer);
        VkSemaphore finished = VK_NULL_HANDLE;
        if (ConsumeFault(Fault::SyncObject))
            return failed(VK_ERROR_OUT_OF_HOST_MEMORY, "Render-finished semaphore");
        if (const VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &finished);
            result != VK_SUCCESS)
            return failed(result, "Render-finished semaphore");
        next.renderFinished.push_back(finished);
    }
    swapchain = std::move(next);
    swapchainDirty = false;
    return SwapchainResult::Ready;
}

bool DeviceState::RecreateSwapchain() {
    if (const VkResult result = vkDeviceWaitIdle(device); result != VK_SUCCESS)
        return Fail(result, "vkDeviceWaitIdle before swapchain recreation");
    if (!WaitForPresents(config.gpuTimeoutNs))
        return false;
    PollCompleted();
    CollectGarbage();
    const SwapchainResult result = CreateSwapchain();
    if (result == SwapchainResult::Ready)
        ++counters.swapchainRecreations;
    return result != SwapchainResult::Failed;
}

} // namespace LuxEngine::Vk
