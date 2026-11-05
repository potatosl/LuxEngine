#include "Engine/Renderer/RHI/GpuDevice.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/Ogre/OgreBridge.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#ifndef LUX_OGRE_GL_PLUGIN_PATH
#define LUX_OGRE_GL_PLUGIN_PATH "RenderSystem_GL"
#endif

namespace LuxEngine {
namespace {
std::string ResolveOgrePluginPath() {
    if (const char* overridePath = std::getenv("LUX_OGRE_GL_PLUGIN"); overridePath && *overridePath)
        return overridePath;

    const std::string configuredPath = LUX_OGRE_GL_PLUGIN_PATH;
    const size_t separator = configuredPath.find_last_of("/\\");
    const std::string filename =
        configuredPath.substr(separator == std::string::npos ? 0 : separator + 1);
    if (const char* basePath = SDL_GetBasePath()) {
        const std::string localPath = std::string(basePath) + filename;
        SDL_PathInfo info{};
        if (SDL_GetPathInfo(localPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
            return localPath;
    }
    return configuredPath;
}
} // namespace

GpuDevice::~GpuDevice() {
    Shutdown();
}

bool GpuDevice::Initialize(SDL_Window* window) {
    if (IsReady() || !window)
        return false;
    Shutdown();

    const std::string pluginPath = ResolveOgrePluginPath();
    char error[2048]{};
    ogreContext_ = lux_ogre_create(window, pluginPath.c_str(), 1, error, sizeof(error));
    if (!ogreContext_) {
        Log::Error(std::string("Ogre 렌더러 초기화에 실패했습니다: ") + error);
        return false;
    }

    window_ = window;
    DeviceLifetime::Register(ogreContext_);
    return true;
}

void GpuDevice::Shutdown() {
    frameActive_ = false;
    if (ogreContext_) {
        DeviceLifetime::Forget(ogreContext_);
        lux_ogre_destroy(ogreContext_);
        ogreContext_ = nullptr;
    }
    window_ = nullptr;
}

GpuDevice::FrameStart GpuDevice::BeginFrame() {
    if (!IsReady() || frameActive_)
        return FrameStart::Error;

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height))
        return FrameStart::Error;
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) || width <= 0 || height <= 0)
        return FrameStart::Skipped;

    if (!lux_ogre_begin_frame(ogreContext_)) {
        Log::Error("Ogre 렌더링 프레임을 시작하지 못했습니다.");
        return FrameStart::Error;
    }
    frameActive_ = true;
    return FrameStart::Ready;
}

bool GpuDevice::Clear(float r, float g, float b, float a) {
    return frameActive_ && lux_ogre_clear(ogreContext_, r, g, b, a);
}

bool GpuDevice::DrawTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                              GpuTexture* texture, bool depth) {
    if (!frameActive_ || vertices.empty() || vertices.size() % 3 != 0 ||
        vertices.size_bytes() > std::numeric_limits<uint32>::max())
        return false;

    LuxOgreTexture* ogreTexture = nullptr;
    if (texture) {
        if (!texture->IsValid() || texture->GetOgreContext() != ogreContext_)
            return false;
        ogreTexture = texture->GetOgreTexture();
    }

    std::vector<LuxOgreVertex> converted;
    converted.reserve(vertices.size());
    for (const RenderVertex& vertex : vertices) {
        converted.push_back({vertex.position.x, vertex.position.y, vertex.position.z,
                             vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w,
                             vertex.uv.x, vertex.uv.y});
    }
    return lux_ogre_draw_triangles(ogreContext_, converted.data(), converted.size(),
                                   viewProjection.elements, ogreTexture, depth ? 1 : 0);
}

bool GpuDevice::EndFrame() {
    if (!frameActive_)
        return false;
    frameActive_ = false;
    const bool rendered = lux_ogre_end_frame(ogreContext_) != 0;
    if (!rendered)
        Log::Error("Ogre 렌더링 프레임을 완료하지 못했습니다.");
    return rendered;
}

bool GpuDevice::SetVSync(bool enabled) {
    return IsReady() && !frameActive_ && lux_ogre_set_vsync(ogreContext_, enabled ? 1 : 0);
}
} // namespace LuxEngine
