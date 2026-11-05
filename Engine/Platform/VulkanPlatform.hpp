#pragma once

#include "Engine/Platform/Window.hpp"
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace LuxEngine::Platform {
bool GetVulkanInstanceExtensions(const Window& window, std::vector<const char*>& extensions,
                                 std::string& error);
VkSurfaceKHR CreateVulkanSurface(const Window& window, VkInstance instance, std::string& error);
void DestroyVulkanSurface(VkInstance instance, VkSurfaceKHR surface);
std::string ExecutableDirectory();
} // namespace LuxEngine::Platform
