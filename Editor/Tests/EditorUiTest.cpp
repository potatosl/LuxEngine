#include "App/EditorWindow.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Renderer/RHI/GpuDevice.hpp"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Renderer/Vulkan/VulkanFault.hpp"
#include "Scene/EditorDocument.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include "Viewport/SceneViewport3D.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <psapi.h>
#include <windows.h>
#ifdef small
#undef small
#endif
#endif

namespace {
constexpr int kSkipReturnCode = 77;
constexpr int kDefaultIterations = 100;

struct CpuMemorySample {
    LuxEngine::uint64 workingSetBytes = 0;
    LuxEngine::uint64 privateBytes = 0;
};

struct DetailedCpuMemorySample {
    LuxEngine::uint64 workingSetBytes = 0;
    LuxEngine::uint64 privateBytes = 0;
    LuxEngine::uint64 defaultHeapBusyBytes = 0;
    LuxEngine::uint64 defaultHeapBusyBlocks = 0;
    LuxEngine::uint64 secondaryHeapsBusyBytes = 0;
    LuxEngine::uint64 secondaryHeapsBusyBlocks = 0;
    LuxEngine::uint32 heapCount = 0;
    LuxEngine::uint64 vmPrivateCommitBytes = 0;
    LuxEngine::uint64 vmMappedCommitBytes = 0;
    LuxEngine::uint64 vmImageCommitBytes = 0;
};

struct FourStageCpuSample {
    int iteration = 0;
    CpuMemorySample preInit{};
    CpuMemorySample postRender{};
    CpuMemorySample postShutdown{};
    CpuMemorySample postDestroy{};
    DetailedCpuMemorySample detailedPostDestroy{};
};

CpuMemorySample QueryCpuMemory() {
    CpuMemorySample sample{};
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        sample.workingSetBytes = static_cast<LuxEngine::uint64>(pmc.WorkingSetSize);
        sample.privateBytes = static_cast<LuxEngine::uint64>(pmc.PrivateUsage);
    }
#endif
    return sample;
}

void CompactProcessHeaps() {
#if defined(_WIN32)
    _heapmin();
    const DWORD count = GetProcessHeaps(0, nullptr);
    if (count > 0 && count <= 512) {
        std::vector<HANDLE> heaps(count);
        const DWORD fetched = GetProcessHeaps(count, heaps.data());
        for (DWORD i = 0; i < fetched; ++i) {
            if (heaps[i])
                HeapCompact(heaps[i], 0);
        }
    }
#endif
}

DetailedCpuMemorySample QueryDetailedCpuMemory(bool compactFirst = false) {
    DetailedCpuMemorySample out{};
#if defined(_WIN32)
    if (compactFirst)
        CompactProcessHeaps();

    const auto basic = QueryCpuMemory();
    out.workingSetBytes = basic.workingSetBytes;
    out.privateBytes = basic.privateBytes;

    const HANDLE defaultHeap = GetProcessHeap();
    const DWORD count = GetProcessHeaps(0, nullptr);
    if (count > 0 && count <= 512) {
        std::vector<HANDLE> heaps(count);
        const DWORD fetched = GetProcessHeaps(count, heaps.data());
        out.heapCount = fetched;
        for (DWORD i = 0; i < fetched; ++i) {
            HANDLE h = heaps[i];
            if (!h)
                continue;
            if (!HeapLock(h))
                continue;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(h, &entry)) {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                    if (h == defaultHeap) {
                        out.defaultHeapBusyBytes += entry.cbData;
                        ++out.defaultHeapBusyBlocks;
                    } else {
                        out.secondaryHeapsBusyBytes += entry.cbData;
                        ++out.secondaryHeapsBusyBlocks;
                    }
                }
            }
            HeapUnlock(h);
        }
    }

    MEMORY_BASIC_INFORMATION mbi{};
    const unsigned char* addr = nullptr;
    while (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT) {
            if (mbi.Type == MEM_PRIVATE)
                out.vmPrivateCommitBytes += mbi.RegionSize;
            else if (mbi.Type == MEM_MAPPED)
                out.vmMappedCommitBytes += mbi.RegionSize;
            else if (mbi.Type == MEM_IMAGE)
                out.vmImageCommitBytes += mbi.RegionSize;
        }
        const unsigned char* next =
            static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr)
            break;
        addr = next;
    }
#else
    (void)compactFirst;
#endif
    return out;
}

struct StressCheckpoint {
    int iteration = 0;
    CpuMemorySample cpu{};
    FourStageCpuSample fourStage{};
    LuxEngine::GpuMemoryStats gpuMem{};
    LuxEngine::GpuDeviceCounters gpuCounters{};
    LuxEngine::uint64 cumulativeFramesSubmitted = 0;
    LuxEngine::uint64 cumulativeSubmissions = 0;
    LuxEngine::uint64 cumulativePipelinesCreated = 0;
    LuxEngine::uint32 cumulativeValidationErrors = 0;
};

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void CheckWithGpuDiagnostics(bool condition, const char* phase, int iteration,
                             const LuxEngine::EngineSceneRenderer* renderer, const char* message) {
    if (condition)
        return;
    const auto cpu = QueryCpuMemory();
    std::ostringstream oss;
    oss << "[FAILURE DIAGNOSTIC] phase=" << phase << " iteration=" << iteration << " error=\""
        << message << "\" cpuWorkingSetBytes=" << cpu.workingSetBytes
        << " cpuPrivateBytes=" << cpu.privateBytes;
    if (renderer) {
        oss << " rendererReady=" << (renderer->IsReady() ? "true" : "false")
            << " recoveryCount=" << renderer->GetRecoveryCount() << " rendererError=\""
            << renderer->LastError().toStdString() << "\"";
        if (const auto* gpu = renderer->GetGpuDevice()) {
            const auto mem = gpu->GetMemoryStats();
            const auto cnt = gpu->GetCounters();
            oss << " gpuStatus=" << LuxEngine::ToString(gpu->GetStatus())
                << " gpuLastFailure=" << LuxEngine::ToString(gpu->GetLastFailure())
                << " gpuLastError=\"" << gpu->GetLastError() << "\""
                << " activeAllocCount=" << mem.allocationCount
                << " activeAllocBytes=" << mem.allocationBytes
                << " activeLiveBuffers=" << mem.liveBuffers
                << " activeLiveTextures=" << mem.liveTextures
                << " pendingDestructions=" << mem.pendingDestructions
                << " pendingStagingBytes=" << mem.pendingStagingBytes
                << " deviceLocalUsage=" << mem.deviceLocalUsage
                << " validationErrors=" << cnt.validationErrors
                << " validationWarnings=" << cnt.validationWarnings
                << " cumulativeFramesSubmitted=" << cnt.framesSubmitted;
        } else {
            oss << " gpuDevice=null";
        }
    }
    throw std::runtime_error(oss.str());
}

int ParsePositiveInt(const std::string& raw, int fallback) {
    if (raw.empty())
        return fallback;
    try {
        size_t pos = 0;
        const long val = std::stol(raw, &pos, 10);
        if (pos == raw.size() && val >= 1 && val <= 100000)
            return static_cast<int>(val);
    } catch (...) {
    }
    return fallback;
}

struct StressConfig {
    std::string mode = "all";
    int gpuIterations = kDefaultIterations;
    int viewportIterations = kDefaultIterations;
    int undoIterations = kDefaultIterations;
};

StressConfig ParseStressConfig(int argc, char** argv) {
    StressConfig cfg;
    const QString envAll = qEnvironmentVariable("LUX_STRESS_ITERATIONS");
    if (!envAll.isEmpty()) {
        const int n = ParsePositiveInt(envAll.toStdString(), kDefaultIterations);
        cfg.gpuIterations = n;
        cfg.viewportIterations = n;
        cfg.undoIterations = n;
    }
    const QString envGpu = qEnvironmentVariable("LUX_GPU_STRESS_ITERATIONS");
    if (!envGpu.isEmpty())
        cfg.gpuIterations = ParsePositiveInt(envGpu.toStdString(), cfg.gpuIterations);

    const QString envVp = qEnvironmentVariable("LUX_VIEWPORT_STRESS_ITERATIONS");
    if (!envVp.isEmpty())
        cfg.viewportIterations = ParsePositiveInt(envVp.toStdString(), cfg.viewportIterations);

    const QString envUndo = qEnvironmentVariable("LUX_UNDO_STRESS_ITERATIONS");
    if (!envUndo.isEmpty())
        cfg.undoIterations = ParsePositiveInt(envUndo.toStdString(), cfg.undoIterations);

    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        const std::string arg = argv[i];
        if (arg == "ui" || arg == "gpu" || arg == "all") {
            cfg.mode = arg;
        } else if (arg.rfind("--iterations=", 0) == 0) {
            const int n = ParsePositiveInt(arg.substr(13), kDefaultIterations);
            cfg.gpuIterations = n;
            cfg.viewportIterations = n;
            cfg.undoIterations = n;
        } else if (arg.rfind("--gpu-iterations=", 0) == 0) {
            cfg.gpuIterations = ParsePositiveInt(arg.substr(17), cfg.gpuIterations);
        } else if (arg.rfind("--viewport-iterations=", 0) == 0) {
            cfg.viewportIterations = ParsePositiveInt(arg.substr(22), cfg.viewportIterations);
        } else if (arg.rfind("--undo-iterations=", 0) == 0) {
            cfg.undoIterations = ParsePositiveInt(arg.substr(18), cfg.undoIterations);
        } else {
            const int maybeNum = ParsePositiveInt(arg, -1);
            if (maybeNum > 0) {
                cfg.gpuIterations = maybeNum;
                cfg.viewportIterations = maybeNum;
                cfg.undoIterations = maybeNum;
            }
        }
    }
    return cfg;
}

bool FloatNear(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
}

bool Vec2Near(const LuxEngine::Vec2& a, const LuxEngine::Vec2& b, float eps = 1e-4f) {
    return FloatNear(a.x, b.x, eps) && FloatNear(a.y, b.y, eps);
}

bool Vec3Near(const LuxEngine::Vec3& a, const LuxEngine::Vec3& b, float eps = 1e-4f) {
    return FloatNear(a.x, b.x, eps) && FloatNear(a.y, b.y, eps) && FloatNear(a.z, b.z, eps);
}

bool Vec4Near(const LuxEngine::Vec4& a, const LuxEngine::Vec4& b, float eps = 1e-4f) {
    return FloatNear(a.x, b.x, eps) && FloatNear(a.y, b.y, eps) && FloatNear(a.z, b.z, eps) &&
           FloatNear(a.w, b.w, eps);
}

std::string FormatVulkanVersion(LuxEngine::uint32 version) {
    const auto major = (version >> 22) & 0x7FU;
    const auto minor = (version >> 12) & 0x3FFU;
    const auto patch = version & 0xFFFU;
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

void SetClipboardJsonBytes(const QByteArray& rawBytes) {
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-lux-entity"), rawBytes);
    QApplication::clipboard()->setMimeData(mime);
}

void SetClipboardJsonObject(const QJsonObject& obj) {
    SetClipboardJsonBytes(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject MakeValidClipboardEntityJson() {
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), QStringLiteral("검증엔티티"));
    obj.insert(QStringLiteral("position"), QJsonArray{1.5, -2.5, 3.0});
    obj.insert(QStringLiteral("rotation"), QJsonArray{0.1, 0.2, 0.3});
    obj.insert(QStringLiteral("scale"), QJsonArray{2.0, 3.0, 4.0});
    obj.insert(QStringLiteral("sprite"), QJsonArray{0.2, 0.4, 0.6, 1.0});
    obj.insert(QStringLiteral("mesh"), QJsonArray{0.25, 0.75, 0.8, 0.7, 0.6, 1.0});

    QJsonObject cam;
    cam.insert(QStringLiteral("primary"), true);
    cam.insert(QStringLiteral("perspective"), true);
    cam.insert(QStringLiteral("fov"), 65.0);
    cam.insert(QStringLiteral("orthoSize"), 12.0);
    obj.insert(QStringLiteral("camera"), cam);

    QJsonObject map;
    map.insert(QStringLiteral("width"), 2);
    map.insert(QStringLiteral("height"), 2);
    map.insert(QStringLiteral("tileSize"), 1.5);
    map.insert(QStringLiteral("tiles"), QJsonArray{1, 2, 3, 4});
    obj.insert(QStringLiteral("tilemap"), map);

    QJsonObject rb2d;
    rb2d.insert(QStringLiteral("type"), 2);
    rb2d.insert(QStringLiteral("velocity"), QJsonArray{3.5, -1.5});
    rb2d.insert(QStringLiteral("mass"), 4.25);
    rb2d.insert(QStringLiteral("gravityScale"), 1.75);
    obj.insert(QStringLiteral("rigidbody2d"), rb2d);

    QJsonObject col2d;
    col2d.insert(QStringLiteral("size"), QJsonArray{2.5, 3.5});
    col2d.insert(QStringLiteral("offset"), QJsonArray{0.25, -0.5});
    col2d.insert(QStringLiteral("trigger"), true);
    obj.insert(QStringLiteral("collider2d"), col2d);

    QJsonObject rb3d;
    rb3d.insert(QStringLiteral("type"), 1);
    rb3d.insert(QStringLiteral("velocity"), QJsonArray{1.0, 2.0, 3.0});
    rb3d.insert(QStringLiteral("mass"), 8.0);
    rb3d.insert(QStringLiteral("gravityScale"), 0.5);
    obj.insert(QStringLiteral("rigidbody3d"), rb3d);

    QJsonObject col3d;
    col3d.insert(QStringLiteral("size"), QJsonArray{1.5, 2.5, 3.5});
    col3d.insert(QStringLiteral("offset"), QJsonArray{-0.1, 0.2, -0.3});
    col3d.insert(QStringLiteral("trigger"), false);
    obj.insert(QStringLiteral("collider3d"), col3d);

    QJsonObject anim;
    anim.insert(QStringLiteral("name"), QStringLiteral("이동클립"));
    anim.insert(QStringLiteral("duration"), 2.5);
    anim.insert(QStringLiteral("loop"), true);
    anim.insert(QStringLiteral("playing"), false);
    QJsonObject key0;
    key0.insert(QStringLiteral("time"), 0.0);
    key0.insert(QStringLiteral("position"), QJsonArray{0.0, 0.0, 0.0});
    key0.insert(QStringLiteral("rotation"), QJsonArray{0.0, 0.0, 0.0});
    key0.insert(QStringLiteral("scale"), QJsonArray{1.0, 1.0, 1.0});
    QJsonObject key1;
    key1.insert(QStringLiteral("time"), 1.25);
    key1.insert(QStringLiteral("position"), QJsonArray{4.0, 5.0, 6.0});
    key1.insert(QStringLiteral("rotation"), QJsonArray{0.0, 1.0, 0.0});
    key1.insert(QStringLiteral("scale"), QJsonArray{2.0, 2.0, 2.0});
    anim.insert(QStringLiteral("keyframes"), QJsonArray{key0, key1});
    obj.insert(QStringLiteral("animation"), anim);
    return obj;
}

void VerifyFullClipboardEntityComponents(LuxEngine::Entity entity) {
    using namespace LuxEngine;
    Check(entity.IsValid(), "pasted entity is valid");
    Check(entity.HasComponent<TagComponent>(), "pasted entity has TagComponent");
    Check(entity.GetComponent<TagComponent>().tag == "검증엔티티 복사본",
          "pasted entity TagComponent name matches");

    Check(entity.HasComponent<TransformComponent>(), "pasted entity has TransformComponent");
    const auto& tr = entity.GetComponent<TransformComponent>();
    Check(Vec3Near(tr.position, {1.5f, -2.5f, 3.0f}) && Vec3Near(tr.rotation, {0.1f, 0.2f, 0.3f}) &&
              Vec3Near(tr.scale, {2.0f, 3.0f, 4.0f}),
          "pasted entity TransformComponent values match");

    Check(entity.HasComponent<SpriteRendererComponent>(),
          "pasted entity has SpriteRendererComponent");
    Check(Vec4Near(entity.GetComponent<SpriteRendererComponent>().color, {0.2f, 0.4f, 0.6f, 1.0f}),
          "pasted entity SpriteRendererComponent color matches");

    Check(entity.HasComponent<MeshRendererComponent>(), "pasted entity has MeshRendererComponent");
    const auto& mat = entity.GetComponent<MeshRendererComponent>().material;
    Check(FloatNear(mat.metallic, 0.25f) && FloatNear(mat.roughness, 0.75f) &&
              Vec4Near(mat.albedoColor, {0.8f, 0.7f, 0.6f, 1.0f}),
          "pasted entity MeshRendererComponent material matches");

    Check(entity.HasComponent<CameraComponent>(), "pasted entity has CameraComponent");
    const auto& cam = entity.GetComponent<CameraComponent>();
    Check(cam.isPrimary && cam.isPerspective && FloatNear(cam.fov, 65.0f) &&
              FloatNear(cam.orthoSize, 12.0f),
          "pasted entity CameraComponent values match");

    Check(entity.HasComponent<TilemapComponent>(), "pasted entity has TilemapComponent");
    const auto& map = entity.GetComponent<TilemapComponent>();
    Check(map.width == 2 && map.height == 2 && FloatNear(map.tileSize, 1.5f) &&
              map.tiles.size() == 4 && map.tiles[0] == 1 && map.tiles[1] == 2 &&
              map.tiles[2] == 3 && map.tiles[3] == 4,
          "pasted entity TilemapComponent values match");

    Check(entity.HasComponent<Rigidbody2DComponent>(), "pasted entity has Rigidbody2DComponent");
    const auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();
    Check(rb2d.type == BodyType::Kinematic && Vec2Near(rb2d.velocity, {3.5f, -1.5f}) &&
              FloatNear(rb2d.mass, 4.25f) && FloatNear(rb2d.gravityScale, 1.75f),
          "pasted entity Rigidbody2DComponent values match");

    Check(entity.HasComponent<BoxCollider2DComponent>(),
          "pasted entity has BoxCollider2DComponent");
    const auto& col2d = entity.GetComponent<BoxCollider2DComponent>();
    Check(Vec2Near(col2d.size, {2.5f, 3.5f}) && Vec2Near(col2d.offset, {0.25f, -0.5f}) &&
              col2d.isTrigger,
          "pasted entity BoxCollider2DComponent values match");

    Check(entity.HasComponent<Rigidbody3DComponent>(), "pasted entity has Rigidbody3DComponent");
    const auto& rb3d = entity.GetComponent<Rigidbody3DComponent>();
    Check(rb3d.type == BodyType::Dynamic && Vec3Near(rb3d.velocity, {1.0f, 2.0f, 3.0f}) &&
              FloatNear(rb3d.mass, 8.0f) && FloatNear(rb3d.gravityScale, 0.5f),
          "pasted entity Rigidbody3DComponent values match");

    Check(entity.HasComponent<BoxCollider3DComponent>(),
          "pasted entity has BoxCollider3DComponent");
    const auto& col3d = entity.GetComponent<BoxCollider3DComponent>();
    Check(Vec3Near(col3d.size, {1.5f, 2.5f, 3.5f}) &&
              Vec3Near(col3d.offset, {-0.1f, 0.2f, -0.3f}) && !col3d.isTrigger,
          "pasted entity BoxCollider3DComponent values match");

    Check(entity.HasComponent<AnimationComponent>(), "pasted entity has AnimationComponent");
    const auto& anim = entity.GetComponent<AnimationComponent>();
    Check(anim.clip.name == "이동클립" && FloatNear(anim.clip.duration, 2.5f) && anim.clip.loop &&
              !anim.playing && anim.clip.keyframes.size() == 2 &&
              FloatNear(anim.clip.keyframes[0].time, 0.0f) &&
              Vec3Near(anim.clip.keyframes[0].position, {0.0f, 0.0f, 0.0f}) &&
              Vec3Near(anim.clip.keyframes[0].scale, {1.0f, 1.0f, 1.0f}) &&
              FloatNear(anim.clip.keyframes[1].time, 1.25f) &&
              Vec3Near(anim.clip.keyframes[1].position, {4.0f, 5.0f, 6.0f}) &&
              Vec3Near(anim.clip.keyframes[1].rotation, {0.0f, 1.0f, 0.0f}) &&
              Vec3Near(anim.clip.keyframes[1].scale, {2.0f, 2.0f, 2.0f}),
          "pasted entity AnimationComponent values and keyframes match");
}

LuxEngine::Entity FindEntityByTag(LuxEngine::Scene& scene, const std::string& tag) {
    for (LuxEngine::Entity entity : scene.GetEntities()) {
        if (entity.HasComponent<LuxEngine::TagComponent>() &&
            entity.GetComponent<LuxEngine::TagComponent>().tag == tag) {
            return entity;
        }
    }
    return {};
}

bool HasNonBackgroundPixel(const QImage& image, int bgR, int bgG, int bgB) {
    if (image.isNull())
        return false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - bgR) > 15 || std::abs(c.green() - bgG) > 15 ||
                std::abs(c.blue() - bgB) > 15) {
                return true;
            }
        }
    }
    return false;
}

int RunHeadlessEditorUiTests(QApplication& app, QTemporaryDir& settings, const StressConfig& cfg) {
    using namespace LuxEngine;
    // Strictly disable GPU viewport in Editor_ui so it runs 100% headlessly without GPU dependency.
    EngineSceneRenderer::SetForceAllowed(false);

    std::cerr << "[Editor_ui] creating EditorWindow (headless offscreen, undoIterations="
              << cfg.undoIterations << ")\n";
    EditorWindow window;
    window.show();
    app.processEvents();

    Check(window.findChildren<QDockWidget*>().size() == 8, "eight dock panels are available");
    auto* tabs = window.findChild<QTabWidget*>();
    Check(tabs && tabs->count() == 2, "2D and 3D workspaces exist");
    const QByteArray state = window.saveState(1);
    Check(!state.isEmpty() && window.restoreState(state, 1), "dock layout roundtrip");

    auto* hierarchy = window.findChildren<QListWidget*>().front();
    auto* add = window.findChild<QAction*>(QStringLiteral("addEntity"));
    Check(add && hierarchy->count() == 0, "new document starts empty");
    add->trigger();
    Check(hierarchy->count() == 1, "entity can be added from menu");

    auto* undo = window.findChild<QUndoStack*>();
    Check(undo && undo->canUndo(), "scene edit enters undo history");
    undo->undo();
    Check(hierarchy->count() == 0 && undo->canRedo(), "undo restores scene");
    undo->redo();
    Check(hierarchy->count() == 1, "redo restores edit");

    auto* addTilemap = window.findChild<QAction*>(QStringLiteral("addTilemap"));
    Check(addTilemap != nullptr, "tilemap action exists");
    addTilemap->trigger();
    Check(hierarchy->count() == 2, "tilemap can be added");
    app.processEvents();

    auto* canvas = window.findChild<QWidget*>(QStringLiteral("sceneViewport2D"));
    Check(canvas && canvas->isVisible(), "2D tile editor is visible");
    const int beforePaint = undo->count();
    const QPointF tile(canvas->width() * .5 + 36, canvas->height() * .5 - 36);
    const QPointF globalTile = canvas->mapToGlobal(tile.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, tile, globalTile, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, tile, globalTile, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QApplication::sendEvent(canvas, &release);
    Check(undo->count() == beforePaint + 1, "tile painting enters undo history");

    for (const QString& id : {QStringLiteral("editorBody2D"), QStringLiteral("editorCollider2D"),
                              QStringLiteral("editorAnimation")}) {
        auto* checkbox = window.findChild<QCheckBox*>(id);
        Check(checkbox != nullptr, "component editor exists");
        checkbox->setChecked(true);
        Check(checkbox->isChecked(), "component can be added in inspector");
    }

    auto* copyAction = window.findChild<QAction*>(QStringLiteral("copyEntity"));
    auto* pasteAction = window.findChild<QAction*>(QStringLiteral("pasteEntity"));
    Check(copyAction && pasteAction, "copy and paste actions exist");

    // --- PART 3.1: Configurable (100x / 1000x) Undo/Redo Regression ---
    std::cerr << "[Editor_ui] PART 3.1: Entity create -> add components -> copy -> paste -> "
              << cfg.undoIterations << "x Undo/Redo\n";
    copyAction->trigger();
    pasteAction->trigger();
    Check(hierarchy->count() == 3, "copy and paste entity duplicates all components");
    for (const QString& id : {QStringLiteral("editorBody2D"), QStringLiteral("editorCollider2D"),
                              QStringLiteral("editorAnimation")}) {
        auto* checkbox = window.findChild<QCheckBox*>(id);
        Check(checkbox && checkbox->isChecked(),
              "pasted entity retains physics, collider and animation components");
    }

    undo->undo();
    Check(hierarchy->count() == 2, "undo before full-component paste");

    // Paste full 11-component entity and run N-iteration Undo/Redo loop
    SetClipboardJsonObject(MakeValidClipboardEntityJson());
    pasteAction->trigger();
    Check(hierarchy->count() == 3 && window.GetDocument().GetScene().GetEntityCount() == 3,
          "valid full-component clipboard JSON pasted");
    for (const QString& id :
         {QStringLiteral("editorSprite"), QStringLiteral("editorMesh"),
          QStringLiteral("editorTilemap"), QStringLiteral("editorBody2D"),
          QStringLiteral("editorCollider2D"), QStringLiteral("editorAnimation")}) {
        auto* checkbox = window.findChild<QCheckBox*>(id);
        Check(checkbox && checkbox->isChecked(),
              "inspector checkboxes reflect pasted entity components");
    }

    Entity initialPasted = FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본");
    VerifyFullClipboardEntityComponents(initialPasted);

    std::string baselineSerialized;
    Check(window.GetDocument().GetScene().SerializeToString(baselineSerialized),
          "baseline scene serializes before Undo/Redo stress loop");

    const auto undoStart = std::chrono::steady_clock::now();
    const CpuMemorySample undoCpuStart = QueryCpuMemory();
    CpuMemorySample undoCpu100{}, undoCpu500{}, undoCpu1000{};

    for (int iter = 1; iter <= cfg.undoIterations; ++iter) {
        undo->undo();
        if (hierarchy->count() != 2 || window.GetDocument().GetScene().GetEntityCount() != 2 ||
            FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본").IsValid()) {
            const auto cpu = QueryCpuMemory();
            std::ostringstream oss;
            oss << "[Undo/Redo FAILURE] iteration=" << iter
                << " phase=undo hierarchyCount=" << hierarchy->count()
                << " sceneEntities=" << window.GetDocument().GetScene().GetEntityCount()
                << " cpuWorkingSetBytes=" << cpu.workingSetBytes
                << " cpuPrivateBytes=" << cpu.privateBytes;
            throw std::runtime_error(oss.str());
        }
        undo->redo();
        if (hierarchy->count() != 3 || window.GetDocument().GetScene().GetEntityCount() != 3) {
            const auto cpu = QueryCpuMemory();
            std::ostringstream oss;
            oss << "[Undo/Redo FAILURE] iteration=" << iter
                << " phase=redo hierarchyCount=" << hierarchy->count()
                << " sceneEntities=" << window.GetDocument().GetScene().GetEntityCount()
                << " cpuWorkingSetBytes=" << cpu.workingSetBytes
                << " cpuPrivateBytes=" << cpu.privateBytes;
            throw std::runtime_error(oss.str());
        }

        if (iter % 10 == 0 || iter == 1 || iter == cfg.undoIterations) {
            const auto cpuNow = QueryCpuMemory();
            if (iter == 100)
                undoCpu100 = cpuNow;
            if (iter == 500)
                undoCpu500 = cpuNow;
            if (iter == 1000)
                undoCpu1000 = cpuNow;
            std::cerr << "  [Undo/Redo] iter " << iter << "/" << cfg.undoIterations
                      << " | entities=" << window.GetDocument().GetScene().GetEntityCount()
                      << " undoCount=" << undo->count() << " undoIndex=" << undo->index()
                      << " cpuWorkingSetKB=" << (cpuNow.workingSetBytes / 1024)
                      << " cpuPrivateKB=" << (cpuNow.privateBytes / 1024) << "\n";
        }
    }

    const double undoElapsedSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - undoStart).count();
    const CpuMemorySample undoCpuEnd = QueryCpuMemory();

    Entity afterStressPasted =
        FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본");
    VerifyFullClipboardEntityComponents(afterStressPasted);
    std::string afterStressSerialized;
    Check(window.GetDocument().GetScene().SerializeToString(afterStressSerialized) &&
              afterStressSerialized == baselineSerialized,
          "scene state and all component values are 100% identical after Undo/Redo stress loop");

    std::cerr << "[Editor_ui] PART 3.1 PASS: " << cfg.undoIterations << "/" << cfg.undoIterations
              << " Undo/Redo verified in " << undoElapsedSec
              << " s (entity count=3, all 11 components & serialized scene identical, cpuPrivateKB "
                 "start="
              << (undoCpuStart.privateBytes / 1024)
              << " -> end=" << (undoCpuEnd.privateBytes / 1024);
    if (cfg.undoIterations >= 100)
        std::cerr << " | @100=" << (undoCpu100.privateBytes / 1024) << "KB";
    if (cfg.undoIterations >= 500)
        std::cerr << " | @500=" << (undoCpu500.privateBytes / 1024) << "KB";
    if (cfg.undoIterations >= 1000)
        std::cerr << " | @1000=" << (undoCpu1000.privateBytes / 1024) << "KB";
    std::cerr << ")\n";

    // Copy the pasted entity back to clipboard and verify all 9 optional components roundtrip
    // intact
    copyAction->trigger();
    {
        const auto copiedDoc = QJsonDocument::fromJson(QApplication::clipboard()->mimeData()->data(
            QStringLiteral("application/x-lux-entity")));
        Check(copiedDoc.isObject(), "copied back entity JSON is valid object");
        const auto copiedObj = copiedDoc.object();
        Check(copiedObj.contains(QStringLiteral("sprite")) &&
                  copiedObj.contains(QStringLiteral("mesh")) &&
                  copiedObj.contains(QStringLiteral("camera")) &&
                  copiedObj.contains(QStringLiteral("tilemap")) &&
                  copiedObj.contains(QStringLiteral("rigidbody2d")) &&
                  copiedObj.contains(QStringLiteral("collider2d")) &&
                  copiedObj.contains(QStringLiteral("rigidbody3d")) &&
                  copiedObj.contains(QStringLiteral("collider3d")) &&
                  copiedObj.contains(QStringLiteral("animation")),
              "all 9 optional components preserved on copied entity");
    }

    // --- PART 3.2: 13 Independent Invalid Clipboard Inputs ---
    std::cerr << "[Editor_ui] PART 3.2: Validating 13 independent abnormal clipboard inputs\n";
    const int stableEntityCount = hierarchy->count();
    const size_t stableSceneEntityCount = window.GetDocument().GetScene().GetEntityCount();
    const int stableUndoCount = undo->count();
    const int stableUndoIndex = undo->index();
    const int stableSelectedRow = hierarchy->currentRow();
    const uint32 stableSelectedId =
        window.GetDocument().GetSelected() ? window.GetDocument().GetSelected().GetId() : 0;

    auto assertRejected = [&](const char* label) {
        pasteAction->trigger();
        std::string currentSerialized;
        Check(window.GetDocument().GetScene().SerializeToString(currentSerialized),
              "scene serializes after rejected paste");
        const uint32 currentSelectedId =
            window.GetDocument().GetSelected() ? window.GetDocument().GetSelected().GetId() : 0;
        Check(currentSerialized == baselineSerialized && hierarchy->count() == stableEntityCount &&
                  window.GetDocument().GetScene().GetEntityCount() == stableSceneEntityCount &&
                  undo->count() == stableUndoCount && undo->index() == stableUndoIndex &&
                  hierarchy->currentRow() == stableSelectedRow &&
                  currentSelectedId == stableSelectedId &&
                  window.LastLogMessage().contains(QStringLiteral("붙여넣기 실패")),
              label);
    };

    // 1. NaN
    SetClipboardJsonBytes(
        R"({"name":"NaNTest","position":[NaN,0,0],"rotation":[0,0,0],"scale":[1,1,1]})");
    assertRejected("1. NaN rejected");

    // 2. Infinity
    SetClipboardJsonBytes(
        R"({"name":"InfTokenTest","position":[Infinity,0,0],"rotation":[0,0,0],"scale":[1,1,1]})");
    assertRejected("2. Infinity rejected");

    // 3. float Overflow (1e308 overflows 32-bit float to +Inf)
    SetClipboardJsonBytes(
        R"({"name":"OverflowTest","position":[1e308,0,0],"rotation":[0,0,0],"scale":[1,1,1]})");
    assertRejected("3. float overflow (1e308) rejected");

    // 4. Negative Mass
    {
        auto bad = MakeValidClipboardEntityJson();
        QJsonObject rb = bad.value(QStringLiteral("rigidbody2d")).toObject();
        rb.insert(QStringLiteral("mass"), -2.0);
        bad.insert(QStringLiteral("rigidbody2d"), rb);
        SetClipboardJsonObject(bad);
        assertRejected("4. negative mass rejected");
    }

    // 5. Abnormal Collider
    {
        auto bad = MakeValidClipboardEntityJson();
        QJsonObject col = bad.value(QStringLiteral("collider2d")).toObject();
        col.insert(QStringLiteral("size"), QJsonArray{-1.0, 2.0});
        bad.insert(QStringLiteral("collider2d"), col);
        SetClipboardJsonObject(bad);
        assertRejected("5a. negative collider2d size rejected");

        auto bad3d = MakeValidClipboardEntityJson();
        QJsonObject col3 = bad3d.value(QStringLiteral("collider3d")).toObject();
        col3.insert(QStringLiteral("size"), QJsonArray{1.0, 1.0, 0.0});
        bad3d.insert(QStringLiteral("collider3d"), col3);
        SetClipboardJsonObject(bad3d);
        assertRejected("5b. zero collider3d size rejected");
    }

    // 6. Invalid Transform (out-of-bounds position, zero scale, wrong array length)
    {
        auto badPos = MakeValidClipboardEntityJson();
        badPos.insert(QStringLiteral("position"), QJsonArray{250000.0, 0.0, 0.0});
        SetClipboardJsonObject(badPos);
        assertRejected("6a. out-of-bounds transform position rejected");

        auto badScale = MakeValidClipboardEntityJson();
        badScale.insert(QStringLiteral("scale"), QJsonArray{0.0, 1.0, 1.0});
        SetClipboardJsonObject(badScale);
        assertRejected("6b. zero transform scale rejected");

        badScale.insert(QStringLiteral("scale"), QJsonArray{1.0, 1.0});
        SetClipboardJsonObject(badScale);
        assertRejected("6c. wrong transform array length rejected");
    }

    // 7. Abnormal Camera FOV
    {
        auto bad = MakeValidClipboardEntityJson();
        QJsonObject cam = bad.value(QStringLiteral("camera")).toObject();
        cam.insert(QStringLiteral("fov"), 185.0);
        bad.insert(QStringLiteral("camera"), cam);
        SetClipboardJsonObject(bad);
        assertRejected("7a. camera FOV >= 180 rejected");

        cam.insert(QStringLiteral("fov"), 0.0);
        bad.insert(QStringLiteral("camera"), cam);
        SetClipboardJsonObject(bad);
        assertRejected("7b. camera FOV <= 0 rejected");
    }

    // 8. Excessive Tilemap size (> 256x256)
    {
        auto bad = MakeValidClipboardEntityJson();
        QJsonObject map = bad.value(QStringLiteral("tilemap")).toObject();
        map.insert(QStringLiteral("width"), 300);
        map.insert(QStringLiteral("height"), 300);
        bad.insert(QStringLiteral("tilemap"), map);
        SetClipboardJsonObject(bad);
        assertRejected("8. tilemap > 256x256 rejected");
    }

    // 9. Keyframe 4097개 (> 4096)
    {
        auto bad = MakeValidClipboardEntityJson();
        QJsonObject anim = bad.value(QStringLiteral("animation")).toObject();
        QJsonArray manyKeys;
        QJsonObject k;
        k.insert(QStringLiteral("time"), 0.1);
        k.insert(QStringLiteral("position"), QJsonArray{0.0, 0.0, 0.0});
        k.insert(QStringLiteral("rotation"), QJsonArray{0.0, 0.0, 0.0});
        k.insert(QStringLiteral("scale"), QJsonArray{1.0, 1.0, 1.0});
        for (int i = 0; i < 4097; ++i)
            manyKeys.append(k);
        anim.insert(QStringLiteral("keyframes"), manyKeys);
        bad.insert(QStringLiteral("animation"), anim);
        SetClipboardJsonObject(bad);
        assertRejected("9. 4097 keyframes (> 4096) rejected");
    }

    // 10. Invalid UTF-8
    SetClipboardJsonBytes(QByteArray("{\"name\":\"\xFF\xFE\xC0\xAF\",\"position\":[0,0,0],"
                                     "\"rotation\":[0,0,0],\"scale\":[1,1,1]}"));
    assertRejected("10. invalid UTF-8 sequence rejected");

    // 11. Corrupted JSON
    SetClipboardJsonBytes("{corrupted json: [1, 2, ");
    assertRejected("11. corrupted JSON syntax rejected");

    // 12. > 1MB JSON
    {
        QByteArray huge(1024 * 1024 + 512, ' ');
        huge[0] = '{';
        huge[huge.size() - 1] = '}';
        SetClipboardJsonBytes(huge);
        assertRejected("12. oversized >1MB clipboard JSON rejected");
    }

    // 13. Missing required fields ("name", "position", "rotation", "scale")
    {
        auto bad = MakeValidClipboardEntityJson();
        bad.remove(QStringLiteral("position"));
        SetClipboardJsonObject(bad);
        assertRejected("13a. missing position field rejected");

        auto badName = MakeValidClipboardEntityJson();
        badName.remove(QStringLiteral("name"));
        SetClipboardJsonObject(badName);
        assertRejected("13b. missing name field rejected");
    }
    std::cerr << "[Editor_ui] PART 3.2 PASS: All 13 abnormal clipboard inputs rejected without "
                 "mutating scene, undo stack, or selection\n";

    // --- PART 3.4: RecordEdit Exception Rollback & Error Logging ---
    std::cerr
        << "[Editor_ui] PART 3.4: Verifying RecordEdit exception rollback and error logging\n";
    window.ExecuteRecordedEdit(QStringLiteral("강제 오류 편집"), [&] {
        Entity temp = window.GetDocument().CreateEntity("예외임시엔티티");
        temp.AddComponent<SpriteRendererComponent>();
        throw std::runtime_error("의도된 테스트 예외");
    });
    {
        std::string afterExceptionSerialized;
        Check(window.GetDocument().GetScene().SerializeToString(afterExceptionSerialized) &&
                  afterExceptionSerialized == baselineSerialized,
              "RecordEdit rolls back scene state on exception");
        Check(hierarchy->count() == stableEntityCount && undo->count() == stableUndoCount &&
                  undo->index() == stableUndoIndex &&
                  window.LastLogMessage().contains(QStringLiteral("편집 실패")),
              "RecordEdit preserves undo stack and logs user-visible error on exception");
    }
    std::cerr << "[Editor_ui] PART 3.4 PASS: RecordEdit exception recovery verified\n";

    // --- PART 3.3: Actual Pasted Entity Save & Reopen ---
    std::cerr << "[Editor_ui] PART 3.3: Verifying actual pasted entity Scene Save & Reopen\n";
    {
        undo->undo();
        Check(hierarchy->count() == 2, "undo before save roundtrip");
        undo->redo();
        Check(hierarchy->count() == 3, "redo restores pasted entity before save roundtrip");

        std::string preSaveSerialized;
        Check(window.GetDocument().GetScene().SerializeToString(preSaveSerialized) &&
                  preSaveSerialized == baselineSerialized,
              "pre-save serialized scene matches baseline");

        const std::string savedScenePath =
            settings.filePath(QStringLiteral("actual_pasted_editor_scene.lux"))
                .toUtf8()
                .toStdString();
        Check(window.GetDocument().SaveAs(savedScenePath),
              "actual EditorWindow document with pasted entity saves to .lux file");

        EditorDocument reopenedDoc;
        Check(reopenedDoc.Open(savedScenePath), "saved .lux file reopens cleanly");
        Check(reopenedDoc.GetScene().GetEntityCount() == 3,
              "reopened scene has exact entity count (3)");
        Entity reopenedPasted = FindEntityByTag(reopenedDoc.GetScene(), "검증엔티티 복사본");
        VerifyFullClipboardEntityComponents(reopenedPasted);

        std::string postOpenSerialized;
        Check(reopenedDoc.GetScene().SerializeToString(postOpenSerialized) &&
                  postOpenSerialized == preSaveSerialized,
              "pre-serialization and post-deserialization scene data are byte-for-byte identical");

        Check(window.GetDocument().Open(savedScenePath),
              "EditorWindow document reopens saved .lux file");
        Entity windowReopenedPasted =
            FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본");
        VerifyFullClipboardEntityComponents(windowReopenedPasted);
    }
    std::cerr << "[Editor_ui] PART 3.3 PASS: Actual pasted entity save and reopen verified\n";

    undo->undo();
    Check(hierarchy->count() == 2, "undo after save/reopen test returns to 2 entities");

    auto* startPreview = window.findChild<QAction*>(QStringLiteral("previewStart"));
    auto* stepPreview = window.findChild<QAction*>(QStringLiteral("previewStep"));
    auto* stopPreview = window.findChild<QAction*>(QStringLiteral("previewStop"));
    Check(startPreview && stepPreview && stopPreview, "preview controls exist");
    std::cerr << "[Editor_ui] preview\n";
    startPreview->trigger();
    stepPreview->trigger();
    auto* debugState = window.findChild<QTreeWidget*>(QStringLiteral("debugState"));
    Check(debugState && debugState->topLevelItemCount() > 0, "debugger shows runtime state");
    stopPreview->trigger();
    Check(hierarchy->count() == 2, "preview restores editable scene");

    tabs->setCurrentIndex(0);
    std::cerr << "[Editor_ui] offscreen widget render\n";
    QPixmap frame(window.size());
    window.render(&frame);
    const QString screenshot = qEnvironmentVariable("LUX_EDITOR_TEST_SCREENSHOT");
    if (!screenshot.isEmpty())
        frame.save(screenshot);
    Check(!frame.isNull(), "Qt workspace renders offscreen");
    window.resize(960, 600);
    app.processEvents();
    const QString smallScreenshot = qEnvironmentVariable("LUX_EDITOR_TEST_SMALL_SCREENSHOT");
    if (!smallScreenshot.isEmpty()) {
        QPixmap smallFrame(window.size());
        window.render(&smallFrame);
        smallFrame.save(smallScreenshot);
    }
    while (undo->canUndo())
        undo->undo();
    std::cerr << "[Editor_ui] closing window\n";
    window.close();
    return 0;
}

void PrintCheckpointComparison(const char* title, const StressCheckpoint& cpFirst,
                               const StressCheckpoint& cp100, const StressCheckpoint& cp500,
                               const StressCheckpoint& cp1000, const StressCheckpoint& cpLast) {
    std::cerr << "------------------------------------------------------------\n"
              << "  [" << title << " Checkpoint Comparison (Active vs Cumulative)]\n";
    auto printRow = [](const char* label, const StressCheckpoint& cp) {
        if (cp.iteration == 0)
            return;
        std::cerr << "    " << label << " (iter=" << cp.iteration << "): "
                  << "CPU[workingSet=" << (cp.cpu.workingSetBytes / 1024)
                  << "KB, private=" << (cp.cpu.privateBytes / 1024) << "KB] | "
                  << "GPU_ACTIVE[allocCount=" << cp.gpuMem.allocationCount
                  << ", allocBytes=" << cp.gpuMem.allocationBytes
                  << ", liveBuffers=" << cp.gpuMem.liveBuffers
                  << ", liveTextures=" << cp.gpuMem.liveTextures
                  << ", pendingDestroy=" << cp.gpuMem.pendingDestructions
                  << ", deviceLocal=" << cp.gpuMem.deviceLocalUsage << "] | "
                  << "GPU_CUMULATIVE[framesSubmitted=" << cp.cumulativeFramesSubmitted
                  << ", submissions=" << cp.cumulativeSubmissions
                  << ", pipelinesCreated=" << cp.cumulativePipelinesCreated
                  << ", validationErrors=" << cp.cumulativeValidationErrors << "]\n";
        if (cp.fourStage.iteration != 0) {
            const auto& fs = cp.fourStage;
            const auto& det = fs.detailedPostDestroy;
            std::cerr << "      -> 4-Stage CPU PrivateKB: [preInit="
                      << (fs.preInit.privateBytes / 1024)
                      << "KB -> postRender=" << (fs.postRender.privateBytes / 1024)
                      << "KB -> postShutdown=" << (fs.postShutdown.privateBytes / 1024)
                      << "KB -> postDestroy=" << (fs.postDestroy.privateBytes / 1024) << "KB"
                      << " (reclaimedOnShutdown="
                      << (static_cast<long long>(fs.postRender.privateBytes) -
                          static_cast<long long>(fs.postShutdown.privateBytes)) /
                             1024
                      << "KB, netIterDelta="
                      << (static_cast<long long>(fs.postDestroy.privateBytes) -
                          static_cast<long long>(fs.preInit.privateBytes)) /
                             1024
                      << "KB)]\n";
            if (det.privateBytes != 0) {
                std::cerr << "      -> Post-Destroy Compacted Breakdown: [defaultHeapBusy="
                          << (det.defaultHeapBusyBytes / 1024) << "KB ("
                          << det.defaultHeapBusyBlocks
                          << " blks), secondaryHeapsBusy=" << (det.secondaryHeapsBusyBytes / 1024)
                          << "KB (" << det.secondaryHeapsBusyBlocks
                          << " blks, heaps=" << det.heapCount
                          << "), vmPrivateCommit=" << (det.vmPrivateCommitBytes / 1024)
                          << "KB, vmMappedCommit=" << (det.vmMappedCommitBytes / 1024)
                          << "KB, vmImageCommit=" << (det.vmImageCommitBytes / 1024) << "KB]\n";
            }
        }
    };
    printRow("Iter 1   ", cpFirst);
    printRow("Iter 100 ", cp100);
    printRow("Iter 500 ", cp500);
    printRow("Iter 1000", cp1000);
    if (cpLast.iteration != cpFirst.iteration && cpLast.iteration != cp100.iteration &&
        cpLast.iteration != cp500.iteration && cpLast.iteration != cp1000.iteration) {
        printRow("Iter Last", cpLast);
    }
    std::cerr << "------------------------------------------------------------\n";
}

void PrintIsolationDelta(const char* name, int iters, const DetailedCpuMemorySample& before,
                         const DetailedCpuMemorySample& after) {
    const long long privDeltaKB =
        (static_cast<long long>(after.privateBytes) - static_cast<long long>(before.privateBytes)) /
        1024;
    const long long wsDeltaKB = (static_cast<long long>(after.workingSetBytes) -
                                 static_cast<long long>(before.workingSetBytes)) /
                                1024;
    const long long defHeapDeltaBytes = static_cast<long long>(after.defaultHeapBusyBytes) -
                                        static_cast<long long>(before.defaultHeapBusyBytes);
    const long long defHeapDeltaBlks = static_cast<long long>(after.defaultHeapBusyBlocks) -
                                       static_cast<long long>(before.defaultHeapBusyBlocks);
    const long long secHeapDeltaBytes = static_cast<long long>(after.secondaryHeapsBusyBytes) -
                                        static_cast<long long>(before.secondaryHeapsBusyBytes);
    const long long vmPrivDeltaKB = (static_cast<long long>(after.vmPrivateCommitBytes) -
                                     static_cast<long long>(before.vmPrivateCommitBytes)) /
                                    1024;
    const long long vmMapDeltaKB = (static_cast<long long>(after.vmMappedCommitBytes) -
                                    static_cast<long long>(before.vmMappedCommitBytes)) /
                                   1024;
    const long long vmImgDeltaKB = (static_cast<long long>(after.vmImageCommitBytes) -
                                    static_cast<long long>(before.vmImageCommitBytes)) /
                                   1024;
    std::cerr << "  [Isolation: " << name << " (" << iters << "x)]\n"
              << "    PrivateUsage : " << (before.privateBytes / 1024) << "KB -> "
              << (after.privateBytes / 1024) << "KB (delta=" << privDeltaKB << "KB, "
              << (iters > 0 ? static_cast<double>(privDeltaKB) / iters : 0.0) << " KB/iter)\n"
              << "    WorkingSet   : " << (before.workingSetBytes / 1024) << "KB -> "
              << (after.workingSetBytes / 1024) << "KB (delta=" << wsDeltaKB << "KB)\n"
              << "    DefaultHeap  : " << (before.defaultHeapBusyBytes / 1024) << "KB -> "
              << (after.defaultHeapBusyBytes / 1024) << "KB (delta=" << defHeapDeltaBytes
              << " bytes, " << defHeapDeltaBlks << " blocks)\n"
              << "    SecondHeaps  : " << (before.secondaryHeapsBusyBytes / 1024) << "KB -> "
              << (after.secondaryHeapsBusyBytes / 1024) << "KB (delta=" << secHeapDeltaBytes
              << " bytes, heaps=" << before.heapCount << "->" << after.heapCount << ")\n"
              << "    VM Commit    : PrivateDelta=" << vmPrivDeltaKB
              << "KB, MappedDelta=" << vmMapDeltaKB << "KB, ImageDelta=" << vmImgDeltaKB << "KB\n";
}

int RunGpuLifecycleTests(QApplication& app, const StressConfig& cfg) {
    using namespace LuxEngine;
    const auto overallStart = std::chrono::steady_clock::now();
    std::cerr << "[Editor_gpu_lifecycle] probing Vulkan GPU availability (gpuIterations="
              << cfg.gpuIterations << ", viewportIterations=" << cfg.viewportIterations << ")\n";

    EngineSceneRenderer::SetForceAllowed(true);
    // Request Vulkan Synchronization Validation (falls back to standard Validation or Off if layer
    // missing)
    EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::Synchronization));

    GpuDeviceInfo deviceInfo{};
    {
        EngineSceneRenderer probe;
        if (!probe.EnsureReady() || !probe.GetGpuDevice()) {
            std::cerr << "[Editor_gpu_lifecycle] SKIPPED: No Vulkan GPU available ("
                      << probe.LastError().toStdString() << ")\n";
            EngineSceneRenderer::SetForceAllowed(false);
            return kSkipReturnCode;
        }
        deviceInfo = probe.GetGpuDevice()->GetInfo();
        probe.Shutdown();
    }

    std::cerr << "============================================================\n"
              << "[Editor_gpu_lifecycle] Vulkan GPU Detected\n"
              << "  GPU Model              : " << deviceInfo.deviceName << " ("
              << deviceInfo.deviceType << ")\n"
              << "  Vulkan API Version     : " << FormatVulkanVersion(deviceInfo.apiVersion)
              << " (raw=" << deviceInfo.apiVersion << ")\n"
              << "  Vulkan Driver Version  : " << FormatVulkanVersion(deviceInfo.driverVersion)
              << " (raw=" << deviceInfo.driverVersion << ")\n"
              << "  Validation Enabled     : " << (deviceInfo.validation ? "YES" : "NO") << "\n"
              << "  Sync Validation        : "
              << (deviceInfo.synchronizationValidation ? "YES" : "NO") << "\n"
              << "  Target GPU Lifecycle   : " << cfg.gpuIterations << " iterations\n"
              << "  Target Viewport Stress : " << cfg.viewportIterations << " iterations\n"
              << "============================================================\n";

    Scene scene2D;
    Entity quad = scene2D.CreateEntity("Quad2D");
    quad.GetComponent<TransformComponent>().position = {0.0f, 0.0f, 0.0f};
    quad.GetComponent<TransformComponent>().scale = {4.0f, 4.0f, 1.0f};
    quad.AddComponent<SpriteRendererComponent>().color = {0.9f, 0.2f, 0.2f, 1.0f};

    Scene scene3D;
    Entity cube = scene3D.CreateEntity("Cube3D");
    cube.GetComponent<TransformComponent>().position = {0.0f, 0.0f, 0.0f};
    cube.GetComponent<TransformComponent>().scale = {1.5f, 1.5f, 1.5f};
    cube.AddComponent<MeshRendererComponent>().material.albedoColor = {0.2f, 0.8f, 0.3f, 1.0f};

    uint32 totalValidationErrors = 0;
    uint64 totalFramesSubmitted = 0;
    uint64 totalSubmissions = 0;
    uint64 totalPipelinesCreated = 0;

    // --- PART 2.0: Controlled CPU Memory Isolation Tests (Separating SDL Window, Device
    // Init/Shutdown ONLY vs. Full Render/Readback, and Validation OFF vs. ON vs. Synchronization)
    // ---
    constexpr int kIsolationIterations = 50;
    std::cerr << "[Editor_gpu_lifecycle] PART 2.0: Running " << kIsolationIterations
              << "x controlled CPU memory isolation sub-tests...\n";
    {
        // Sub-test 2.0A: SDL_InitSubSystem + Window Create/Destroy + SDL_QuitSubSystem ONLY
        const auto beforeSdl = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            Check(SDL_InitSubSystem(SDL_INIT_VIDEO), "Isolation SDL_InitSubSystem");
            Window win;
            WindowProps props;
            props.title = "LuxIsolationWindow";
            props.width = 640;
            props.height = 360;
            props.resizable = true;
            props.vulkan = true;
            props.hidden = true;
            props.vsync = false;
            Check(win.Initialize(props), "Isolation Window::Initialize");
            win.Shutdown();
            SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        const auto afterSdl = QueryDetailedCpuMemory(true);
        PrintIsolationDelta("2.0A: SDL Window Create+Destroy ONLY (No Vulkan Device)",
                            kIsolationIterations, beforeSdl, afterSdl);

        // Sub-test 2.0B: GPU Device Init + Shutdown ONLY (No Render, No Readback), Validation = OFF
        EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::Off));
        {
            EngineSceneRenderer warmup;
            Check(warmup.EnsureReady(), "Warmup Off");
            warmup.Shutdown();
        }
        const auto beforeInitOff = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            auto r = std::make_unique<EngineSceneRenderer>();
            Check(r->EnsureReady() && r->GetGpuDevice() != nullptr, "Isolation InitOnly Val=Off");
            Check(r->GetGpuDevice()->WaitIdle(), "Isolation WaitIdle Val=Off");
            r->Shutdown();
            r.reset();
        }
        const auto afterInitOff = QueryDetailedCpuMemory(true);
        PrintIsolationDelta(
            "2.0B: GpuDevice Init+Shutdown ONLY (No Render/Readback, Validation=OFF)",
            kIsolationIterations, beforeInitOff, afterInitOff);

        // Sub-test 2.0C: Full Init + 2D/3D Render + QImage Readback + Shutdown, Validation = OFF
        const auto beforeFullOff = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            auto r = std::make_unique<EngineSceneRenderer>();
            Check(r->EnsureReady() && r->GetGpuDevice() != nullptr, "Isolation Full Val=Off");
            QImage img2D, img3D;
            Check(r->Render(scene2D, 160, 120, QRectF(-5.0, 5.0, 10.0, -10.0), img2D) &&
                      !img2D.isNull(),
                  "Isolation Full 2D Val=Off");
            Check(r->Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, img3D) &&
                      !img3D.isNull(),
                  "Isolation Full 3D Val=Off");
            Check(r->GetGpuDevice()->WaitIdle(), "Isolation Full WaitIdle Val=Off");
            r->Shutdown();
            r.reset();
        }
        const auto afterFullOff = QueryDetailedCpuMemory(true);
        PrintIsolationDelta("2.0C: Full Init+Render2D/3D+Readback+Shutdown (Validation=OFF)",
                            kIsolationIterations, beforeFullOff, afterFullOff);

        // Sub-test 2.0D: GPU Device Init + Shutdown ONLY (No Render, No Readback), Validation = ON
        EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::On));
        const auto beforeInitOn = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            auto r = std::make_unique<EngineSceneRenderer>();
            Check(r->EnsureReady() && r->GetGpuDevice() != nullptr, "Isolation InitOnly Val=On");
            Check(r->GetGpuDevice()->WaitIdle(), "Isolation WaitIdle Val=On");
            totalValidationErrors += r->GetGpuDevice()->GetCounters().validationErrors;
            r->Shutdown();
            r.reset();
        }
        const auto afterInitOn = QueryDetailedCpuMemory(true);
        PrintIsolationDelta(
            "2.0D: GpuDevice Init+Shutdown ONLY (No Render/Readback, Validation=ON)",
            kIsolationIterations, beforeInitOn, afterInitOn);

        // Sub-test 2.0E: GPU Device Init + Shutdown ONLY (No Render, No Readback), Validation =
        // Synchronization
        EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::Synchronization));
        const auto beforeInitSync = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            auto r = std::make_unique<EngineSceneRenderer>();
            Check(r->EnsureReady() && r->GetGpuDevice() != nullptr, "Isolation InitOnly Val=Sync");
            Check(r->GetGpuDevice()->WaitIdle(), "Isolation WaitIdle Val=Sync");
            totalValidationErrors += r->GetGpuDevice()->GetCounters().validationErrors;
            r->Shutdown();
            r.reset();
        }
        const auto afterInitSync = QueryDetailedCpuMemory(true);
        PrintIsolationDelta(
            "2.0E: GpuDevice Init+Shutdown ONLY (No Render/Readback, Validation=Synchronization)",
            kIsolationIterations, beforeInitSync, afterInitSync);

        // Sub-test 2.0F: Full Init + 2D/3D Render + QImage Readback + Shutdown, Validation =
        // Synchronization
        const auto beforeFullSync = QueryDetailedCpuMemory(true);
        for (int i = 0; i < kIsolationIterations; ++i) {
            auto r = std::make_unique<EngineSceneRenderer>();
            Check(r->EnsureReady() && r->GetGpuDevice() != nullptr, "Isolation Full Val=Sync");
            QImage img2D, img3D;
            Check(r->Render(scene2D, 160, 120, QRectF(-5.0, 5.0, 10.0, -10.0), img2D) &&
                      !img2D.isNull(),
                  "Isolation Full 2D Val=Sync");
            Check(r->Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, img3D) &&
                      !img3D.isNull(),
                  "Isolation Full 3D Val=Sync");
            Check(r->GetGpuDevice()->WaitIdle(), "Isolation Full WaitIdle Val=Sync");
            totalValidationErrors += r->GetGpuDevice()->GetCounters().validationErrors;
            r->Shutdown();
            r.reset();
        }
        const auto afterFullSync = QueryDetailedCpuMemory(true);
        PrintIsolationDelta(
            "2.0F: Full Init+Render2D/3D+Readback+Shutdown (Validation=Synchronization)",
            kIsolationIterations, beforeFullSync, afterFullSync);
    }
    EngineSceneRenderer::SetDefaultValidation(static_cast<int>(GpuValidation::Synchronization));

    // --- PART 2.1: Full EngineSceneRenderer Create -> Init -> 2D/3D Render -> QImage Readback ->
    // WaitIdle -> Shutdown -> Resource Release (cfg.gpuIterations times) ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.1: Starting " << cfg.gpuIterations
              << "x full EngineSceneRenderer create-render-readback-waitidle-shutdown loop...\n";
    const auto loopStart = std::chrono::steady_clock::now();
    StressCheckpoint cpFirst{}, cp100{}, cp500{}, cp1000{}, cpLast{};
    int completedIterations = 0;

    for (int i = 0; i < cfg.gpuIterations; ++i) {
        const int iterNum = i + 1;
        FourStageCpuSample stageSample{};
        stageSample.iteration = iterNum;
        stageSample.preInit = QueryCpuMemory();

        auto renderer = std::make_unique<EngineSceneRenderer>();
        CheckWithGpuDiagnostics(renderer->EnsureReady() && renderer->IsReady() &&
                                    renderer->GetGpuDevice() != nullptr,
                                "EngineSceneRenderer::EnsureReady", iterNum, renderer.get(),
                                "EngineSceneRenderer failed to initialize GpuDevice and contexts");

        {
            QImage img2D;
            CheckWithGpuDiagnostics(
                renderer->Render(scene2D, 160, 120, QRectF(-5.0, 5.0, 10.0, -10.0), img2D),
                "EngineSceneRenderer::Render2D", iterNum, renderer.get(), "2D render failed");
            CheckWithGpuDiagnostics(!img2D.isNull() && img2D.width() == 160 &&
                                        img2D.height() == 120 &&
                                        HasNonBackgroundPixel(img2D, 30, 34, 42),
                                    "EngineSceneRenderer::Readback2D", iterNum, renderer.get(),
                                    "2D QImage readback invalid or missing rendered quad pixels");

            QImage img3D;
            CheckWithGpuDiagnostics(
                renderer->Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, img3D),
                "EngineSceneRenderer::Render3D", iterNum, renderer.get(), "3D render failed");
            CheckWithGpuDiagnostics(!img3D.isNull() && img3D.width() == 160 &&
                                        img3D.height() == 120 &&
                                        HasNonBackgroundPixel(img3D, 27, 33, 42),
                                    "EngineSceneRenderer::Readback3D", iterNum, renderer.get(),
                                    "3D QImage readback invalid or missing rendered mesh pixels");

            CheckWithGpuDiagnostics(renderer->GetGpuDevice()->WaitIdle(), "GpuDevice::WaitIdle",
                                    iterNum, renderer.get(), "GpuDevice WaitIdle failed");

            stageSample.postRender = QueryCpuMemory();
        }

        const auto counters = renderer->GetGpuDevice()->GetCounters();
        const auto memStats = renderer->GetGpuDevice()->GetMemoryStats();
        totalValidationErrors += counters.validationErrors;
        totalFramesSubmitted += counters.framesSubmitted;
        totalSubmissions += counters.submissions;
        totalPipelinesCreated += counters.pipelinesCreated;

        CheckWithGpuDiagnostics(counters.validationErrors == 0, "VulkanValidation", iterNum,
                                renderer.get(),
                                "Vulkan validation error detected during EngineSceneRenderer loop");
        CheckWithGpuDiagnostics(
            memStats.liveBuffers == 0 && memStats.liveTextures == 1 &&
                memStats.pendingDestructions == 0 && memStats.pendingStagingBytes == 0 &&
                memStats.allocationCount > 0 && memStats.allocationBytes > 0,
            "ActiveGpuResourceCheck", iterNum, renderer.get(),
            "Active GPU resource counts deviated from expected baseline before Shutdown");

        // Verify post-shutdown context safety when tearing down
        Renderer2D::Context observer2D(renderer->GetGpuDevice());
        Renderer3D::Context observer3D(renderer->GetGpuDevice());
        CheckWithGpuDiagnostics(observer2D.IsDeviceValid() && observer3D.IsDeviceValid(),
                                "ContextValidBeforeShutdown", iterNum, renderer.get(),
                                "Observer contexts should be valid while GpuDevice is alive");

        renderer->Shutdown();
        stageSample.postShutdown = QueryCpuMemory();

        CheckWithGpuDiagnostics(!renderer->IsReady() && renderer->GetGpuDevice() == nullptr,
                                "EngineSceneRenderer::Shutdown", iterNum, renderer.get(),
                                "Shutdown did not release GpuDevice");
        CheckWithGpuDiagnostics(!observer2D.IsDeviceValid() && !observer3D.IsDeviceValid(),
                                "ContextInvalidAfterShutdown", iterNum, renderer.get(),
                                "Observer contexts did not detect destroyed GpuDevice");
        observer2D.Shutdown();
        observer3D.Shutdown();

        renderer.reset();

        const bool isMilestone = (iterNum == 1 || iterNum == 100 || iterNum == 500 ||
                                  iterNum == 1000 || iterNum == cfg.gpuIterations);
        if (isMilestone) {
            stageSample.detailedPostDestroy = QueryDetailedCpuMemory(true);
            stageSample.postDestroy.workingSetBytes =
                stageSample.detailedPostDestroy.workingSetBytes;
            stageSample.postDestroy.privateBytes = stageSample.detailedPostDestroy.privateBytes;
        } else {
            stageSample.postDestroy = QueryCpuMemory();
        }

        StressCheckpoint currentCp{};
        currentCp.iteration = iterNum;
        currentCp.cpu = stageSample.postRender;
        currentCp.fourStage = stageSample;
        currentCp.gpuMem = memStats;
        currentCp.gpuCounters = counters;
        currentCp.cumulativeFramesSubmitted = totalFramesSubmitted;
        currentCp.cumulativeSubmissions = totalSubmissions;
        currentCp.cumulativePipelinesCreated = totalPipelinesCreated;
        currentCp.cumulativeValidationErrors = totalValidationErrors;

        if (iterNum == 1)
            cpFirst = currentCp;
        if (iterNum == 100)
            cp100 = currentCp;
        if (iterNum == 500)
            cp500 = currentCp;
        if (iterNum == 1000)
            cp1000 = currentCp;
        if (iterNum == cfg.gpuIterations)
            cpLast = currentCp;

        ++completedIterations;
        if (iterNum % 10 == 0 || iterNum == 1 || iterNum == cfg.gpuIterations) {
            std::cerr << "  [GPU Lifecycle] iter " << iterNum << "/" << cfg.gpuIterations
                      << " | CPU_PRIV_4STAGE[pre=" << (stageSample.preInit.privateBytes / 1024)
                      << "KB, render=" << (stageSample.postRender.privateBytes / 1024)
                      << "KB, shut=" << (stageSample.postShutdown.privateBytes / 1024)
                      << "KB, destroy=" << (stageSample.postDestroy.privateBytes / 1024) << "KB]"
                      << " | GPU_ACTIVE[allocs=" << memStats.allocationCount
                      << ", bytes=" << memStats.allocationBytes << ", buf=" << memStats.liveBuffers
                      << ", tex=" << memStats.liveTextures
                      << ", pend=" << memStats.pendingDestructions
                      << ", vram=" << memStats.deviceLocalUsage << "]"
                      << " | GPU_CUM[frames=" << totalFramesSubmitted
                      << ", submits=" << totalSubmissions << ", pipes=" << totalPipelinesCreated
                      << ", valErr=" << totalValidationErrors << "]\n";
        }
    }

    const double loopSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - loopStart).count();
    PrintCheckpointComparison("EngineSceneRenderer Full Lifecycle", cpFirst, cp100, cp500, cp1000,
                              cpLast);

    Check(completedIterations == cfg.gpuIterations &&
              cpFirst.gpuMem.allocationCount == cpLast.gpuMem.allocationCount &&
              cpFirst.gpuMem.allocationBytes == cpLast.gpuMem.allocationBytes &&
              cpFirst.gpuMem.liveBuffers == cpLast.gpuMem.liveBuffers &&
              cpFirst.gpuMem.liveTextures == cpLast.gpuMem.liveTextures &&
              cpLast.gpuMem.pendingDestructions == 0,
          "EngineSceneRenderer lifecycle completed with zero active GPU resource or allocation "
          "drift");
    if (cp100.iteration > 0 && cp500.iteration > 0 && cp1000.iteration > 0) {
        Check(cp100.gpuMem.allocationCount == cp500.gpuMem.allocationCount &&
                  cp500.gpuMem.allocationCount == cp1000.gpuMem.allocationCount &&
                  cp100.gpuMem.allocationBytes == cp500.gpuMem.allocationBytes &&
                  cp500.gpuMem.allocationBytes == cp1000.gpuMem.allocationBytes &&
                  cp100.gpuMem.liveBuffers == cp500.gpuMem.liveBuffers &&
                  cp500.gpuMem.liveBuffers == cp1000.gpuMem.liveBuffers &&
                  cp100.gpuMem.liveTextures == cp500.gpuMem.liveTextures &&
                  cp500.gpuMem.liveTextures == cp1000.gpuMem.liveTextures,
              "Active GPU allocations and resource counts at 100, 500, and 1000 iterations are "
              "strictly constant (zero leak)");
    }

    // --- PART 2.2: 2D/3D Viewport Create/Destroy & Alternating Destruction Order (A->B vs B->A)
    // for cfg.viewportIterations iterations ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.2: Starting " << cfg.viewportIterations
              << "x 2D/3D viewport creation/destruction & alternating teardown order (A->B / "
                 "B->A)...\n";
    const auto vpStart = std::chrono::steady_clock::now();
    StressCheckpoint vpCpFirst{}, vpCp100{}, vpCp500{}, vpCp1000{}, vpCpLast{};
    {
        EditorDocument doc;
        auto rendererA = std::make_shared<EngineSceneRenderer>();
        auto rendererB = std::make_shared<EngineSceneRenderer>();
        CheckWithGpuDiagnostics(rendererA->EnsureReady(), "ViewportStress::InitA", 0,
                                rendererA.get(), "rendererA failed to initialize");
        CheckWithGpuDiagnostics(rendererB->EnsureReady(), "ViewportStress::InitB", 0,
                                rendererB.get(), "rendererB failed to initialize");

        QImage warmupA, warmupB;
        CheckWithGpuDiagnostics(
            rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), warmupA) &&
                rendererA->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, warmupA),
            "ViewportStress::WarmupA", 0, rendererA.get(), "rendererA warmup failed");
        CheckWithGpuDiagnostics(
            rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0, warmupB) &&
                rendererB->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), warmupB),
            "ViewportStress::WarmupB", 0, rendererB.get(), "rendererB warmup failed");
        CheckWithGpuDiagnostics(
            rendererA->GetGpuDevice()->WaitIdle() && rendererB->GetGpuDevice()->WaitIdle(),
            "ViewportStress::WarmupWaitIdle", 0, rendererA.get(), "warmup WaitIdle failed");

        for (int i = 0; i < cfg.viewportIterations; ++i) {
            const int iterNum = i + 1;
            const bool close2DFirst = (i % 2 == 0);

            auto ctx2D = std::make_unique<Renderer2D::Context>(rendererA->GetGpuDevice());
            auto ctx3D = std::make_unique<Renderer3D::Context>(rendererB->GetGpuDevice());
            CheckWithGpuDiagnostics(ctx2D->IsDeviceValid() && ctx3D->IsDeviceValid(),
                                    "ViewportStress::ContextInit", iterNum, rendererA.get(),
                                    "2D or 3D context invalid");

            auto vp2D = std::make_unique<SceneViewport2D>(
                nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [](auto, auto, auto, auto) {},
                [] {}, [] {}, rendererA);
            auto vp3D = std::make_unique<SceneViewport3D>(
                nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [] {}, [] {}, rendererB);
            vp2D->resize(200, 150);
            vp3D->resize(200, 150);

            QImage imgA, imgB;
            if (close2DFirst) {
                // Close 2D viewport & context first, then verify 3D viewport/renderer still renders
                vp2D.reset();
                ctx2D->Shutdown();
                ctx2D.reset();
                CheckWithGpuDiagnostics(
                    rendererB->IsReady() &&
                        rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0,
                                            imgB) &&
                        !imgB.isNull(),
                    "ViewportStress::Render3DAfter2DClosed", iterNum, rendererB.get(),
                    "3D viewport render failed after 2D viewport closed first");
                vp3D.reset();
                ctx3D->Shutdown();
                ctx3D.reset();
            } else {
                // Close 3D viewport & context first, then verify 2D viewport/renderer still renders
                vp3D.reset();
                ctx3D->Shutdown();
                ctx3D.reset();
                CheckWithGpuDiagnostics(
                    rendererA->IsReady() &&
                        rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0),
                                          imgA) &&
                        !imgA.isNull(),
                    "ViewportStress::Render2DAfter3DClosed", iterNum, rendererA.get(),
                    "2D viewport render failed after 3D viewport closed first");
                vp2D.reset();
                ctx2D->Shutdown();
                ctx2D.reset();
            }

            if (iterNum % 10 == 0 || iterNum == 1 || iterNum == cfg.viewportIterations) {
                CheckWithGpuDiagnostics(rendererA->GetGpuDevice()->WaitIdle() &&
                                            rendererB->GetGpuDevice()->WaitIdle(),
                                        "ViewportStress::WaitIdle", iterNum, rendererA.get(),
                                        "WaitIdle failed during viewport loop");
                const auto memA = rendererA->GetGpuDevice()->GetMemoryStats();
                const auto memB = rendererB->GetGpuDevice()->GetMemoryStats();
                const auto cntA = rendererA->GetGpuDevice()->GetCounters();
                const auto cntB = rendererB->GetGpuDevice()->GetCounters();
                CheckWithGpuDiagnostics(cntA.validationErrors == 0 && cntB.validationErrors == 0,
                                        "ViewportStress::Validation", iterNum, rendererA.get(),
                                        "validation error during viewport loop");

                StressCheckpoint cp{};
                cp.iteration = iterNum;
                cp.cpu = QueryCpuMemory();
                cp.gpuMem.allocationCount = memA.allocationCount + memB.allocationCount;
                cp.gpuMem.allocationBytes = memA.allocationBytes + memB.allocationBytes;
                cp.gpuMem.liveBuffers = memA.liveBuffers + memB.liveBuffers;
                cp.gpuMem.liveTextures = memA.liveTextures + memB.liveTextures;
                cp.gpuMem.pendingDestructions = memA.pendingDestructions + memB.pendingDestructions;
                cp.gpuMem.deviceLocalUsage = memA.deviceLocalUsage + memB.deviceLocalUsage;
                cp.cumulativeFramesSubmitted = cntA.framesSubmitted + cntB.framesSubmitted;
                cp.cumulativeSubmissions = cntA.submissions + cntB.submissions;
                cp.cumulativePipelinesCreated = cntA.pipelinesCreated + cntB.pipelinesCreated;
                cp.cumulativeValidationErrors = cntA.validationErrors + cntB.validationErrors;

                if (iterNum == 1)
                    vpCpFirst = cp;
                if (iterNum == 100)
                    vpCp100 = cp;
                if (iterNum == 500)
                    vpCp500 = cp;
                if (iterNum == 1000)
                    vpCp1000 = cp;
                if (iterNum == cfg.viewportIterations)
                    vpCpLast = cp;

                std::cerr << "  [Viewport 2D/3D Order=" << (close2DFirst ? "2D->3D" : "3D->2D")
                          << "] iter " << iterNum << "/" << cfg.viewportIterations
                          << " | CPU[ws=" << (cp.cpu.workingSetBytes / 1024)
                          << "KB, priv=" << (cp.cpu.privateBytes / 1024) << "KB]"
                          << " | GPU_ACTIVE[allocs=" << cp.gpuMem.allocationCount
                          << ", bytes=" << cp.gpuMem.allocationBytes
                          << ", buf=" << cp.gpuMem.liveBuffers << ", tex=" << cp.gpuMem.liveTextures
                          << ", pend=" << cp.gpuMem.pendingDestructions << "]"
                          << " | GPU_CUM[frames=" << cp.cumulativeFramesSubmitted
                          << ", valErr=" << cp.cumulativeValidationErrors << "]\n";
            }
        }

        totalValidationErrors += rendererA->GetGpuDevice()->GetCounters().validationErrors +
                                 rendererB->GetGpuDevice()->GetCounters().validationErrors;
        rendererA->Shutdown();
        rendererB->Shutdown();
    }
    const double vpSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - vpStart).count();
    PrintCheckpointComparison("Multi-Viewport 2D/3D Alternating Teardown", vpCpFirst, vpCp100,
                              vpCp500, vpCp1000, vpCpLast);
    Check(vpCpFirst.gpuMem.allocationCount == vpCpLast.gpuMem.allocationCount &&
              vpCpFirst.gpuMem.allocationBytes == vpCpLast.gpuMem.allocationBytes &&
              vpCpFirst.gpuMem.liveBuffers == vpCpLast.gpuMem.liveBuffers &&
              vpCpFirst.gpuMem.liveTextures == vpCpLast.gpuMem.liveTextures &&
              vpCpLast.gpuMem.pendingDestructions == 0,
          "zero GPU resource drift after multi-viewport alternating teardown loop");

    // Also verify full EngineSceneRenderer A & B destruction in both orders (A->B and B->A)
    for (int closeOrder = 0; closeOrder < 2; ++closeOrder) {
        auto rendererA = std::make_unique<EngineSceneRenderer>();
        auto rendererB = std::make_unique<EngineSceneRenderer>();
        QImage imgA, imgB;
        Check(rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgA),
              "viewport A initial 2D render");
        Check(rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0, imgB),
              "viewport B initial 3D render");
        if (closeOrder == 0) {
            rendererA.reset();
            Check(rendererB->IsReady() &&
                      rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0,
                                          imgB) &&
                      rendererB->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgB),
                  "viewport B renders 3D & 2D after viewport A GpuDevice is destroyed first");
            totalValidationErrors += rendererB->GetGpuDevice()->GetCounters().validationErrors;
        } else {
            rendererB.reset();
            Check(
                rendererA->IsReady() &&
                    rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgA) &&
                    rendererA->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.3, 0.1, 4.0, imgA),
                "viewport A renders 2D & 3D after viewport B GpuDevice is destroyed first");
            totalValidationErrors += rendererA->GetGpuDevice()->GetCounters().validationErrors;
        }
    }

    // --- PART 2.3: Resize, Mid-Render Shutdown, Post-Shutdown Context Guard & DeviceLost Fault
    // Injection ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.3: Resize, mid-render Shutdown, and DeviceLost "
                 "fault injection tests...\n";
    {
        EngineSceneRenderer renderer;
        QImage output;
        const std::pair<int, int> sizes[] = {{160, 120}, {320, 240}, {256, 256},
                                             {400, 220}, {128, 96},  {360, 180}};
        for (const auto& [w, h] : sizes) {
            Check(renderer.Render(scene2D, w, h, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                      output.width() == w && output.height() == h,
                  "2D viewport resize during rendering matches target dimensions");
            Check(renderer.Render3D(scene3D, w, h, {0.0f, 0.0f, 0.0f}, 0.2, 0.2, 5.0, output) &&
                      output.width() == w && output.height() == h,
                  "3D viewport resize during rendering matches target dimensions");
        }

        // Request Shutdown() while rendering is in progress
        bool hookCalled = false;
        renderer.SetPostSceneHookForTesting([&] {
            hookCalled = true;
            renderer.Shutdown();
            Check(renderer.IsReady() && renderer.GetGpuDevice() != nullptr,
                  "GpuDevice remains alive while mid-frame render is still executing");
        });
        Check(renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                  hookCalled && !output.isNull(),
              "in-progress frame completes cleanly when Shutdown() is requested mid-render");
        renderer.SetPostSceneHookForTesting(nullptr);
        Check(!renderer.IsReady() && renderer.GetGpuDevice() == nullptr,
              "deferred Shutdown() tears down GpuDevice immediately after frame completion");

        // Re-enable via Reset() and test post-Shutdown Context usage prevention
        renderer.Reset();
        Check(renderer.EnsureReady(), "renderer re-initializes after Reset()");
        {
            Renderer2D::Context detached2D(renderer.GetGpuDevice());
            Renderer3D::Context detached3D(renderer.GetGpuDevice());
            detached2D.Shutdown();
            detached3D.Shutdown();
            Check(!detached2D.IsDeviceValid() && !detached3D.IsDeviceValid(),
                  "Shutdown contexts report !IsDeviceValid()");
            OrthographicCamera ortho(-5.0f, 5.0f, -5.0f, 5.0f);
            detached2D.BeginScene(ortho);
            detached2D.DrawQuad(Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec4{1.0f, 1.0f, 1.0f, 1.0f});
            detached2D.EndScene();

            PerspectiveCamera persp(1.0f, 1.0f, 0.1f, 100.0f);
            DirectionalLight light;
            detached3D.BeginScene(persp, light);
            detached3D.DrawCube(Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, Material3D{});
            detached3D.EndScene();
        }

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
        renderer.Shutdown();
        renderer.Reset();
        Vk::ClearFaults();
        Vk::InjectFault(Vk::Fault::InstanceCreation);
        Check(!renderer.EnsureReady() && !renderer.LastError().isEmpty(),
              "GPU initialization failure reported cleanly");
        Vk::ClearFaults();
        renderer.Reset();
        Check(renderer.EnsureReady() &&
                  renderer.Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.0, 0.0, 5.0, output),
              "EngineSceneRenderer recovers after Reset following init failure");

        // DeviceLost recovery (up to kMaxDeviceRecoveries = 3) and recovery limit enforcement on
        // 4th
        for (uint32 expectedRecovery = 1; expectedRecovery <= 3; ++expectedRecovery) {
            Vk::InjectFault(Vk::Fault::DeviceLost);
            Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output),
                  "DeviceLost fault fails the active frame cleanly");
            Vk::ClearFaults();
            Check(renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                      renderer.IsReady() && renderer.GetRecoveryCount() == expectedRecovery,
                  "EngineSceneRenderer automatically recovers after DeviceLost within limit");
        }
        Vk::InjectFault(Vk::Fault::DeviceLost);
        Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output),
              "4th DeviceLost fails active frame");
        Vk::ClearFaults();
        Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                  !renderer.IsReady() && renderer.LastError().contains(QStringLiteral("복구 한도")),
              "EngineSceneRenderer enforces recovery limit after 3 recoveries");

        renderer.Reset();
        Check(renderer.GetRecoveryCount() == 0 &&
                  renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                  renderer.IsReady(),
              "Reset() clears recovery counter and restores rendering");
#endif
        totalValidationErrors += renderer.GetGpuDevice()->GetCounters().validationErrors;
        Check(renderer.GetGpuDevice()->GetCounters().validationErrors == 0,
              "viewport renderer validation clean after resize, mid-render shutdown, and recovery");
        renderer.Shutdown();
    }

    // --- PART 2.4: QPainter Viewport PaintEvent & Rapid Tab Switching / Resize Stress Test ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.4: QPainter Viewport PaintEvent, Tab Switching & "
                 "Resize Stress Test...\n";
    {
        EngineSceneRenderer::SetForceAllowed(true);
        auto sharedRenderer = std::make_shared<EngineSceneRenderer>();
        EditorDocument doc;
        auto e = doc.GetScene().CreateEntity("TestQuad");
        e.GetComponent<TransformComponent>().scale = Vec3{2.0f, 2.0f, 1.0f};
        e.AddComponent<SpriteRendererComponent>(Vec4{0.2f, 0.8f, 0.4f, 1.0f});

        SceneViewport2D vp2D(
            nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [](auto, auto, auto, auto) {},
            [] {}, [] {}, sharedRenderer);
        SceneViewport3D vp3D(
            nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [] {}, [] {}, sharedRenderer);

        const int switchIterations =
            std::min(1000, cfg.viewportIterations > 0 ? cfg.viewportIterations : 200);
        std::cerr << "  Executing " << switchIterations
                  << " rapid 2D/3D viewport switch, resize, and QPainter render cycles...\n";

        const std::pair<int, int> testSizes[] = {{320, 240}, {400, 300}, {512, 384}, {640, 480},
                                                 {800, 600}, {256, 256}, {300, 200}};

        for (int i = 0; i < switchIterations; ++i) {
            const auto& sz = testSizes[i % std::size(testSizes)];
            if (i % 2 == 0) {
                vp2D.resize(sz.first, sz.second);
                QImage buffer(sz.first, sz.second, QImage::Format_ARGB32_Premultiplied);
                buffer.fill(Qt::black);
                vp2D.render(&buffer);
                Check(!buffer.isNull(), "SceneViewport2D QPainter render succeeded");
            } else {
                vp3D.resize(sz.first, sz.second);
                QImage buffer(sz.first, sz.second, QImage::Format_ARGB32_Premultiplied);
                buffer.fill(Qt::black);
                vp3D.render(&buffer);
                Check(!buffer.isNull(), "SceneViewport3D QPainter render succeeded");
            }
        }

        if (sharedRenderer->IsReady() && sharedRenderer->GetGpuDevice()) {
            Check(sharedRenderer->GetGpuDevice()->WaitIdle(),
                  "sharedRenderer WaitIdle after paint stress");
            Check(sharedRenderer->GetGpuDevice()->GetCounters().validationErrors == 0,
                  "zero validation errors after QPainter viewport paint stress");
            totalValidationErrors += sharedRenderer->GetGpuDevice()->GetCounters().validationErrors;
        }
        sharedRenderer->Shutdown();
        sharedRenderer.reset();

        // Also verify Software Viewport mode renders without errors
        {
            EngineSceneRenderer::SetForceAllowed(false);
            qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
            SceneViewport2D sw2D(
                nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [](auto, auto, auto, auto) {},
                [] {}, [] {});
            SceneViewport3D sw3D(nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [] {}, [] {});
            sw2D.resize(320, 240);
            sw3D.resize(320, 240);
            QImage img2D(320, 240, QImage::Format_ARGB32_Premultiplied);
            QImage img3D(320, 240, QImage::Format_ARGB32_Premultiplied);
            sw2D.render(&img2D);
            sw3D.render(&img3D);
            Check(!img2D.isNull() && !img3D.isNull(),
                  "Software Viewport 2D and 3D render succeeded");
            qunsetenv("LUX_EDITOR_SOFTWARE_VIEWPORT");
        }
    }

    EngineSceneRenderer::SetForceAllowed(false);
    app.processEvents();
    const auto finalSuiteCpu = QueryDetailedCpuMemory(true);

    const double totalSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - overallStart).count();
    std::cerr << "============================================================\n"
              << "[Editor_gpu_lifecycle] SUMMARY REPORT\n"
              << "  Status                 : PASS\n"
              << "  GPU Model              : " << deviceInfo.deviceName << " ("
              << deviceInfo.deviceType << ")\n"
              << "  Vulkan API Version     : " << FormatVulkanVersion(deviceInfo.apiVersion) << "\n"
              << "  Vulkan Driver Version  : " << FormatVulkanVersion(deviceInfo.driverVersion)
              << "\n"
              << "  Validation / SyncVal   : " << (deviceInfo.validation ? "ON" : "OFF") << " / "
              << (deviceInfo.synchronizationValidation ? "ON" : "OFF") << "\n"
              << "  GPU Lifecycle Loop     : " << completedIterations << "/" << cfg.gpuIterations
              << " PASS (" << loopSec << " s)\n"
              << "  Viewport Teardown Loop : " << cfg.viewportIterations << "/"
              << cfg.viewportIterations << " PASS (" << vpSec << " s)\n"
              << "  Total Execution Time   : " << totalSec << " s\n"
              << "  Validation Errors      : " << totalValidationErrors << "\n"
              << "  Active GPU Memory Delta (Iter 1 -> Iter " << cfg.gpuIterations
              << "): allocCount="
              << (static_cast<long long>(cpLast.gpuMem.allocationCount) -
                  static_cast<long long>(cpFirst.gpuMem.allocationCount))
              << ", allocBytes="
              << (static_cast<long long>(cpLast.gpuMem.allocationBytes) -
                  static_cast<long long>(cpFirst.gpuMem.allocationBytes))
              << " bytes\n"
              << "  CPU Post-Destroy PrivateUsage (Iter 1 -> Iter " << cfg.gpuIterations
              << "): " << (cpFirst.fourStage.postDestroy.privateBytes / 1024) << "KB -> "
              << (cpLast.fourStage.postDestroy.privateBytes / 1024) << "KB (delta="
              << (static_cast<long long>(cpLast.fourStage.postDestroy.privateBytes) -
                  static_cast<long long>(cpFirst.fourStage.postDestroy.privateBytes)) /
                     1024
              << "KB)\n"
              << "  CPU DefaultHeap Busy (Iter 1 -> Iter " << cfg.gpuIterations
              << "): " << (cpFirst.fourStage.detailedPostDestroy.defaultHeapBusyBytes / 1024)
              << "KB (" << cpFirst.fourStage.detailedPostDestroy.defaultHeapBusyBlocks
              << " blks) -> " << (cpLast.fourStage.detailedPostDestroy.defaultHeapBusyBytes / 1024)
              << "KB (" << cpLast.fourStage.detailedPostDestroy.defaultHeapBusyBlocks << " blks)\n"
              << "  Final Suite Teardown CPU: PrivateUsage=" << (finalSuiteCpu.privateBytes / 1024)
              << "KB, WorkingSet=" << (finalSuiteCpu.workingSetBytes / 1024)
              << "KB, DefaultHeapBusy=" << (finalSuiteCpu.defaultHeapBusyBytes / 1024) << "KB ("
              << finalSuiteCpu.defaultHeapBusyBlocks << " blks)\n"
              << "============================================================\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEngineTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LuxEditorUiTests"));
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    // A modal dialog would block this headless test forever. Dismiss it (No/OK) and say so.
    int dialogs = 0;
    QTimer dismiss;
    dismiss.setInterval(100);
    QObject::connect(&dismiss, &QTimer::timeout, [&dialogs] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            ++dialogs;
            std::cerr << "dialog dismissed: " << box->text().toStdString() << '\n';
            if (auto* no = box->button(QMessageBox::No))
                no->click();
            else
                box->reject();
        }
    });
    dismiss.start();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());

    const StressConfig cfg = ParseStressConfig(argc, argv);

    try {
        if (cfg.mode == "ui") {
            return RunHeadlessEditorUiTests(app, settings, cfg);
        }
        if (cfg.mode == "gpu") {
            return RunGpuLifecycleTests(app, cfg);
        }
        const int uiResult = RunHeadlessEditorUiTests(app, settings, cfg);
        if (uiResult != 0)
            return uiResult;
        const int gpuResult = RunGpuLifecycleTests(app, cfg);
        return gpuResult == kSkipReturnCode ? 0 : gpuResult;
    } catch (const std::exception& error) {
        std::cerr << "[FATAL] " << error.what() << '\n';
        return 1;
    }
}
