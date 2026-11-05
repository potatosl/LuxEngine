#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Core/Log.hpp"
#include <fstream>
#include <vector>
#include <limits>
#include <cstddef>
#include <algorithm>

namespace LuxEngine {
GpuDevice::~GpuDevice() { Shutdown(); }
bool GpuDevice::Initialize(SDL_Window* window) {
    Shutdown();
    if (!window) return false;
    window_ = window;
    const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;
#ifdef LUX_DEBUG
    constexpr bool debug = true;
#else
    constexpr bool debug = false;
#endif
    device_ = SDL_CreateGPUDevice(formats, debug, nullptr);
    if (!device_ || !SDL_ClaimWindowForGPUDevice(device_, window_)) {
        Log::Error(SDL_GetError()); Shutdown(); return false;
    }
    claimed_ = true;
    DeviceLifetime::Register(device_);
    const uint32 pixel = 0xffffffff;
    if (!white_.Initialize(device_, 1, 1, &pixel) || !CreatePipelines()) {
        Log::Error(SDL_GetError()); Shutdown(); return false;
    }
    return true;
}
bool GpuDevice::CreatePipelines() {
    const auto formats = SDL_GetGPUShaderFormats(device_);
    const auto format = (formats & SDL_GPU_SHADERFORMAT_DXIL) ? SDL_GPU_SHADERFORMAT_DXIL :
        ((formats & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_MSL);
    auto shader = [&](bool vertex) {
        const std::string extension = format == SDL_GPU_SHADERFORMAT_DXIL ? "dxil" : "spv";
        const std::string file = format == SDL_GPU_SHADERFORMAT_MSL ? "Basic.metal" : std::string("Basic.") + (vertex ? "vert." : "frag.") + extension;
        std::ifstream in(std::string(SDL_GetBasePath()) + "shaders/" + file, std::ios::binary | std::ios::ate);
        if (!in) { SDL_SetError("Shader file missing: %s", file.c_str()); return static_cast<SDL_GPUShader*>(nullptr); }
        const auto size = in.tellg();
        if (size <= 0) return static_cast<SDL_GPUShader*>(nullptr);
        std::vector<uint8> bytes(static_cast<size_t>(size) + 1, 0);
        in.seekg(0); if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return static_cast<SDL_GPUShader*>(nullptr);
        SDL_GPUShaderCreateInfo info{};
        info.code = bytes.data(); info.code_size = static_cast<size_t>(size);
        info.entrypoint = format == SDL_GPU_SHADERFORMAT_MSL ? (vertex ? "vertexMain" : "fragmentMain") : "main";
        info.format = format;
        info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_uniform_buffers = vertex ? 1 : 0;
        info.num_samplers = vertex ? 0 : 1;
        return SDL_CreateGPUShader(device_, &info);
    };
    auto* vs = shader(true); auto* fs = shader(false);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(device_, vs);
        if (fs) SDL_ReleaseGPUShader(device_, fs);
        return false;
    }
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0; buffer.pitch = sizeof(RenderVertex); buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(RenderVertex, position)};
    attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(RenderVertex, color)};
    attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(RenderVertex, uv)};
    SDL_GPUColorTargetDescription color{};
    color.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    color.blend_state.enable_blend = true;
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vs; info.fragment_shader = fs;
    info.vertex_input_state = {&buffer, 1, attributes, 3};
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
    pipeline2D_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    color.blend_state.enable_blend = false;
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipeline3D_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    SDL_ReleaseGPUShader(device_, vs); SDL_ReleaseGPUShader(device_, fs);
    return pipeline2D_ && pipeline3D_;
}
void GpuDevice::Shutdown() {
    if (currentCommandBuffer_) {
        if (currentSwapchainTexture_) SDL_SubmitGPUCommandBuffer(currentCommandBuffer_);
        else SDL_CancelGPUCommandBuffer(currentCommandBuffer_);
    }
    currentCommandBuffer_ = nullptr; currentSwapchainTexture_ = nullptr;
    if (device_) {
        SDL_WaitForGPUIdle(device_);
        vertices_.Release(); white_.Release();
        if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
        if (pipeline2D_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline2D_);
        if (pipeline3D_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline3D_);
        if (claimed_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
        DeviceLifetime::Forget(device_);
        SDL_DestroyGPUDevice(device_);
    }
    depth_ = nullptr; pipeline2D_ = nullptr; pipeline3D_ = nullptr;
    depthWidth_ = depthHeight_ = 0; claimed_ = false; device_ = nullptr; window_ = nullptr;
}
bool GpuDevice::ResizeDepth(uint32 width, uint32 height) {
    if (depth_ && depthWidth_ == width && depthHeight_ == height) return true;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = width; info.height = height; info.layer_count_or_depth = 1; info.num_levels = 1;
    auto* replacement = SDL_CreateGPUTexture(device_, &info);
    if (!replacement) return false;
    if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
    depth_ = replacement; depthWidth_ = width; depthHeight_ = height;
    return true;
}
bool GpuDevice::BeginFrame() {
    if (!IsReady() || currentCommandBuffer_) return false;
    currentCommandBuffer_ = SDL_AcquireGPUCommandBuffer(device_);
    if (!currentCommandBuffer_) return false;
    uint32 width = 0, height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(currentCommandBuffer_, window_, &currentSwapchainTexture_, &width, &height)) {
        SDL_CancelGPUCommandBuffer(currentCommandBuffer_); currentCommandBuffer_ = nullptr; return false;
    }
    if (!currentSwapchainTexture_) { EndFrame(); return false; }
    if (!ResizeDepth(width, height)) { EndFrame(); return false; }
    depthCleared_ = false;
    return true;
}
void GpuDevice::Clear(float r, float g, float b, float a) {
    if (!currentCommandBuffer_ || !currentSwapchainTexture_) return;
    SDL_GPUColorTargetInfo color{};
    color.texture = currentSwapchainTexture_; color.clear_color = {r,g,b,a};
    color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
    auto* pass = SDL_BeginGPURenderPass(currentCommandBuffer_, &color, 1, nullptr);
    if (pass) SDL_EndGPURenderPass(pass);
}
bool GpuDevice::DrawTriangles(std::span<const RenderVertex> data, const Mat4& vp, GpuTexture* texture, bool depth) {
    if (!currentCommandBuffer_ || !currentSwapchainTexture_ || data.empty() || data.size() % 3 != 0 || data.size_bytes() > std::numeric_limits<uint32>::max()) return false;
    const auto bytes = static_cast<uint32>(data.size_bytes());
    if (vertices_.GetSize() < bytes) {
        const auto capacity = static_cast<uint32>(std::min<uint64>(std::numeric_limits<uint32>::max(), std::max<uint64>(bytes,std::max<uint64>(65536,static_cast<uint64>(vertices_.GetSize())*2))));
        if (!vertices_.Initialize(device_, BufferType::Vertex, capacity)) return false;
    }
    if (!vertices_.SetData(data.data(), bytes, currentCommandBuffer_)) return false;
    if (!texture) texture = &white_;
    if (!texture->IsValid() || texture->GetDevice() != device_) return false;
    SDL_GPUColorTargetInfo color{};
    color.texture = currentSwapchainTexture_; color.load_op = SDL_GPU_LOADOP_LOAD; color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = depth_; depthInfo.clear_depth = 1.0f;
    depthInfo.load_op = depthCleared_ ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE; depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_PushGPUVertexUniformData(currentCommandBuffer_, 0, vp.elements, sizeof(vp.elements));
    auto* pass = SDL_BeginGPURenderPass(currentCommandBuffer_, &color, 1, depth ? &depthInfo : nullptr);
    if (!pass) return false;
    SDL_BindGPUGraphicsPipeline(pass, depth ? pipeline3D_ : pipeline2D_);
    SDL_GPUBufferBinding binding{vertices_.GetHandle(), 0};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_GPUTextureSamplerBinding sampler{texture->GetHandle(), texture->GetSampler()};
    SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
    SDL_DrawGPUPrimitives(pass, static_cast<uint32>(data.size()), 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    if (depth) depthCleared_ = true;
    return true;
}
void GpuDevice::EndFrame() {
    if (currentCommandBuffer_ && !SDL_SubmitGPUCommandBuffer(currentCommandBuffer_)) Log::Error(SDL_GetError());
    currentCommandBuffer_ = nullptr; currentSwapchainTexture_ = nullptr;
}
bool GpuDevice::SetVSync(bool enabled) {
    if (!IsReady() || currentCommandBuffer_) return false;
    auto mode = enabled ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!SDL_WindowSupportsGPUPresentMode(device_, window_, mode)) return false;
    return SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}
}
