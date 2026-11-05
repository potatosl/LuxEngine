#include "Engine/Platform/VulkanPlatform.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace LuxEngine::Platform {
namespace {
bool SupportsVulkan(const Window& window, std::string& error) {
    SDL_Window* handle = window.GetNativeHandle();
    if (!handle) {
        error = "Vulkan requires a live window";
        return false;
    }
    if ((SDL_GetWindowFlags(handle) & SDL_WINDOW_VULKAN) == 0) {
        error = "Window was not created with Vulkan support";
        return false;
    }
    return true;
}
} // namespace

bool GetVulkanInstanceExtensions(const Window& window, std::vector<const char*>& extensions,
                                 std::string& error) {
    if (!SupportsVulkan(window, error))
        return false;
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names || count == 0) {
        error = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
        return false;
    }
    extensions.assign(names, names + count);
    return true;
}

VkSurfaceKHR CreateVulkanSurface(const Window& window, VkInstance instance, std::string& error) {
    if (!instance || !SupportsVulkan(window, error))
        return VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window.GetNativeHandle(), instance, nullptr, &surface)) {
        error = std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError();
        return VK_NULL_HANDLE;
    }
    return surface;
}

void DestroyVulkanSurface(VkInstance instance, VkSurfaceKHR surface) {
    if (instance && surface)
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
}

std::string ExecutableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::string(base) : std::string();
}
} // namespace LuxEngine::Platform
