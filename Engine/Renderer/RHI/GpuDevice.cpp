#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    GpuDevice::~GpuDevice() {
        Shutdown();
    }

    bool GpuDevice::Initialize(SDL_Window* window) {
        window_ = window;
        constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL);

        device_ = SDL_CreateGPUDevice(formats, false, nullptr);
        if (!device_) {
            Log::Error(std::string("Failed to create SDL GPU Device: ") + SDL_GetError());
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
            Log::Error(std::string("Failed to claim window for GPU Device: ") + SDL_GetError());
            return false;
        }

        claimed_ = true;
        const char* driver = SDL_GetGPUDeviceDriver(device_);
        Log::Info(std::string("GPU Device created with driver backend: ") + (driver ? driver : "Unknown"));
        return true;
    }

    void GpuDevice::Shutdown() {
        if (device_) {
            if (claimed_ && window_) {
                SDL_ReleaseWindowFromGPUDevice(device_, window_);
                claimed_ = false;
            }
            SDL_DestroyGPUDevice(device_);
            device_ = nullptr;
        }
        window_ = nullptr;
    }

    bool GpuDevice::BeginFrame() {
        if (!device_ || !window_) return false;

        currentCommandBuffer_ = SDL_AcquireGPUCommandBuffer(device_);
        if (!currentCommandBuffer_) {
            Log::Error(std::string("Failed to acquire GPU command buffer: ") + SDL_GetError());
            return false;
        }

        currentSwapchainTexture_ = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(currentCommandBuffer_, window_, &currentSwapchainTexture_, nullptr, nullptr)) {
            Log::Error(std::string("Failed to acquire GPU swapchain texture: ") + SDL_GetError());
            SDL_CancelGPUCommandBuffer(currentCommandBuffer_);
            currentCommandBuffer_ = nullptr;
            return false;
        }

        return true;
    }

    void GpuDevice::Clear(float r, float g, float b, float a) {
        if (!currentCommandBuffer_ || !currentSwapchainTexture_) return;

        SDL_GPUColorTargetInfo targetInfo{};
        targetInfo.texture = currentSwapchainTexture_;
        targetInfo.clear_color = SDL_FColor{r, g, b, a};
        targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        targetInfo.store_op = SDL_GPU_STOREOP_STORE;

        auto* pass = SDL_BeginGPURenderPass(currentCommandBuffer_, &targetInfo, 1, nullptr);
        if (pass) {
            SDL_EndGPURenderPass(pass);
        }
    }

    void GpuDevice::EndFrame() {
        if (currentCommandBuffer_) {
            SDL_SubmitGPUCommandBuffer(currentCommandBuffer_);
            currentCommandBuffer_ = nullptr;
            currentSwapchainTexture_ = nullptr;
        }
    }

} // namespace LuxEngine
