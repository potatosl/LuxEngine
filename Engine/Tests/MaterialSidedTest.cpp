#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace LuxEngine;

void Check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string_view Environment(const char* name) {
    const auto* value = std::getenv(name);
    return value ? value : "";
}

bool DisplayUnavailable() {
    const auto driver = Environment("SDL_VIDEODRIVER");
    if (driver == "dummy" || driver == "offscreen")
        return true;
#if defined(__linux__)
    if (driver == "x11")
        return Environment("DISPLAY").empty();
    if (driver == "wayland")
        return Environment("WAYLAND_DISPLAY").empty();
    if (driver.empty())
        return Environment("DISPLAY").empty() && Environment("WAYLAND_DISPLAY").empty();
#endif
    return false;
}

class VideoLifetime {
public:
    VideoLifetime() {
        Check(SDL_Init(SDL_INIT_VIDEO),
              std::string("SDL initialization failed: ") + SDL_GetError());
    }

    ~VideoLifetime() {
        SDL_Quit();
    }
};

void BeginFrame(Window& window, GpuDevice& gpu) {
    const auto started = SDL_GetTicks();
    while (SDL_GetTicks() - started < 10000) {
        window.PollEvents();
        Check(window.IsRunning(), "Material test window was closed");
        const auto state = gpu.BeginFrame();
        if (state == GpuDevice::FrameStart::Ready)
            return;
        Check(state != GpuDevice::FrameStart::Error, gpu.GetLastError());
        SDL_Delay(5);
    }
    throw std::runtime_error("Material test frame initialization timed out");
}

double Capture(Window& window, GpuDevice& gpu, Renderer3D::Context& renderer, const Mesh3D& mesh,
               bool back, bool mirrored, bool doubleSided) {
    Check(gpu.RequestFrameCapture(), "Material test capture request failed");
    BeginFrame(window, gpu);
    Check(gpu.Clear(0, 0, 0, 1), "Material test clear failed");
    uint32 width = 0, height = 0;
    Check(window.GetFramebufferSize(width, height) && width && height,
          "Material test framebuffer is unavailable");
    PerspectiveCamera camera(ToRadians(60), static_cast<float>(width) / height, .1f, 20);
    camera.SetPosition({0, 0, back ? -4.f : 4.f});
    camera.SetTarget({0, 0, 0});
    Render3DSettings settings;
    settings.mode = Render3DMode::Legacy;
    settings.sun.direction = {0, 0, back ? 1.f : -1.f};
    settings.sun.intensity = .7f;
    settings.imageBasedLighting = false;
    settings.shadows = false;
    settings.showSkybox = false;
    settings.toneMapping = false;
    renderer.BeginScene(camera, settings, 1);
    Material3D material;
    material.albedoColor = {.8f, .6f, .4f, 1};
    material.alphaMode = MaterialAlphaMode::Opaque;
    material.doubleSided = doubleSided;
    renderer.DrawMesh(mesh, Mat4::Scale({mirrored ? -1.f : 1.f, 1, 1}), material);
    renderer.EndScene();
    Check(renderer.GetLastAssetError().empty(), renderer.GetLastAssetError());
    Check(gpu.EndFrame(), "Material test presentation failed: " + gpu.GetLastError());
    GpuFrameCapture pixels;
    Check(gpu.ReadFrameCapture(pixels) && pixels.width >= 32 && pixels.height >= 32 &&
              pixels.rgba.size() == static_cast<size_t>(pixels.width) * pixels.height * 4,
          "Material test readback is invalid");
    // Sample an interior patch so edge rasterization and window-system scaling do not affect
    // the comparison. Front and back lights both point straight at the visible surface.
    double brightness = 0;
    for (uint32 y = pixels.height / 2 - 8; y < pixels.height / 2 + 8; ++y)
        for (uint32 x = pixels.width / 2 - 8; x < pixels.width / 2 + 8; ++x) {
            const auto offset = (static_cast<size_t>(y) * pixels.width + x) * 4;
            brightness +=
                (pixels.rgba[offset] + pixels.rgba[offset + 1] + pixels.rgba[offset + 2]) / 3.0;
        }
    Check(gpu.GetCounters().validationErrors == 0,
          "Material sidedness rendering produced GPU validation errors");
    return brightness / 256.0;
}

int Run() {
    if (DisplayUnavailable()) {
        std::cout << "SKIP: material sidedness test requires a window-system display\n";
        return 77;
    }
    VideoLifetime video;
    Window window;
    WindowProps props;
    props.title = "LuxEngine material winding and double-sided normals";
    props.width = 256;
    props.height = 256;
    props.resizable = false;
    Check(window.Initialize(props), std::string("Window initialization failed: ") + SDL_GetError());
    GpuDevice gpu;
    GpuDeviceConfig configuration;
    configuration.vsync = false;
    configuration.validation = GpuValidation::Synchronization;
    Check(gpu.Initialize(window, configuration),
          "GPU initialization failed: " + gpu.GetLastError());
    Renderer3D::Context renderer(&gpu);
    const std::array<Vertex3D, 4> vertices{{
        {{-1, -1, 0}, {0, 0, 1}, {0, 0}},
        {{1, -1, 0}, {0, 0, 1}, {1, 0}},
        {{1, 1, 0}, {0, 0, 1}, {1, 1}},
        {{-1, 1, 0}, {0, 0, 1}, {0, 1}},
    }};
    const std::array<uint32, 6> indices{0, 1, 2, 0, 2, 3};
    Mesh3D mesh;
    Check(mesh.Upload(&gpu, vertices, indices), "Material test plane upload failed");
    const double front = Capture(window, gpu, renderer, mesh, false, false, false);
    const double culledBack = Capture(window, gpu, renderer, mesh, true, false, false);
    const double back = Capture(window, gpu, renderer, mesh, true, false, true);
    const double mirroredFront = Capture(window, gpu, renderer, mesh, false, true, false);
    const double mirroredBack = Capture(window, gpu, renderer, mesh, true, true, true);
    std::cout << "Material brightness: front=" << front << ", culled=" << culledBack
              << ", back=" << back << ", mirrored front=" << mirroredFront
              << ", mirrored back=" << mirroredBack << '\n';
    Check(front > 100 && culledBack < 3,
          "Single-sided plane did not retain front faces and cull back faces");
    for (const auto brightness : {back, mirroredFront, mirroredBack})
        Check(std::abs(brightness - front) < std::max(5.0, front * .05),
              "Mirrored winding or double-sided backface lighting differs from the front face");
    Check(gpu.WaitIdle(), "Material test GPU completion failed");
    std::cout << "Material winding and double-sided normal regression passed\n";
    return 0;
}
} // namespace

int main() {
    try {
        return Run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
