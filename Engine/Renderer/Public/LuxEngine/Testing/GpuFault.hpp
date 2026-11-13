#pragma once

#include <LuxEngine/Core/Core.hpp>

namespace LuxEngine::Vk {

enum class Fault : uint32 {
    InstanceCreation,
    DeviceSelection,
    DeviceCreation,
    SurfaceCreation,
    SwapchainCreation,
    BufferAllocation,
    ImageAllocation,
    ShaderModule,
    PipelineCreation,
    CommandBuffer,
    SyncObject,
    Submit,
    DeviceLost,
    AcquireOutOfDate,
    SurfaceZeroExtent,
    GpuTimeout,
    PresentFencesUnsupported,
    PresentFenceAllocation,
    PresentOutOfDate,
    PresentOutOfHostMemory,
    Count
};

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
struct InstanceLifetimeCounters {
    uint32 instances = 0;
    uint32 devices = 0;
};

LUX_API InstanceLifetimeCounters GetInstanceLifetimeCounters();
// Test counter accounting without issuing invalid Vulkan commands or polluting validation logs.
LUX_API void InjectInstanceValidationCounters(uint32 errors, uint32 warnings);
LUX_API void InjectFault(Fault fault, uint32 count = 1, uint32 skip = 0);
LUX_API void ClearFaults();
LUX_API uint32 TriggeredFaults(Fault fault);
#endif

} // namespace LuxEngine::Vk
