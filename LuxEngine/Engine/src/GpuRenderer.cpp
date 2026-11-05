#include "LuxEngine/GpuRenderer.hpp"
#include <cmath>

namespace LuxEngine {
GpuRenderer::~GpuRenderer() { shutdown(); }

bool GpuRenderer::initialize(SDL_Window* window) {
    window_ = window;
    constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL);
    device_ = SDL_CreateGPUDevice(formats, false, nullptr);
    if (!device_) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GPU device: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "GPU swapchain: %s", SDL_GetError());
        return false;
    }
    claimed_ = true;
    SDL_Log("GPU backend: %s", SDL_GetGPUDeviceDriver(device_));
    return true;
}

bool GpuRenderer::render(float elapsedSeconds) {
    auto* command = SDL_AcquireGPUCommandBuffer(device_);
    if (!command) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Acquire command buffer: %s", SDL_GetError());
        return false;
    }
    SDL_GPUTexture* swapchainTexture = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window_, &swapchainTexture, nullptr, nullptr)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Acquire swapchain texture: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(command);
        return false;
    }
    if (swapchainTexture) {
        const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds);
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchainTexture;
        target.clear_color = SDL_FColor{0.06f + 0.12f * pulse, 0.10f, 0.17f + 0.12f * pulse, 1.0f};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(command, &target, 1, nullptr);
        if (!pass) {
            SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Begin render pass: %s", SDL_GetError());
            // Cannot cancel after a swapchain texture has been acquired.
            SDL_SubmitGPUCommandBuffer(command);
            return false;
        }
        SDL_EndGPURenderPass(pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(command)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Submit GPU commands: %s", SDL_GetError());
        return false;
    }
    return true;
}

void GpuRenderer::shutdown() {
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
}
