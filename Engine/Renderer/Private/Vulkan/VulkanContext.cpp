#include "Vulkan/VulkanDevice.hpp"
#include "Vulkan/VulkanPlatform.hpp"
#include <LuxEngine/Core/Log.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>

namespace LuxEngine::Vk {
struct InstanceOwner {
    VkInstance handle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    Ref<InstanceValidation> validation = CreateRef<InstanceValidation>();
    ~InstanceOwner() {
        if (messenger) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(handle, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy)
                destroy(handle, messenger, nullptr);
        }
        if (handle)
            vkDestroyInstance(handle, nullptr);
    }
};

// NVIDIA Wayland WSI stores callbacks at process scope. Destroying one VkInstance while
// another surface is live invalidates those callbacks (SDL issue #12053).
// Reuse matching instances, and release
// the bounded set of configuration variants only after the last device has released its lease.
struct InstancePool {
    std::mutex mutex;
    size_t active = 0;
    std::vector<std::pair<std::string, std::shared_ptr<InstanceOwner>>> instances;
};
struct InstanceLease {
    std::shared_ptr<InstancePool> pool;
    std::shared_ptr<InstanceOwner> owner;
    // Acquired while pool->mutex is held; constructing the complete lease is non-throwing.
    InstanceLease(std::shared_ptr<InstancePool> sharedPool,
                  std::shared_ptr<InstanceOwner> sharedOwner)
        : pool(std::move(sharedPool)), owner(std::move(sharedOwner)) {
        ++pool->active;
    }
    InstanceLease(const InstanceLease&) = delete;
    InstanceLease& operator=(const InstanceLease&) = delete;
    ~InstanceLease() {
        std::lock_guard lock(pool->mutex);
        owner.reset();
        if (--pool->active == 0)
            pool->instances.clear();
    }
};
namespace {
std::shared_ptr<InstancePool> SharedInstances() {
    static auto pool = std::make_shared<InstancePool>();
    return pool;
}
std::string InstanceKey(const VkInstanceCreateInfo& create, bool syncValidation) {
    std::ostringstream key;
    key << create.pApplicationInfo->apiVersion << ':' << create.flags << ':' << syncValidation;
    for (uint32 i = 0; i < create.enabledExtensionCount; ++i)
        key << ':' << create.ppEnabledExtensionNames[i];
    for (uint32 i = 0; i < create.enabledLayerCount; ++i)
        key << ':' << create.ppEnabledLayerNames[i];
    return key.str();
}

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
struct FaultSlot {
    uint32 skip = 0;
    uint32 count = 0;
    uint32 triggered = 0;
};
std::mutex faultMutex;
std::array<FaultSlot, static_cast<size_t>(Fault::Count)> faults;
#endif

bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::vector<VkExtensionProperties> InstanceExtensions(const char* layer) {
    uint32 count = 0;
    if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS)
        return {};
    std::vector<VkExtensionProperties> result(count);
    if (vkEnumerateInstanceExtensionProperties(layer, &count, result.data()) != VK_SUCCESS)
        return {};
    result.resize(count);
    return result;
}

std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice device) {
    uint32 count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return {};
    std::vector<VkExtensionProperties> result(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, result.data()) != VK_SUCCESS)
        return {};
    result.resize(count);
    return result;
}

bool LayerAvailable(const char* name) {
    uint32 count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;
    return std::any_of(layers.begin(), layers.begin() + count, [&](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

GpuValidation ResolveValidation(GpuValidation requested) {
    if (const char* env = std::getenv("LUX_VULKAN_VALIDATION"); env && *env) {
        const std::string_view value(env);
        if (value == "0" || value == "off")
            return GpuValidation::Off;
        if (value == "sync")
            return GpuValidation::Synchronization;
        return GpuValidation::On;
    }
    if (requested != GpuValidation::Auto)
        return requested;
#ifdef LUX_DEBUG
    return GpuValidation::On;
#else
    return GpuValidation::Off;
#endif
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* user) {
    auto* counters = static_cast<InstanceValidation*>(user);
    const char* message = data && data->pMessage ? data->pMessage : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        if (counters)
            ++counters->errors;
        Log::Error(std::string("[Vulkan] ") + message);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        if (counters)
            ++counters->warnings;
        Log::Warn(std::string("[Vulkan] ") + message);
    }
    return VK_FALSE;
}

const char* DeviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    uint32 graphics = UINT32_MAX;
    uint32 present = UINT32_MAX;
    uint32 compute = UINT32_MAX;
    uint32 transfer = UINT32_MAX;
    int64 score = -1;
};
} // namespace

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
InstanceLifetimeCounters GetInstanceLifetimeCounters() {
    auto pool = SharedInstances();
    std::lock_guard lock(pool->mutex);
    return {static_cast<uint32>(pool->instances.size()), static_cast<uint32>(pool->active)};
}
void InjectInstanceValidationCounters(uint32 errors, uint32 warnings) {
    auto pool = SharedInstances();
    std::lock_guard lock(pool->mutex);
    for (const auto& entry : pool->instances) {
        entry.second->validation->errors += errors;
        entry.second->validation->warnings += warnings;
    }
}
void InjectFault(Fault fault, uint32 count, uint32 skip) {
    std::lock_guard lock(faultMutex);
    faults[static_cast<size_t>(fault)] = {skip, count, 0};
}
void ClearFaults() {
    std::lock_guard lock(faultMutex);
    faults = {};
}
uint32 TriggeredFaults(Fault fault) {
    std::lock_guard lock(faultMutex);
    return faults[static_cast<size_t>(fault)].triggered;
}
bool ConsumeFault(Fault fault) {
    std::lock_guard lock(faultMutex);
    auto& slot = faults[static_cast<size_t>(fault)];
    if (slot.count == 0)
        return false;
    if (slot.skip > 0) {
        --slot.skip;
        return false;
    }
    --slot.count;
    ++slot.triggered;
    return true;
}
#endif

const char* ResultString(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INVALID_SHADER_NV:
        return "VK_ERROR_INVALID_SHADER_NV";
    default:
        return "VK_ERROR_UNKNOWN";
    }
}

bool DeviceState::Fail(GpuStatus failure, std::string_view what) {
    lastFailure = failure;
    lastError.assign(what);
    if (failure == GpuStatus::DeviceLost || failure == GpuStatus::SurfaceLost)
        status = failure;
    Log::Error(std::string("[GPU] ") + lastError);
    return false;
}

bool DeviceState::Fail(VkResult result, std::string_view what) {
    GpuStatus failure = GpuStatus::Error;
    switch (result) {
    case VK_ERROR_DEVICE_LOST:
        failure = GpuStatus::DeviceLost;
        break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    case VK_ERROR_OUT_OF_POOL_MEMORY:
    case VK_ERROR_FRAGMENTED_POOL:
        failure = GpuStatus::OutOfMemory;
        break;
    case VK_ERROR_SURFACE_LOST_KHR:
        failure = GpuStatus::SurfaceLost;
        break;
    case VK_TIMEOUT:
        failure = GpuStatus::Timeout;
        break;
    default:
        break;
    }
    return Fail(failure, std::string(what) + ": " + ResultString(result));
}

bool DeviceState::CreateInstance() {
    if (ConsumeFault(Fault::InstanceCreation))
        return Fail(VK_ERROR_INITIALIZATION_FAILED, "vkCreateInstance");
    uint32 loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS ||
        loaderVersion < VK_API_VERSION_1_1)
        return Fail(VK_ERROR_INCOMPATIBLE_DRIVER, "Vulkan 1.1 loader is required");
    apiVersion = std::min<uint32>(loaderVersion, VK_API_VERSION_1_3);

    std::vector<const char*> extensions;
    std::string error;
    if (!Platform::GetVulkanInstanceExtensions(*window, extensions, error))
        return Fail(GpuStatus::Error, error);
    const auto available = InstanceExtensions(nullptr);
    for (const char* name : extensions)
        if (!HasExtension(available, name))
            return Fail(VK_ERROR_EXTENSION_NOT_PRESENT,
                        std::string("Required instance extension ") + name);

#if defined(VK_EXT_surface_maintenance1) && defined(VK_EXT_swapchain_maintenance1)
    // Present fences require both instance extensions, in addition to the device feature.
    if (HasExtension(available, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) &&
        HasExtension(available, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) {
        for (const char* name : {VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
                                 VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME})
            if (std::none_of(extensions.begin(), extensions.end(), [name](const char* existing) {
                    return std::strcmp(existing, name) == 0;
                }))
                extensions.push_back(name);
        surfaceMaintenance1 = true;
    }
#endif

    const GpuValidation validation = ResolveValidation(config.validation);
    std::vector<const char*> layers;
    bool debugUtils = false;
    bool syncValidation = false;
    if (validation != GpuValidation::Off) {
        constexpr const char* layer = "VK_LAYER_KHRONOS_validation";
        if (LayerAvailable(layer)) {
            layers.push_back(layer);
            const auto layerExtensions = InstanceExtensions(layer);
            syncValidation =
                validation == GpuValidation::Synchronization &&
                HasExtension(layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            if (syncValidation)
                extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            if (validation == GpuValidation::Synchronization && !syncValidation)
                Log::Warn("Vulkan synchronization validation is unavailable");
        } else {
            Log::Warn("Vulkan validation requested but VK_LAYER_KHRONOS_validation is missing");
        }
        if (HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugUtils = true;
        }
    }
    if (HasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        portabilityEnumeration = true;
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "LuxEngine";
    application.pEngineName = "LuxEngine";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.apiVersion = apiVersion;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = DebugCallback;
    // Callback state is owned by the shared instance, never by an individual DeviceState.
    messengerInfo.pUserData = nullptr;

    const VkValidationFeatureEnableEXT syncFeature =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features{};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = &syncFeature;

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    create.enabledExtensionCount = static_cast<uint32>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    create.enabledLayerCount = static_cast<uint32>(layers.size());
    create.ppEnabledLayerNames = layers.data();
    if (portabilityEnumeration)
        create.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    const void** next = &create.pNext;
    if (debugUtils) {
        *next = &messengerInfo;
        next = const_cast<const void**>(&messengerInfo.pNext);
    }
    if (syncValidation)
        *next = &features;

    {
        auto pool = SharedInstances();
        std::lock_guard lock(pool->mutex);
        const auto key = InstanceKey(create, syncValidation);
        auto found = std::find_if(pool->instances.begin(), pool->instances.end(),
                                  [&](const auto& entry) { return entry.first == key; });
        std::shared_ptr<InstanceOwner> owner;
        if (found != pool->instances.end()) {
            owner = found->second;
        } else {
            owner = std::make_shared<InstanceOwner>();
            messengerInfo.pUserData = owner->validation.get();
            if (const VkResult result = vkCreateInstance(&create, nullptr, &owner->handle);
                result != VK_SUCCESS) {
                owner->handle = VK_NULL_HANDLE;
                return Fail(result, "vkCreateInstance");
            }
            if (debugUtils) {
                messengerInfo.pNext = nullptr;
                auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(owner->handle, "vkCreateDebugUtilsMessengerEXT"));
                if (!createMessenger || createMessenger(owner->handle, &messengerInfo, nullptr,
                                                        &owner->messenger) != VK_SUCCESS) {
                    owner->messenger = VK_NULL_HANDLE;
                    Log::Warn("Vulkan debug messenger could not be created");
                }
            }
            pool->instances.emplace_back(key, owner);
        }
        instanceLease = std::make_shared<InstanceLease>(pool, std::move(owner));
        instance = instanceLease->owner->handle;
        instanceValidation = instanceLease->owner->validation;
        validationErrorsAtInitialization = instanceValidation->errors.load();
        validationWarningsAtInitialization = instanceValidation->warnings.load();
    }
    info.validation = !layers.empty();
    info.synchronizationValidation = syncValidation;

    if (ConsumeFault(Fault::SurfaceCreation))
        return Fail(VK_ERROR_INITIALIZATION_FAILED, "Vulkan surface creation");
    surface = Platform::CreateVulkanSurface(*window, instance, error);
    if (!surface)
        return Fail(GpuStatus::Error, error);
    return true;
}

bool DeviceState::SelectPhysicalDevice() {
    if (ConsumeFault(Fault::DeviceSelection))
        return Fail(GpuStatus::Error, "No suitable Vulkan device (injected)");
    uint32 count = 0;
    if (const VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        result != VK_SUCCESS)
        return Fail(result, "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(count);
    if (count == 0 || vkEnumeratePhysicalDevices(instance, &count, devices.data()) < VK_SUCCESS)
        return Fail(GpuStatus::Error, "No Vulkan physical devices are available");

    std::string preferred = config.preferredDevice;
    if (const char* env = std::getenv("LUX_VULKAN_DEVICE"); env && *env)
        preferred = env;
    Candidate best;
    std::string rejected;
    for (VkPhysicalDevice physDevice : devices) {
        Candidate candidate;
        candidate.device = physDevice;
        vkGetPhysicalDeviceProperties(physDevice, &candidate.properties);
        const std::string name = candidate.properties.deviceName;
        auto reject = [&](const char* reason) { rejected += " [" + name + ": " + reason + "]"; };
        if (candidate.properties.apiVersion < VK_API_VERSION_1_1) {
            reject("Vulkan 1.1 unsupported");
            continue;
        }
        const auto extensions = DeviceExtensions(physDevice);
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            reject("no VK_KHR_swapchain");
            continue;
        }
        uint32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &familyCount, families.data());
        for (uint32 index = 0; index < familyCount; ++index) {
            const auto flags = families[index].queueFlags;
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, index, surface, &present) !=
                VK_SUCCESS)
                present = VK_FALSE;
            const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool shared =
                candidate.graphics != UINT32_MAX && candidate.graphics == candidate.present;
            if (graphics && present && !shared) {
                candidate.graphics = index;
                candidate.present = index;
            } else {
                if (graphics && candidate.graphics == UINT32_MAX)
                    candidate.graphics = index;
                if (present && candidate.present == UINT32_MAX)
                    candidate.present = index;
            }
            if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT))
                candidate.compute = index;
            if ((flags & VK_QUEUE_TRANSFER_BIT) &&
                !(flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
                candidate.transfer = index;
        }
        if (candidate.graphics == UINT32_MAX) {
            reject("no graphics queue");
            continue;
        }
        if (candidate.present == UINT32_MAX) {
            reject("cannot present to the window surface");
            continue;
        }
        if (candidate.compute == UINT32_MAX)
            candidate.compute = candidate.graphics;
        if (candidate.transfer == UINT32_MAX)
            candidate.transfer = candidate.graphics;
        uint32 formatCount = 0, modeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, surface, &modeCount, nullptr);
        if (formatCount == 0 || modeCount == 0) {
            reject("surface has no formats or present modes");
            continue;
        }
        switch (candidate.properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            candidate.score = 1000;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            candidate.score = 500;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            candidate.score = 200;
            break;
        default:
            candidate.score = 100;
            break;
        }
        if (candidate.graphics == candidate.present)
            candidate.score += 50;
        if (!preferred.empty() && name.find(preferred) != std::string::npos)
            candidate.score += 100000;
        if (candidate.score > best.score)
            best = candidate;
    }
    if (!best.device)
        return Fail(GpuStatus::Error, "No suitable Vulkan device:" + rejected);

    physical = best.device;
    properties = best.properties;
    apiVersion = std::min(apiVersion, properties.apiVersion);
    timestampPeriod = properties.limits.timestampPeriod;
    info.deviceName = properties.deviceName;
    info.deviceType = DeviceTypeName(properties.deviceType);
    info.apiVersion = apiVersion;
    info.driverVersion = properties.driverVersion;
    info.graphicsQueueFamily = best.graphics;
    info.presentQueueFamily = best.present;
    info.computeQueueFamily = best.compute;
    info.transferQueueFamily = best.transfer;
    info.maxTextureSize = properties.limits.maxImageDimension2D;
    uint32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    info.gpuTimestamps = properties.limits.timestampComputeAndGraphics &&
                         families[best.graphics].timestampValidBits > 0 && timestampPeriod > 0;
    return true;
}

bool DeviceState::CreateDevice() {
    if (ConsumeFault(Fault::DeviceCreation))
        return Fail(VK_ERROR_INITIALIZATION_FAILED, "vkCreateDevice");
    const auto available = DeviceExtensions(physical);
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (HasExtension(available, "VK_KHR_portability_subset")) {
        extensions.push_back("VK_KHR_portability_subset");
        portabilitySubset = true;
    }
    info.memoryBudget = HasExtension(available, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (info.memoryBudget)
        extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 supported{};
    supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (apiVersion >= VK_API_VERSION_1_2)
        supported.pNext = &supported12;
#if defined(VK_EXT_surface_maintenance1) && defined(VK_EXT_swapchain_maintenance1)
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT supportedPresent{};
    supportedPresent.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    const bool presentExtension =
        surfaceMaintenance1 &&
        HasExtension(available, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    if (presentExtension) {
        supportedPresent.pNext = supported.pNext;
        supported.pNext = &supportedPresent;
    }
#endif
    vkGetPhysicalDeviceFeatures2(physical, &supported);

    VkPhysicalDeviceVulkan12Features enabled12{};
    enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabled12.timelineSemaphore = supported12.timelineSemaphore;
    info.timelineSemaphores = apiVersion >= VK_API_VERSION_1_2 && supported12.timelineSemaphore;
#if defined(VK_EXT_surface_maintenance1) && defined(VK_EXT_swapchain_maintenance1)
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabledPresent{};
    enabledPresent.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    const bool disablePresent = ConsumeFault(Fault::PresentFencesUnsupported);
    info.presentFences =
        presentExtension && supportedPresent.swapchainMaintenance1 && !disablePresent;
    if (info.presentFences) {
        extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        enabledPresent.swapchainMaintenance1 = VK_TRUE;
    }
#endif

    const float priority = 1.0f;
    std::set<uint32> families{info.graphicsQueueFamily, info.presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32 family : families) {
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queues.push_back(queue);
    }
    VkDeviceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create.pNext = info.timelineSemaphores ? &enabled12 : nullptr;
#if defined(VK_EXT_surface_maintenance1) && defined(VK_EXT_swapchain_maintenance1)
    if (info.presentFences) {
        enabledPresent.pNext = info.timelineSemaphores ? &enabled12 : nullptr;
        create.pNext = &enabledPresent;
    }
#endif
    create.queueCreateInfoCount = static_cast<uint32>(queues.size());
    create.pQueueCreateInfos = queues.data();
    create.enabledExtensionCount = static_cast<uint32>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    if (const VkResult result = vkCreateDevice(physical, &create, nullptr, &device);
        result != VK_SUCCESS) {
        device = VK_NULL_HANDLE;
        return Fail(result, "vkCreateDevice");
    }
    vkGetDeviceQueue(device, info.graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, info.presentQueueFamily, 0, &presentQueue);

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physical;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = apiVersion;
    if (info.memoryBudget)
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    if (const VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
        result != VK_SUCCESS) {
        allocator = nullptr;
        return Fail(result, "vmaCreateAllocator");
    }
    return true;
}

void DeviceState::DestroyContext() {
    if (allocator)
        vmaDestroyAllocator(allocator);
    allocator = nullptr;
    if (device)
        vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    graphicsQueue = presentQueue = VK_NULL_HANDLE;
    if (surface)
        Platform::DestroyVulkanSurface(instance, surface);
    surface = VK_NULL_HANDLE;
    instanceLease.reset();
    instance = VK_NULL_HANDLE;
    physical = VK_NULL_HANDLE;
}

} // namespace LuxEngine::Vk
