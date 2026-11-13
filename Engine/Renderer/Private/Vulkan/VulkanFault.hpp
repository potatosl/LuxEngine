#pragma once
#include <LuxEngine/Testing/GpuFault.hpp>

namespace LuxEngine::Vk {
#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
bool ConsumeFault(Fault fault);
#else
constexpr bool ConsumeFault(Fault) {
    return false;
}
#endif
} // namespace LuxEngine::Vk
