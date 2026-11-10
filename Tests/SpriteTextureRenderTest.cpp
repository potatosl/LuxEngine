#include "Engine/Assets/AssetManager.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Vulkan/VulkanFault.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;
int checks = 0;

void Check(bool result, const char* message) {
    ++checks;
    if (!result)
        throw std::runtime_error(message);
}

std::string Utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct Files {
    fs::path root = fs::temp_directory_path() /
                    ("lux-sprite-textures-" + std::to_string(SDL_GetTicksNS()));
    fs::path previous = fs::current_path();
    Files() {
        fs::create_directories(root / "Assets");
        fs::create_directories(root / "other");
    }
    ~Files() {
        std::error_code error;
        fs::current_path(previous, error);
        fs::remove_all(root, error);
    }
};

void Image(const fs::path& path, bool quadrants, int solidColor = 0) {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    Check(surface != nullptr, "texture test image allocates");
    const std::array<std::array<Uint8, 4>, 4> colors{{
        {{255, 0, 0, 255}}, {{0, 255, 0, 255}},
        {{0, 0, 255, 255}}, {{255, 255, 255, 255}}}};
    bool painted = true;
    for (int index = 0; index < 4; ++index) {
        const SDL_Rect rect{index % 2 * 16, index / 2 * 16, 16, 16};
        const auto& color = colors[quadrants ? index : solidColor];
        painted = SDL_FillSurfaceRect(surface, &rect,
                                      SDL_MapSurfaceRGBA(surface, color[0], color[1], color[2],
                                                         color[3])) && painted;
    }
    const bool saved = painted && SDL_SaveBMP(surface, Utf8(path).c_str());
    SDL_DestroySurface(surface);
    Check(saved, "texture test image saves");
}

struct Graphics {
    Window window;
    GpuDevice gpu;
    Renderer2D::Context renderer;
    ~Graphics() {
        renderer.Shutdown();
        AssetManager::Shutdown();
        gpu.Shutdown();
        window.Shutdown();
        SDL_Quit();
    }
    bool Initialize() {
        if (!SDL_Init(SDL_INIT_VIDEO))
            return false;
        WindowProps props;
        props.width = props.height = 128;
        props.vulkan = true;
        props.title = "Sprite texture regression";
        if (!window.Initialize(props))
            return false;
        if (!gpu.Initialize(window)) {
            const auto error = gpu.GetLastError();
            if (error.find("No Vulkan physical devices") != std::string::npos ||
                error.find("No suitable Vulkan device") != std::string::npos ||
                error.find("VK_ERROR_INCOMPATIBLE_DRIVER") != std::string::npos)
                return false;
            throw std::runtime_error(error);
        }
        renderer.Init(&gpu);
        return true;
    }
    template <class Draw> GpuFrameCapture CaptureDraw(Draw&& draw) {
        bool began = false;
        for (int retry = 0; retry < 30; ++retry) {
            window.PollEvents();
            const auto result = gpu.BeginFrame();
            Check(result != GpuDevice::FrameStart::Error, "sprite frame begins without GPU error");
            if (result == GpuDevice::FrameStart::Ready) {
                began = true;
                break;
            }
            SDL_Delay(2);
        }
        Check(began && gpu.RequestFrameCapture(), "sprite frame capture queues");
        Check(gpu.Clear(0, 0, 0, 1), "sprite background clears");
        Renderer2D::ScopedContext context(renderer);
        draw();
        Check(gpu.EndFrame(), "textured sprite frame submits");
        GpuFrameCapture capture;
        Check(gpu.ReadFrameCapture(capture), "textured sprite frame reads back");
        return capture;
    }
    GpuFrameCapture Capture(Scene& scene) {
        return CaptureDraw([&] {
            Renderer2D::BeginScene(OrthographicCamera(-2, 2, -2, 2));
            scene.OnRender(&gpu);
            Renderer2D::EndScene();
        });
    }
};

std::array<int, 3> Pixel(const GpuFrameCapture& image, float x, float y) {
    const auto px = static_cast<uint32>(image.width * x);
    const auto py = static_cast<uint32>(image.height * y);
    const size_t index = (static_cast<size_t>(py) * image.width + px) * 4;
    return {image.rgba[index], image.rgba[index + 1], image.rgba[index + 2]};
}

bool Color(const std::array<int, 3>& value, int red, int green, int blue) {
    return std::abs(value[0] - red) < 8 && std::abs(value[1] - green) < 8 &&
           std::abs(value[2] - blue) < 8;
}

struct Logs {
    SDL_LogOutputFunction previous = nullptr;
    void* data = nullptr;
    int missingTextureMessages = 0;
    Logs() {
        SDL_GetLogOutputFunction(&previous, &data);
        SDL_SetLogOutputFunction(
            [](void* userdata, int, SDL_LogPriority, const char* message) {
                auto& logs = *static_cast<Logs*>(userdata);
                if (std::string_view(message).find("Sprite texture could not be loaded") !=
                    std::string_view::npos)
                    ++logs.missingTextureMessages;
            }, this);
    }
    ~Logs() {
        SDL_SetLogOutputFunction(previous, data);
    }
};

void Run(Graphics& graphics) {
    Renderer2D::Init(&graphics.gpu);
    Check(Renderer2D::GetDevice() == &graphics.gpu, "default renderer exposes its initialized GPU");
    Renderer2D::Shutdown();
    Check(Renderer2D::GetDevice() == nullptr, "shut down renderer exposes no stale GPU");
    Files files;
    Image(files.root / "Assets" / "quad.bmp", true);
    Scene source;
    source.SetAssetRoot(Utf8(files.root));
    auto entity = source.CreateEntity("Textured sprite");
    entity.GetComponent<TransformComponent>().scale = {2, 2, 1};
    entity.AddComponent<SpriteRendererComponent>().texturePath = "Assets/quad.bmp";
    const auto file = files.root / "TextureScene.lux";
    Check(source.Serialize(Utf8(file)), "texture scene saves");
    Scene scene;
    Check(scene.Deserialize(Utf8(file)), "texture scene reloads");
    fs::current_path(files.root / "other");
    auto sprite = scene.GetEntities().front();
    auto& transform = sprite.GetComponent<TransformComponent>();
    auto& renderer = sprite.GetComponent<SpriteRendererComponent>();
    auto image = graphics.Capture(scene);
    Check(Color(Pixel(image, .375f, .375f), 255, 0, 0) &&
              Color(Pixel(image, .625f, .375f), 0, 255, 0) &&
              Color(Pixel(image, .375f, .625f), 0, 0, 255),
          "reloaded relative sprite texture renders from another working directory");
    const auto cached = AssetManager::GetCachedTextureCount();
    image = graphics.Capture(scene);
    Check(AssetManager::GetCachedTextureCount() == cached,
          "repeated scene rendering reuses the texture cache");
    image = graphics.CaptureDraw([&] {
        Check(Renderer2D::GetDevice() == &graphics.gpu,
              "active scoped renderer exposes its GPU for implicit scene drawing");
        Renderer2D::BeginScene(OrthographicCamera(-2, 2, -2, 2));
        scene.OnRender();
        AssetManager::UnloadTexture(scene.ResolveAssetPath(renderer.texturePath));
        Renderer2D::EndScene();
    });
    Check(AssetManager::GetCachedTextureCount() == 0 &&
              Color(Pixel(image, .375f, .375f), 255, 0, 0),
          "implicit device sprite remains visible when unloaded before EndScene");
    image = graphics.CaptureDraw([&] {
        Renderer2D::BeginScene(OrthographicCamera(-2, 2, -2, 2));
        scene.OnRender(&graphics.gpu);
        AssetManager::Clear();
        Renderer2D::EndScene();
    });
    Check(AssetManager::GetCachedTextureCount() == 0 &&
              Color(Pixel(image, .625f, .375f), 0, 255, 0),
          "scene retains submitted textures across a cache clear before EndScene");
    const auto originalTransform = transform;
    image = graphics.CaptureDraw([&] {
        Renderer2D::BeginScene(OrthographicCamera(-2, 2, -2, 2));
        transform.scale = {.8f, .8f, 1};
        transform.position.x = -.9f;
        scene.OnRender(&graphics.gpu);
        AssetManager::Clear();
        transform.position.x = .9f;
        scene.OnRender(&graphics.gpu);
        Renderer2D::EndScene();
    });
    transform = originalTransform;
    const bool queuedTextureLifetime = Color(Pixel(image, .225f, .45f), 255, 0, 0) &&
                                       Color(Pixel(image, .675f, .45f), 255, 0, 0);
    std::cout << "same-scene queued texture lifetime=" << queuedTextureLifetime << '\n';

    transform.rotation.z = ToRadians(90);
    image = graphics.Capture(scene);
    Check(Color(Pixel(image, .375f, .375f), 0, 255, 0) &&
              Color(Pixel(image, .375f, .625f), 255, 0, 0) &&
              Color(Pixel(image, .625f, .625f), 0, 0, 255),
          "textured sprite keeps its rotation and UV orientation");
    transform.rotation.z = 0;
    transform.scale.x = -2;
    image = graphics.Capture(scene);
    Check(Color(Pixel(image, .375f, .375f), 0, 255, 0) &&
              Color(Pixel(image, .625f, .375f), 255, 0, 0),
          "negative sprite scale mirrors the texture");

    transform.scale.x = 2;
    renderer.color = {.5f, .25f, 1, .5f};
    image = graphics.Capture(scene);
    Check(Color(Pixel(image, .625f, .625f), 64, 32, 128),
          "texture tint and alpha both reach the GPU");

    GpuTexture white;
    const uint32 whitePixel = 0xffffffffu;
    Check(white.Initialize(&graphics.gpu, 1, 1, &whitePixel), "perspective sprite texture loads");
    image = graphics.CaptureDraw([&] {
        const auto projection = Mat4::Perspective(ToRadians(60), 1, .1f, 100) *
                                Mat4::LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        Renderer2D::BeginScene(projection, true);
        Renderer2D::DrawRotatedSprite(Vec3{0, 0, 1}, {1, 1}, 0, &white, {0, 1, 0, 1});
        Renderer2D::DrawRotatedSprite(Vec3{0, 0, 0}, {2, 2}, 0, &white, {1, 0, 0, 1});
        Renderer2D::EndScene();
    });
    Check(Color(Pixel(image, .5f, .5f), 0, 255, 0) &&
              Color(Pixel(image, .65f, .5f), 255, 0, 0),
          "perspective textured sprites preserve their Z positions and depth testing");

    renderer.texturePath = "Assets/later.bmp";
    renderer.color = {1, 0, 1, 1};
    Logs logs;
    image = graphics.Capture(scene);
    Check(Color(Pixel(image, .5f, .5f), 255, 0, 255),
          "missing texture renders the sprite color as fallback");
    graphics.Capture(scene);
    Check(logs.missingTextureMessages == 1, "missing texture does not log every frame");
    Image(files.root / "Assets" / "later.bmp", false);
    image = graphics.Capture(scene);
    Check(Color(Pixel(image, .5f, .5f), 255, 0, 0),
          "a newly created missing texture is retried without waiting for the cooldown");
    renderer.color = {1, 1, 1, 1};
    const auto liveFile = files.root / "Assets" / "later.bmp";
    Image(liveFile, false, 1);
    fs::last_write_time(liveFile, fs::last_write_time(liveFile) + std::chrono::seconds(1));
    image = graphics.Capture(scene);
    const bool changedTexture = Color(Pixel(image, .5f, .5f), 0, 255, 0);
    fs::remove(liveFile);
    image = graphics.Capture(scene);
    const bool deletedTexture = Color(Pixel(image, .5f, .5f), 255, 255, 255);
    Image(liveFile, false, 2);
    image = graphics.Capture(scene);
    const bool recreatedTexture = Color(Pixel(image, .5f, .5f), 0, 0, 255);
    std::cout << "GPU texture changed=" << changedTexture << " deleted=" << deletedTexture
              << " recreated=" << recreatedTexture << '\n';
    Check(queuedTextureLifetime,
          "repeated scene rendering retains each queued texture until EndScene");
    Check(changedTexture && deletedTexture && recreatedTexture,
          "GPU texture cache follows changed, deleted and recreated image files");
#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
    {
        Window otherWindow;
        WindowProps properties;
        properties.width = properties.height = 64;
        properties.vulkan = true;
        Check(otherWindow.Initialize(properties), "second sprite window initializes");
        GpuDevice otherGpu;
        Check(otherGpu.Initialize(otherWindow), "second sprite GPU initializes");
        Vk::ClearFaults();
        Vk::InjectFault(Vk::Fault::ImageAllocation);
        const auto failed = scene.LoadSpriteTexture(renderer.texturePath, &otherGpu);
        const auto triggered = Vk::TriggeredFaults(Vk::Fault::ImageAllocation);
        Vk::ClearFaults();
        Check(!failed && triggered == 1 && otherGpu.IsReady(),
              "second GPU texture allocation fails recoverably");
        image = graphics.Capture(scene);
        Check(Color(Pixel(image, .5f, .5f), 0, 0, 255),
              "one GPU texture failure does not suppress a healthy GPU's sprite");
        otherGpu.Shutdown();
        Check(otherGpu.Initialize(otherWindow), "failed sprite GPU can be recreated");
        const auto recovered = scene.LoadSpriteTexture(renderer.texturePath, &otherGpu);
        Check(recovered && recovered->BelongsTo(otherGpu),
              "a recreated GPU does not inherit the previous device's texture failure cooldown");
        Check(otherGpu.GetCounters().validationErrors == 0,
              "second GPU sprite operations have no validation errors");
    }
#endif
    Check(graphics.gpu.GetCounters().validationErrors == 0,
          "sprite texture drawing has no Vulkan validation errors");
}
}

int main() {
    try {
        Graphics graphics;
        if (!graphics.Initialize()) {
            std::cout << "No Vulkan display/device available for sprite texture rendering\n";
            return 77;
        }
        Run(graphics);
        std::cout << "sprite textures: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sprite textures: " << error.what() << '\n';
        return 1;
    }
}
