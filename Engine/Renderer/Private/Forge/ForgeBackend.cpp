#include "ForgeBackend.hpp"

#include "LuxForgeApi.h"
#include <LuxEngine/Environment/EnvironmentMap.hpp>
#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

namespace LuxEngine::Forge {

struct ForgeLibrary {
    SDL_SharedObject* module = nullptr;
#if defined(__APPLE__)
    SDL_MetalView metalView = nullptr;
#endif
    const LuxForgeApi* api = nullptr;
    LuxForgeDevice* device = nullptr;

    ~ForgeLibrary() {
        if (api && device)
            api->destroy(device);
#if defined(__APPLE__)
        if (metalView)
            SDL_Metal_DestroyView(metalView);
#endif
        if (module)
            SDL_UnloadObject(module);
    }
};

ForgeBackend::ForgeBackend() = default;

ForgeBackend::~ForgeBackend() {
    Shutdown();
}

bool ForgeBackend::Fail(GpuStatus status, const std::string& message) {
    lastFailure_ = status;
    lastError_ = message;
    if (status == GpuStatus::DeviceLost || status == GpuStatus::Error)
        status_ = status;
    return false;
}

bool ForgeBackend::Reject(const std::string& message) {
    lastError_ = message;
    return false;
}

bool ForgeBackend::ForgeFailure() {
    const std::string message =
        lib_ ? lib_->api->lastError(lib_->device) : "Forge backend not running";
    if (lib_ && lib_->api->deviceLost(lib_->device))
        return Fail(GpuStatus::DeviceLost, message);
    return Reject(message);
}

bool ForgeBackend::Initialize(Window& window, const GpuDeviceConfig& config) {
    Shutdown();
    auto lib = std::make_unique<ForgeLibrary>();
#if defined(_WIN32)
    constexpr const char* libraryName = "LuxForge.dll";
#elif defined(__APPLE__)
    constexpr const char* libraryName = "libLuxForge.dylib";
#else
    constexpr const char* libraryName = "libLuxForge.so";
#endif
    const char* base = SDL_GetBasePath();
    if (!base)
        return Fail(GpuStatus::Error, "The executable directory is unavailable");
    const std::string libraryPath = std::string(base) + libraryName;
    lib->module = SDL_LoadObject(libraryPath.c_str());
    if (!lib->module)
        return Fail(GpuStatus::Error,
                    "The Forge renderer could not be loaded: " + std::string(SDL_GetError()));
    const auto getApi =
        reinterpret_cast<LuxForgeGetApiFn>(SDL_LoadFunction(lib->module, "LuxForgeGetApi"));
    lib->api = getApi ? getApi() : nullptr;
    if (!lib->api || lib->api->version != LUX_FORGE_API_VERSION)
        return Fail(GpuStatus::Error, "The Forge renderer does not match this engine build");
    SDL_Window* sdl = window.GetNativeHandle();
    if (!sdl)
        return Fail(GpuStatus::Error, "The rendering window is unavailable");
    const auto properties = SDL_GetWindowProperties(sdl);
    LuxForgeInitDesc desc = {};
#if defined(_WIN32)
    desc.windowType = LUX_FORGE_WINDOW_WIN32;
    desc.nativeWindow =
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    (void)properties;
    lib->metalView = SDL_Metal_CreateView(sdl);
    desc.windowType = LUX_FORGE_WINDOW_METAL;
    desc.nativeWindow = lib->metalView;
#else
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "wayland") == 0) {
        desc.windowType = LUX_FORGE_WINDOW_WAYLAND;
        desc.nativeDisplay =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        desc.nativeWindow =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        desc.windowType = LUX_FORGE_WINDOW_X11;
        desc.nativeDisplay =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        desc.nativeWindowId = static_cast<uint64_t>(
            SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
#endif
    uint32 width = 0, height = 0;
    window.GetFramebufferSize(width, height);

    desc.width = width;
    desc.height = height;
    desc.framesInFlight = config.framesInFlight ? config.framesInFlight : 2;
    desc.vsync = config.vsync ? 1 : 0;
    desc.validation = config.validation == GpuValidation::Off ? 0 : 1;
    desc.maxFrameVertexBytes = config.maxFrameVertexBytes;
    char error[512] = {};
    lib->device = lib->api->create(&desc, error, sizeof(error));
    if (!lib->device)
        return Fail(GpuStatus::Error, std::string("The Forge failed to start: ") + error);

    LuxForgeInfo forgeInfo = {};
    lib->api->getInfo(lib->device, &forgeInfo);
    info_ = {};
    info_.deviceName = forgeInfo.deviceName;
    info_.deviceType = std::string("The Forge / ") + forgeInfo.apiName;
    info_.framesInFlight = forgeInfo.framesInFlight;
    info_.validation = forgeInfo.validation != 0;
    info_.apiVersion = forgeInfo.apiVersion;
    info_.driverVersion = forgeInfo.driverVersion;
    info_.maxTextureSize = forgeInfo.maxTextureSize;
    info_.gpuTimestamps = forgeInfo.gpuTimestamps != 0;

    lib_ = std::move(lib);
    window_ = &window;
    status_ = GpuStatus::Ready;
    lastError_.clear();
    frameStats_ = {};
    counters_ = {};
    return true;
}

void ForgeBackend::Shutdown() {
    textures_.clear();
    buffers_.clear();
    lib_.reset();
    window_ = nullptr;
    frameOpen_ = false;
    resizePending_ = false;
    if (status_ != GpuStatus::Uninitialized)
        status_ = GpuStatus::Uninitialized;
}

bool ForgeBackend::Usable() const {
    return lib_ && lib_->device && status_ == GpuStatus::Ready;
}

GpuStatus ForgeBackend::GetStatus() const {
    return status_;
}

GpuStatus ForgeBackend::GetLastFailure() const {
    return lastFailure_;
}

const std::string& ForgeBackend::GetLastError() const {
    return lastError_;
}

const GpuDeviceInfo& ForgeBackend::GetInfo() const {
    return info_;
}

GpuFrameStats ForgeBackend::GetFrameStats() const {
    return frameStats_;
}

void ForgeBackend::UpdateFrameStats() {
    LuxForgeStats stats = {};
    lib_->api->stats(lib_->device, &stats);
    frameStats_.drawCalls = stats.drawCalls;
    frameStats_.pipelineBinds = stats.pipelineBinds;
    frameStats_.descriptorBinds = stats.descriptorBinds;
    frameStats_.uniformBinds = stats.uniformBinds;
    frameStats_.vertexBytes = stats.vertexBytes;
}

GpuDeviceCounters ForgeBackend::GetCounters() const {
    return counters_;
}

GpuMemoryStats ForgeBackend::GetMemoryStats() const {
    GpuMemoryStats memory;
    if (lib_ && lib_->device) {
        LuxForgeStats stats = {};
        lib_->api->stats(lib_->device, &stats);
        memory.liveTextures = stats.liveTextures;
        memory.pendingDestructions = stats.pendingDeletes;
        memory.allocationBytes = stats.sceneBytes + stats.textureBytes;
    }
    return memory;
}

GpuDevice::FrameStart ForgeBackend::BeginFrame() {
    if (!Usable())
        return GpuDevice::FrameStart::Error;
    if (frameOpen_) {
        Reject("BeginFrame called while a frame is open");
        return GpuDevice::FrameStart::Error;
    }
    // DXGI flip-model swap chains never report "out of date", so follow the window size here.
    if (window_) {
        uint32 width = 0, height = 0;
        window_->GetFramebufferSize(width, height);
        if (width == 0 || height == 0 || window_->IsMinimized()) {
            ++counters_.framesSkipped;
            return GpuDevice::FrameStart::Skipped;
        }
        uint32_t currentWidth = 0, currentHeight = 0;
        lib_->api->size(lib_->device, &currentWidth, &currentHeight);
        if (resizePending_ || width != currentWidth || height != currentHeight) {
            if (!lib_->api->resize(lib_->device, width, height)) {
                ForgeFailure();
                return GpuDevice::FrameStart::Error;
            }
            if (width != currentWidth || height != currentHeight)
                ++counters_.swapchainRecreations;
            resizePending_ = false;
        }
    }
    const int start = lib_->api->beginFrame(lib_->device);
    if (start == LUX_FORGE_FRAME_ERROR) {
        ForgeFailure();
        return GpuDevice::FrameStart::Error;
    }
    if (start == LUX_FORGE_FRAME_SKIPPED) {
        ++counters_.framesSkipped;
        return GpuDevice::FrameStart::Skipped;
    }
    frameOpen_ = true;
    const uint64 index = frameStats_.frameIndex + 1;
    frameStats_ = {};
    frameStats_.frameIndex = index;
    return GpuDevice::FrameStart::Ready;
}

bool ForgeBackend::Clear(float r, float g, float b, float a) {
    if (!Usable())
        return false;
    if (!frameOpen_)
        return Reject("Clear called outside a frame");
    return lib_->api->clear(lib_->device, r, g, b, a) != 0 || ForgeFailure();
}

static_assert(sizeof(RenderVertex) == sizeof(LuxForgeVertex),
              "RenderVertex must match LuxForgeVertex");

bool ForgeBackend::DrawTexturedTriangles(std::span<const RenderVertex> vertices,
                                         const Mat4& viewProjection, uint64 texture, bool depth) {
    if (!Usable())
        return false;
    if (!frameOpen_)
        return Reject("DrawTriangles called outside a frame");
    if (vertices.empty() || vertices.size() % 3 != 0 || vertices.size() > 0xFFFFFFFFu)
        return Reject("DrawTriangles needs a non-empty triangle list");
    if (texture && !textures_.count(texture))
        return Reject("DrawTriangles with a texture that is not live");
    if (!lib_->api->drawTriangles(lib_->device,
                                  reinterpret_cast<const LuxForgeVertex*>(vertices.data()),
                                  static_cast<uint32_t>(vertices.size()), viewProjection.elements,
                                  static_cast<uint32_t>(texture), depth ? 1 : 0))
        return ForgeFailure();
    UpdateFrameStats();
    return true;
}

bool ForgeBackend::SetLighting(const GpuLighting& lighting) {
    if (!Usable())
        return false;
    if (!frameOpen_)
        return Reject("SetLighting called outside a frame");
    LuxForgePointLight points[16] = {};
    const size_t count = std::min<size_t>(lighting.points.size(), 16);
    for (size_t i = 0; i < count; ++i) {
        const GpuPointLight& p = lighting.points[i];
        points[i] = {{p.position.x, p.position.y, p.position.z},
                     p.radius,
                     {p.color.x, p.color.y, p.color.z},
                     p.intensity};
    }
    LuxForgeLighting desc = {};
    desc.direction[0] = lighting.direction.x, desc.direction[1] = lighting.direction.y,
    desc.direction[2] = lighting.direction.z;
    desc.color[0] = lighting.color.x, desc.color[1] = lighting.color.y,
    desc.color[2] = lighting.color.z;
    desc.intensity = lighting.intensity;
    desc.camera[0] = lighting.camera.x, desc.camera[1] = lighting.camera.y,
    desc.camera[2] = lighting.camera.z;
    desc.pbr = lighting.pbr ? 1 : 0;
    desc.pointCount = static_cast<uint32_t>(count);
    desc.points = points;
    return lib_->api->setLighting(lib_->device, &desc) != 0 || ForgeFailure();
}

bool ForgeBackend::DrawIndexed(uint64 vertexBuffer, uint64 indexBuffer, uint32 indexCount,
                               const Mat4& viewProjection, const Mat4& model, const Vec4& albedo,
                               uint64 texture, float metallic, float roughness, bool pbr) {
    if (!Usable())
        return false;
    if (!frameOpen_)
        return Reject("DrawIndexed called outside a frame");
    if (!buffers_.count(vertexBuffer) || !buffers_.count(indexBuffer))
        return Reject("DrawIndexed with a buffer that is not live");
    if (texture && !textures_.count(texture))
        return Reject("DrawIndexed with a texture that is not live");
    const float colour[4] = {albedo.x, albedo.y, albedo.z, albedo.w};
    if (!lib_->api->drawIndexed(lib_->device, static_cast<uint32_t>(vertexBuffer),
                                static_cast<uint32_t>(indexBuffer), indexCount,
                                viewProjection.elements, model.elements, colour,
                                static_cast<uint32_t>(texture), metallic, roughness, pbr ? 1 : 0))
        return ForgeFailure();
    UpdateFrameStats();
    return true;
}

bool ForgeBackend::EndFrame() {
    if (!Usable())
        return false;
    if (!frameOpen_)
        return Reject("EndFrame called without BeginFrame");
    frameOpen_ = false;
    if (!lib_->api->endFrame(lib_->device))
        return ForgeFailure();
    UpdateFrameStats();
    ++counters_.framesSubmitted;
    ++counters_.submissions;
    frameStats_.submissions = 1;
    return true;
}

bool ForgeBackend::SetVSync(bool enabled) {
    if (!Usable())
        return false;
    if (frameOpen_)
        return Reject("SetVSync called during a frame");
    return lib_->api->setVSync(lib_->device, enabled ? 1 : 0) != 0 || ForgeFailure();
}

void ForgeBackend::NotifyResize() {
    resizePending_ = true;
}

bool ForgeBackend::FlushUploads() {
    return Usable();
}

bool ForgeBackend::WaitIdle() {
    return Usable() && (lib_->api->waitIdle(lib_->device) != 0 || ForgeFailure());
}

bool ForgeBackend::RequestFrameCapture() {
    return Usable() && lib_->api->requestCapture(lib_->device) != 0;
}

bool ForgeBackend::ReadFrameCapture(GpuFrameCapture& capture) {
    if (!Usable())
        return false;
    uint32_t width = 0, height = 0;
    size_t size = 0;
    if (!lib_->api->readCapture(lib_->device, &width, &height, nullptr, 0, &size))
        return ForgeFailure();
    capture.rgba.resize(size);
    if (!lib_->api->readCapture(lib_->device, &width, &height, capture.rgba.data(), size, &size))
        return ForgeFailure();
    capture.width = width;
    capture.height = height;
    return true;
}

bool ForgeBackend::CreateBuffer(BufferType type, uint32 sizeBytes, uint64& handle) {
    if (!Usable())
        return false;
    int forgeType = LUX_FORGE_BUFFER_VERTEX;
    switch (type) {
    case BufferType::Vertex:
        forgeType = LUX_FORGE_BUFFER_VERTEX;
        break;
    case BufferType::Index:
        forgeType = LUX_FORGE_BUFFER_INDEX;
        break;
    case BufferType::Uniform:
        forgeType = LUX_FORGE_BUFFER_UNIFORM;
        break;
    case BufferType::Storage:
        forgeType = LUX_FORGE_BUFFER_STORAGE;
        break;
    }
    const uint32_t id = lib_->api->createBuffer(lib_->device, forgeType, sizeBytes);
    if (!id)
        return ForgeFailure();
    handle = id;
    buffers_.insert(handle);
    return true;
}

bool ForgeBackend::UpdateBuffer(uint64 handle, const void* data, uint32 sizeBytes,
                                uint32 offsetBytes) {
    if (!Usable())
        return false;
    if (!buffers_.count(handle))
        return Reject("UpdateBuffer on a buffer that is not live");
    return lib_->api->updateBuffer(lib_->device, static_cast<uint32_t>(handle), data, sizeBytes,
                                   offsetBytes) != 0 ||
           ForgeFailure();
}

void ForgeBackend::DestroyBuffer(uint64 handle) {
    if (!lib_ || !buffers_.erase(handle))
        return;
    lib_->api->destroyBuffer(lib_->device, static_cast<uint32_t>(handle));
}

bool ForgeBackend::HasBuffer(uint64 handle) const {
    return buffers_.count(handle) != 0;
}

bool ForgeBackend::CreateTexture(uint32 width, uint32 height, const void* pixelData,
                                 TextureFilter filter, TextureColorSpace colorSpace,
                                 uint32 mipLevels, TextureSampling, uint64& handle) {
    if (!Usable())
        return false;
    const uint32_t id = lib_->api->createTexture(
        lib_->device, width, height, pixelData, filter == TextureFilter::Nearest ? 1 : 0,
        colorSpace == TextureColorSpace::Srgb ? 1 : 0, mipLevels);
    if (!id)
        return ForgeFailure();
    handle = id;
    textures_.insert(handle);
    return true;
}

void ForgeBackend::DestroyTexture(uint64 handle) {
    if (!lib_ || !textures_.erase(handle))
        return;
    lib_->api->destroyTexture(lib_->device, static_cast<uint32_t>(handle));
}

bool ForgeBackend::HasTexture(uint64 handle) const {
    return textures_.count(handle) != 0;
}

namespace {
// Background preparation of an environment map. The worker thread owns the job through a
// shared_ptr, so a renderer that goes away just abandons it and never waits (issue #38).
struct EnvironmentJob {
    std::mutex mutex;
    bool done = false;
    std::shared_ptr<EnvironmentData> data;
};

LuxForgeEnvironmentImage ToForge(const EnvironmentImage& image) {
    return {image.size, image.levels, image.layers, image.pixels.data(),
            static_cast<uint64_t>(image.pixels.size())};
}

void Copy4(float* out, const Vec4& v) {
    out[0] = v.x, out[1] = v.y, out[2] = v.z, out[3] = v.w;
}
} // namespace

// Port of Vk::Scene3DRenderer onto LuxForge.dll's renderScene: the engine computes the scene
// constants exactly like the Vulkan backend, the DLL records the passes.
class ForgeScene3DRenderer final : public Rhi::Scene3DRenderer {
public:
    explicit ForgeScene3DRenderer(ForgeBackend& backend)
        : backend_(&backend), library_(backend.lib_) {}

    ~ForgeScene3DRenderer() override {
        if (auto lib = Library()) {
            lib->api->destroyEnvironment(lib->device, environment_);
            lib->api->destroyEnvironment(lib->device, neutral_);
        }
    }

    bool AddDraw(const Rhi::SceneDrawDesc& draw) override {
        if (!Library() || !backend_->buffers_.count(draw.vertexBuffer) ||
            !backend_->buffers_.count(draw.indexBuffer) ||
            (draw.texture && !backend_->textures_.count(draw.texture)))
            return false;
        for (const auto handle : {draw.normalTexture, draw.metallicTexture, draw.roughnessTexture,
                                  draw.occlusionTexture})
            if (handle && !backend_->textures_.count(handle))
                return false;
        LuxForgeSceneDraw d = {};
        d.vertexBuffer = static_cast<uint32_t>(draw.vertexBuffer);
        d.indexBuffer = static_cast<uint32_t>(draw.indexBuffer);
        d.texture = static_cast<uint32_t>(draw.texture);
        d.indexCount = draw.indexCount;
        std::memcpy(d.model, draw.model.elements, sizeof(d.model));
        Copy4(d.albedo, draw.albedo);
        d.metallic = draw.metallic;
        d.roughness = draw.roughness;
        d.alphaMode = draw.alphaMode;
        d.alphaCutoff = draw.alphaCutoff;
        d.doubleSided = draw.doubleSided ? 1 : 0;
        d.legacy = draw.legacy ? 1 : 0;
        d.normalTexture = static_cast<uint32_t>(draw.normalTexture);
        d.metallicTexture = static_cast<uint32_t>(draw.metallicTexture);
        d.roughnessTexture = static_cast<uint32_t>(draw.roughnessTexture);
        d.occlusionTexture = static_cast<uint32_t>(draw.occlusionTexture);
        d.normalScale = draw.normalScale;
        d.occlusionStrength = draw.occlusionStrength;
        d.sampling = draw.sampling;
        draws_.push_back(d);
        return true;
    }

    size_t DrawCount() const override {
        return draws_.size();
    }

    void ClearDraws() override {
        draws_.clear();
    }

    bool EnvironmentReady() const override {
        return ready_ && activePath_ == attemptedPath_ && activeRevision_ == attemptedRevision_ &&
               !job_;
    }

    double EnvironmentPreparationMs() const override {
        return preparationMs_;
    }

    bool Render(const PerspectiveCamera& camera, const Render3DSettings& settings, float exposure,
                std::string& error) override {
        auto lib = Library();
        if (!lib || !backend_->Usable() || !backend_->frameOpen_)
            return false;
        const LuxForgeApi& api = *lib->api;
        if (!neutral_ && !(neutral_ = Upload(*lib, *EnvironmentData::Neutral()))) {
            error = std::string("3D scene environment failed: ") + api.lastError(lib->device);
            return Fallback(camera, exposure);
        }
        PollEnvironment(*lib, settings);
        const bool active = ready_ && activePath_ == settings.environmentPath;

        LuxForgeScene scene = {};
        std::memcpy(scene.viewProjection, camera.GetViewProjectionMatrix().elements,
                    sizeof(scene.viewProjection));
        const auto position = camera.GetPosition();
        Copy4(scene.camera, {position.x, position.y, position.z, exposure});
        Copy4(scene.environment, {settings.environmentColor.x, settings.environmentColor.y,
                                  settings.environmentColor.z, settings.environmentIntensity});
        scene.options[0] = settings.environmentRotation * std::numbers::pi_v<float> / 180;
        scene.options[1] = settings.imageBasedLighting && active ? 1.f : 0.f;
        scene.options[2] = settings.toneMapping ? 1.f : 0.f;
        scene.options[3] = settings.showSkybox && active ? 1.f : 0.f;
        const auto& view = camera.GetViewMatrix().elements;
        const auto& projection = camera.GetProjectionMatrix().elements;
        for (int i = 0; i < 3; ++i) {
            scene.right[i] = view[i * 4] / projection[0];
            scene.up[i] = view[i * 4 + 1] / projection[5];
            scene.forward[i] = -view[i * 4 + 2];
        }
        if (settings.shadows) {
            const auto direction = settings.sun.direction.Normalized();
            const auto target = camera.GetTarget();
            auto lightView =
                Mat4::LookAt(target - direction * (settings.shadowRange * 2), target,
                             std::abs(direction.y) > .99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
            const float texel = settings.shadowRange / settings.shadowResolution;
            lightView.elements[12] = std::round(lightView.elements[12] / texel) * texel;
            lightView.elements[13] = std::round(lightView.elements[13] / texel) * texel;
            const float half = settings.shadowRange * .5f;
            const Mat4 shadowMatrix =
                Mat4::Orthographic(-half, half, -half, half, .1f, settings.shadowRange * 4) *
                lightView;
            std::memcpy(scene.shadowMatrix, shadowMatrix.elements, sizeof(scene.shadowMatrix));
            scene.shadowOptions[0] = 1;
            scene.shadowOptions[1] = 1.f / settings.shadowResolution;
            scene.shadowOptions[2] = settings.shadowDepthBias;
            scene.shadowOptions[3] = settings.shadowNormalBias;
            scene.shadowResolution = settings.shadowResolution;
        }
        scene.environmentMap = active ? environment_ : neutral_;

        uint32_t drawn = 0;
        if (!api.renderScene(lib->device, &scene, draws_.data(),
                             static_cast<uint32_t>(draws_.size()), &drawn)) {
            const std::string reason = api.lastError(lib->device);
            if (api.deviceLost(lib->device)) {
                backend_->ForgeFailure();
                error = reason;
                return false;
            }
            error = reason;
            return Fallback(camera, exposure);
        }
        backend_->UpdateFrameStats();
        if (drawn < draws_.size())
            error = "3D object budget reached; excess objects skipped";
        if (!environmentError_.empty())
            error += (error.empty() ? "" : "; ") + environmentError_;
        return true;
    }

private:
    std::shared_ptr<ForgeLibrary> Library() const {
        auto lib = library_.lock();
        return lib && lib->device ? lib : nullptr;
    }

    uint32_t Upload(ForgeLibrary& lib, const EnvironmentData& data) {
        const LuxForgeEnvironmentImage images[4] = {ToForge(data.sky), ToForge(data.diffuse),
                                                    ToForge(data.specular), ToForge(data.brdf)};
        return lib.api->createEnvironment(lib.device, images);
    }

    void PollEnvironment(ForgeLibrary& lib, const Render3DSettings& settings) {
        const auto revision = EnvironmentData::Revision(settings.environmentPath);
        if (job_) {
            std::shared_ptr<EnvironmentData> data;
            {
                std::lock_guard lock(job_->mutex);
                if (job_->done)
                    data = job_->data;
            }
            if (data) {
                job_.reset();
                if (requestedPath_ == settings.environmentPath && requestedRevision_ == revision) {
                    if (!data->error.empty())
                        environmentError_ = data->error;
                    else if (const uint32_t uploaded = Upload(lib, *data)) {
                        lib.api->destroyEnvironment(lib.device, environment_);
                        environment_ = uploaded;
                        activePath_ = requestedPath_;
                        activeRevision_ = requestedRevision_;
                        ready_ = true;
                        preparationMs_ = data->preparationMs;
                        environmentError_.clear();
                    } else
                        environmentError_ = std::string("Environment upload failed: ") +
                                            lib.api->lastError(lib.device);
                }
            }
        }
        if ((settings.imageBasedLighting || settings.showSkybox) && !job_ &&
            (!hasAttempt_ || attemptedPath_ != settings.environmentPath ||
             attemptedRevision_ != revision)) {
            requestedPath_ = attemptedPath_ = settings.environmentPath;
            requestedRevision_ = attemptedRevision_ = revision;
            hasAttempt_ = true;
            auto job = std::make_shared<EnvironmentJob>();
            try {
                std::thread([job, path = requestedPath_] {
                    auto prepared = EnvironmentData::Prepare(path);
                    std::lock_guard lock(job->mutex);
                    job->data = std::move(prepared);
                    job->done = true;
                }).detach();
                job_ = std::move(job);
            } catch (const std::exception& e) {
                environmentError_ =
                    std::string("Environment preparation could not start: ") + e.what();
            }
        }
    }

    // Same fallback as the Vulkan backend: the meshes with the plain lit pipeline.
    bool Fallback(const PerspectiveCamera& camera, float exposure) {
        const Mat4 viewProjection = camera.GetViewProjectionMatrix();
        for (const auto& d : draws_) {
            Mat4 model;
            std::memcpy(model.elements, d.model, sizeof(d.model));
            const Vec4 albedo(d.albedo[0] * exposure, d.albedo[1] * exposure,
                              d.albedo[2] * exposure, d.albedo[3]);
            if (!backend_->DrawIndexed(d.vertexBuffer, d.indexBuffer, d.indexCount, viewProjection,
                                       model, albedo, d.texture, 0, .5f, false))
                break;
        }
        return false;
    }

    ForgeBackend* backend_;
    std::weak_ptr<ForgeLibrary> library_;
    std::vector<LuxForgeSceneDraw> draws_;
    uint32_t environment_ = 0, neutral_ = 0;
    std::shared_ptr<EnvironmentJob> job_;
    std::string requestedPath_, attemptedPath_, activePath_;
    uint64 requestedRevision_ = UINT64_MAX, attemptedRevision_ = UINT64_MAX,
           activeRevision_ = UINT64_MAX;
    bool hasAttempt_ = false, ready_ = false;
    double preparationMs_ = 0;
    std::string environmentError_;
};

std::unique_ptr<Rhi::Scene3DRenderer> ForgeBackend::CreateScene3DRenderer() {
    if (!Usable())
        return nullptr;
    return std::make_unique<ForgeScene3DRenderer>(*this);
}

} // namespace LuxEngine::Forge
