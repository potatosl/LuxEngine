#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
namespace le = LuxEngine;
namespace fs = std::filesystem;

constexpr le::Vec3 Background{.16f, .17f, .19f};

void Require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string Utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string_view Environment(const char* name) {
    const char* value = std::getenv(name);
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

struct VideoLifetime {
    VideoLifetime() {
        const bool initialized = SDL_Init(SDL_INIT_VIDEO);
        Require(initialized, std::string("SDL video initialization failed: ") + SDL_GetError());
    }

    ~VideoLifetime() {
        SDL_Quit();
    }
};

struct AssetLifetime {
    AssetLifetime() {
        le::AssetManager::Init();
    }

    ~AssetLifetime() {
        le::AssetManager::Shutdown();
    }
};

void BeginFrame(le::Window& window, le::GpuDevice& gpu) {
    const auto start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 10000) {
        window.PollEvents();
        Require(window.IsRunning(), "The FBX gallery window closed during rendering");
        const auto state = gpu.BeginFrame();
        if (state == le::GpuDevice::FrameStart::Ready)
            return;
        Require(state != le::GpuDevice::FrameStart::Error,
                "GPU frame initialization failed: " + gpu.GetLastError());
        SDL_Delay(5);
    }
    throw std::runtime_error("GPU frame initialization timed out: " + gpu.GetLastError());
}

struct GalleryModel {
    std::string id;
    std::string path;
    le::Vec3 minimum;
    le::Vec3 maximum;
    le::Mat4 transform;
};

std::vector<GalleryModel> LoadGallery(const fs::path& library) {
    const auto descriptors = le::BasicModelLibrary::GetModels();
    Require(descriptors.size() == 10, "The basic FBX gallery must contain all ten catalog models");
    std::vector<GalleryModel> models;
    for (size_t index = 0; index < descriptors.size(); ++index) {
        const auto& descriptor = descriptors[index];
        const auto path = Utf8(library / descriptor.fileName);
        le::RawMeshData mesh;
        const bool loaded = le::AssetManager::LoadMesh(path, mesh);
        Require(loaded,
                "FBX import failed for " + path + ": " + le::AssetManager::GetAssetError(path));
        Require(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.indices.size() % 3 == 0 &&
                    mesh.normals.size() == mesh.vertices.size() && mesh.hasTexCoords &&
                    mesh.texCoords.size() == mesh.vertices.size() / 3 * 2,
                "FBX vertex attributes are incomplete for " + path);
        const auto extent = mesh.boundsMax - mesh.boundsMin;
        const float largest = std::max({extent.x, extent.y, extent.z});
        Require(std::isfinite(largest) && largest > .0001f, "FBX bounds are invalid for " + path);
        // Normalize only the gallery transform. The original CC0 files stay byte-identical.
        const float scale = 1.6f / largest;
        const float x = (static_cast<float>(index % 5) - 2.0f) * 2.6f;
        const float z = index < 5 ? -2.2f : 2.2f;
        const le::Vec3 origin{-(mesh.boundsMin.x + mesh.boundsMax.x) * .5f, -mesh.boundsMin.y,
                              -(mesh.boundsMin.z + mesh.boundsMax.z) * .5f};
        const auto transform = le::Mat4::Translate({x, 0, z}) *
                               le::Mat4::RotateY(le::ToRadians(-20)) *
                               le::Mat4::Scale({scale, scale, scale}) * le::Mat4::Translate(origin);
        models.push_back({descriptor.id, path, mesh.boundsMin, mesh.boundsMax, transform});
    }
    return models;
}

le::GpuFrameCapture Capture(le::Window& window, le::GpuDevice& gpu,
                            le::Renderer3D::Context& renderer, const le::PerspectiveCamera& camera,
                            const std::vector<GalleryModel>& models, const std::string& texture,
                            bool drawModels) {
    Require(gpu.RequestFrameCapture(), "GPU frame capture request failed");
    BeginFrame(window, gpu);
    Require(gpu.Clear(Background.x, Background.y, Background.z, 1), "Gallery clear failed");
    if (drawModels) {
        le::Render3DSettings settings;
        settings.mode = le::Render3DMode::Pbr;
        settings.sun.direction = {-.45f, -1.0f, -.7f};
        settings.sun.intensity = 2.0f;
        settings.environmentColor = {.35f, .35f, .35f};
        settings.imageBasedLighting = false;
        settings.shadows = false;
        settings.showSkybox = false;
        renderer.BeginScene(camera, settings, 1);
        le::Material3D material;
        material.roughness = .8f;
        material.baseColorTexture = texture;
        for (const auto& model : models) {
            const bool drawn = renderer.DrawMeshAsset(model.path, model.transform, material);
            Require(drawn, "FBX draw failed for " + model.id + ": " + renderer.GetLastAssetError());
        }
        renderer.EndScene();
        const auto stats = renderer.GetStats();
        Require(stats.meshCount == models.size() && stats.drawCalls == models.size(),
                "The gallery did not submit one mesh draw for every FBX model");
        Require(renderer.GetLastAssetError().empty(),
                "Gallery asset error: " + renderer.GetLastAssetError());
    }
    const bool presented = gpu.EndFrame();
    Require(presented, "Gallery presentation failed: " + gpu.GetLastError());
    le::GpuFrameCapture capture;
    Require(gpu.ReadFrameCapture(capture) && capture.width > 0 && capture.height > 0 &&
                capture.rgba.size() == static_cast<size_t>(capture.width) * capture.height * 4,
            "Gallery frame readback failed");
    if (drawModels)
        Require(gpu.GetFrameStats().drawCalls >= models.size(),
                "GPU draw-call accounting did not include all FBX meshes");
    Require(gpu.GetCounters().validationErrors == 0, "GPU validation reported a rendering error");
    return capture;
}

struct Rectangle {
    int left;
    int top;
    int right;
    int bottom;
};

Rectangle ProjectBounds(const GalleryModel& model, const le::Mat4& viewProjection,
                        const le::GpuFrameCapture& capture) {
    const auto matrix = viewProjection * model.transform;
    const auto& m = matrix.elements;
    float left = std::numeric_limits<float>::infinity();
    float top = left;
    float right = -left;
    float bottom = -left;
    for (int corner = 0; corner < 8; ++corner) {
        const float x = corner & 1 ? model.maximum.x : model.minimum.x;
        const float y = corner & 2 ? model.maximum.y : model.minimum.y;
        const float z = corner & 4 ? model.maximum.z : model.minimum.z;
        const float w = m[3] * x + m[7] * y + m[11] * z + m[15];
        Require(std::isfinite(w) && w > 0, "Gallery model is behind the camera: " + model.id);
        const float pixelX =
            ((m[0] * x + m[4] * y + m[8] * z + m[12]) / w + 1) * .5f * capture.width;
        const float pixelY =
            (1 - (m[1] * x + m[5] * y + m[9] * z + m[13]) / w) * .5f * capture.height;
        Require(std::isfinite(pixelX) && std::isfinite(pixelY),
                "Gallery projection is not finite: " + model.id);
        left = std::min(left, pixelX);
        top = std::min(top, pixelY);
        right = std::max(right, pixelX);
        bottom = std::max(bottom, pixelY);
    }
    Require(left > 2 && top > 2 && right < capture.width - 2 && bottom < capture.height - 2,
            "The gallery camera cropped a model: " + model.id);
    return {static_cast<int>(std::floor(left)) - 1, static_cast<int>(std::floor(top)) - 1,
            static_cast<int>(std::ceil(right)) + 1, static_cast<int>(std::ceil(bottom)) + 1};
}

int PixelDifference(const le::GpuFrameCapture& first, const le::GpuFrameCapture& second,
                    size_t offset) {
    int difference = 0;
    for (size_t channel = 0; channel < 3; ++channel)
        difference += std::abs(static_cast<int>(first.rgba[offset + channel]) -
                               static_cast<int>(second.rgba[offset + channel]));
    return difference;
}

void CheckPixels(const std::vector<GalleryModel>& models, const le::PerspectiveCamera& camera,
                 const le::GpuFrameCapture& background, const le::GpuFrameCapture& textured,
                 const le::GpuFrameCapture& untextured) {
    Require(background.width == textured.width && background.height == textured.height &&
                untextured.width == textured.width && untextured.height == textured.height,
            "Gallery capture dimensions changed between comparison frames");
    Require(PixelDifference(background, textured, 0) <= 3,
            "The plain gallery background changed outside the model region");
    for (const auto& model : models) {
        const auto region = ProjectBounds(model, camera.GetViewProjectionMatrix(), textured);
        size_t visible = 0, textureChanges = 0, lit = 0;
        int brightest = 0;
        for (int y = region.top; y <= region.bottom; ++y) {
            for (int x = region.left; x <= region.right; ++x) {
                const auto offset =
                    (static_cast<size_t>(y) * textured.width + static_cast<size_t>(x)) * 4;
                const bool covered = PixelDifference(background, textured, offset) > 24;
                visible += covered;
                textureChanges += covered && PixelDifference(textured, untextured, offset) > 15;
                if (covered) {
                    const int brightness = std::max({static_cast<int>(textured.rgba[offset]),
                                                     static_cast<int>(textured.rgba[offset + 1]),
                                                     static_cast<int>(textured.rgba[offset + 2])});
                    brightest = std::max(brightest, brightness);
                    lit += brightness > 30;
                }
                Require(textured.rgba[offset + 3] == 255,
                        "The gallery capture contains a non-opaque model pixel");
            }
        }
        std::cout << model.id << ": visible=" << visible << " textured=" << textureChanges
                  << " lit=" << lit << " brightest=" << brightest << '\n';
        Require(visible > 100, "FBX model produced no visible geometry: " + model.id);
        Require(textureChanges > 50, "The shared colormap did not affect FBX pixels: " + model.id);
        // The selected source UVs use the colormap's gray palette. Geometry/texturing can both
        // change pixels while an inverted V coordinate samples its black rows instead.
        Require(lit > 100,
                "FBX model lost the lit gray palette of the shared colormap: " + model.id);
    }
}

void WritePngBytes(void* context, void* data, int bytes) {
    auto& output = *static_cast<std::ofstream*>(context);
    output.write(static_cast<const char*>(data), bytes);
}

void SaveCapture(const fs::path& path, const le::GpuFrameCapture& capture) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "The gallery PNG destination could not be opened");
    Require(stbi_write_png_to_func(WritePngBytes, &output, static_cast<int>(capture.width),
                                   static_cast<int>(capture.height), 4, capture.rgba.data(),
                                   static_cast<int>(capture.width * 4)) != 0,
            "Gallery PNG encoding failed");
    output.close();
    Require(static_cast<bool>(output), "The gallery PNG could not be saved completely");
}

int Run(const fs::path& capturePath) {
    if (DisplayUnavailable()) {
        std::cout << "SKIP: the FBX GPU gallery requires a window-system display\n";
        return 77;
    }
    VideoLifetime video;
    le::Window window;
    le::WindowProps props;
    props.title = "LuxEngine CC0 FBX basic model gallery";
    props.width = 1280;
    props.height = 800;
    props.resizable = false;
    const bool windowInitialized = window.Initialize(props);
    Require(windowInitialized,
            std::string("Gallery window initialization failed: ") + SDL_GetError());
    le::GpuDevice gpu;
    le::GpuDeviceConfig configuration;
    configuration.vsync = false;
    const bool gpuInitialized = gpu.Initialize(window, configuration);
    Require(gpuInitialized, "GPU initialization failed: " + gpu.GetLastError());
    std::cout << "GPU: " << gpu.GetInfo().deviceName << " / " << gpu.GetInfo().deviceType << '\n';
    AssetLifetime assets;
    le::Renderer3D::Context renderer(&gpu);
    const auto library = le::BasicModelLibrary::FindLibraryDirectory();
    Require(!library.empty(), "The installed CC0 basic model library was not found");
    const auto models = LoadGallery(library);
    const auto texture = Utf8(library / "Textures/colormap.png");
    int textureWidth = 0, textureHeight = 0;
    Require(le::AssetManager::ValidateTextureFile(texture, &textureWidth, &textureHeight) &&
                textureWidth == 512 && textureHeight == 512,
            "The shared CC0 colormap is missing or invalid");
    le::uint32 width = 0, height = 0;
    Require(window.GetFramebufferSize(width, height) && width > 0 && height > 0,
            "Gallery framebuffer dimensions are unavailable");
    le::PerspectiveCamera camera(le::ToRadians(40), static_cast<float>(width) / height, .1f, 100);
    camera.SetPosition({0, 9, 13});
    camera.SetTarget({0, .5f, 0});
    const auto background = Capture(window, gpu, renderer, camera, models, {}, false);
    const auto textured = Capture(window, gpu, renderer, camera, models, texture, true);
    if (!capturePath.empty()) {
        SaveCapture(capturePath, textured);
        std::cout << "Gallery PNG: " << Utf8(capturePath) << '\n';
    }
    const auto untextured = Capture(window, gpu, renderer, camera, models, {}, true);
    CheckPixels(models, camera, background, textured, untextured);
    Require(le::AssetManager::GetCachedMeshCount() == models.size(),
            "The gallery did not retain all imported FBX models");
    Require(le::AssetManager::GetAssetError(texture, true).empty(),
            "The shared colormap reported an asset error");
    Require(gpu.WaitIdle(), "Waiting for the FBX gallery GPU work failed");
    std::cout
        << "PASS: ten CC0 FBX meshes rendered with the shared colormap and valid GPU output\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
        std::cout << "Usage: LuxFbxRenderTests [gallery.png]\n";
        return argc > 2 ? 1 : 0;
    }
    try {
        fs::path capturePath;
        if (argc == 2) {
            const std::string_view value(argv[1]);
            capturePath = fs::path(
                std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }
        return Run(capturePath);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
