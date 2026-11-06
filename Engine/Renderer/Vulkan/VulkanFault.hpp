#pragma once

#include "Engine/Core/Core.hpp"

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
    Count
};

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
LUX_API void InjectFault(Fault fault, uint32 count = 1, uint32 skip = 0);
LUX_API void ClearFaults();
LUX_API uint32 TriggeredFaults(Fault fault);
bool ConsumeFault(Fault fault);
#else
constexpr bool ConsumeFault(Fault) {
    return false;
}
#endif

} // namespace LuxEngine::Vk
