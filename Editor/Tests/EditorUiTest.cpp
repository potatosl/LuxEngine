#include "App/EditorWindow.hpp"
#include "Engine/ECS/ECS.hpp"
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
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kSkipReturnCode = 77;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
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

std::string FormatVulkanVersion( LuxEngine::uint32 version) {
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
    Check(Vec3Near(tr.position, {1.5f, -2.5f, 3.0f}) &&
              Vec3Near(tr.rotation, {0.1f, 0.2f, 0.3f}) && Vec3Near(tr.scale, {2.0f, 3.0f, 4.0f}),
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

int RunHeadlessEditorUiTests(QApplication& app, QTemporaryDir& settings) {
    using namespace LuxEngine;
    // Strictly disable GPU viewport in Editor_ui so it runs 100% headlessly without GPU dependency.
    EngineSceneRenderer::SetForceAllowed(false);

    std::cerr << "[Editor_ui] creating EditorWindow (headless offscreen)\n";
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

    // --- PART 3.1: 100x Undo/Redo Regression ---
    std::cerr << "[Editor_ui] PART 3.1: Entity create -> add components -> copy -> paste -> 100x "
                 "Undo/Redo\n";
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

    // Paste full 11-component entity and run 100x Undo/Redo loop
    SetClipboardJsonObject(MakeValidClipboardEntityJson());
    pasteAction->trigger();
    Check(hierarchy->count() == 3 && window.GetDocument().GetScene().GetEntityCount() == 3,
          "valid full-component clipboard JSON pasted");
    for (const QString& id : {QStringLiteral("editorSprite"), QStringLiteral("editorMesh"),
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
          "baseline scene serializes before 100x Undo/Redo");

    for (int iter = 0; iter < 100; ++iter) {
        undo->undo();
        Check(hierarchy->count() == 2 && window.GetDocument().GetScene().GetEntityCount() == 2 &&
                  !FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본").IsValid(),
              "100x Undo removes pasted entity cleanly");
        undo->redo();
        Check(hierarchy->count() == 3 && window.GetDocument().GetScene().GetEntityCount() == 3,
              "100x Redo restores pasted entity cleanly");
    }

    Entity after100Pasted = FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본");
    VerifyFullClipboardEntityComponents(after100Pasted);
    std::string after100Serialized;
    Check(window.GetDocument().GetScene().SerializeToString(after100Serialized) &&
              after100Serialized == baselineSerialized,
          "scene state and all component values are 100% identical after 100x Undo/Redo");
    std::cerr << "[Editor_ui] PART 3.1 PASS: 100x Undo/Redo verified (entity count=3, all 11 "
                 "components identical)\n";

    // Copy the pasted entity back to clipboard and verify all 9 optional components roundtrip intact
    copyAction->trigger();
    {
        const auto copiedDoc = QJsonDocument::fromJson(
            QApplication::clipboard()->mimeData()->data(QStringLiteral("application/x-lux-entity")));
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
    SetClipboardJsonBytes(QByteArray(
        "{\"name\":\"\xFF\xFE\xC0\xAF\",\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1]}"));
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
    std::cerr << "[Editor_ui] PART 3.4: Verifying RecordEdit exception rollback and error logging\n";
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
        // Perform Undo and Redo on the actual pasted entity in EditorWindow, then save the scene
        undo->undo();
        Check(hierarchy->count() == 2, "undo before save roundtrip");
        undo->redo();
        Check(hierarchy->count() == 3, "redo restores pasted entity before save roundtrip");

        std::string preSaveSerialized;
        Check(window.GetDocument().GetScene().SerializeToString(preSaveSerialized) &&
                  preSaveSerialized == baselineSerialized,
              "pre-save serialized scene matches baseline");

        const std::string savedScenePath =
            settings.filePath(QStringLiteral("actual_pasted_editor_scene.lux")).toUtf8().toStdString();
        Check(window.GetDocument().SaveAs(savedScenePath),
              "actual EditorWindow document with pasted entity saves to .lux file");

        // Reopen into an independent EditorDocument and verify every entity and component
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

        // Also reopen directly in EditorWindow's document and verify UI & components
        Check(window.GetDocument().Open(savedScenePath),
              "EditorWindow document reopens saved .lux file");
        Entity windowReopenedPasted =
            FindEntityByTag(window.GetDocument().GetScene(), "검증엔티티 복사본");
        VerifyFullClipboardEntityComponents(windowReopenedPasted);
    }
    std::cerr << "[Editor_ui] PART 3.3 PASS: Actual pasted entity save and reopen verified\n";

    // Return scene to 2 entities (undo pasted entity) for preview & offscreen screenshot checks
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
        QPixmap small(window.size());
        window.render(&small);
        small.save(smallScreenshot);
    }
    while (undo->canUndo())
        undo->undo();
    std::cerr << "[Editor_ui] closing window\n";
    window.close();
    return 0;
}

int RunGpuLifecycleTests(QApplication& app) {
    using namespace LuxEngine;
    const auto overallStart = std::chrono::steady_clock::now();
    std::cerr << "[Editor_gpu_lifecycle] probing Vulkan GPU availability\n";

    EngineSceneRenderer::SetForceAllowed(true);
    // Request Vulkan Synchronization Validation (falls back to standard Validation or Off if layer missing)
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

    // --- PART 2.1: 100x Full EngineSceneRenderer Create / Render 2D & 3D / Readback / Shutdown ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.1: Starting 100x full EngineSceneRenderer "
                 "create-render-shutdown stress loop...\n";
    const auto loop100Start = std::chrono::steady_clock::now();
    GpuMemoryStats firstIterStats{};
    GpuMemoryStats lastIterStats{};
    int completedIterations = 0;

    for (int i = 0; i < 100; ++i) {
        // 1. Create EngineSceneRenderer
        auto renderer = std::make_unique<EngineSceneRenderer>();
        // 2 & 3. Initialize GpuDevice + Renderer2D & Renderer3D Contexts
        Check(renderer->EnsureReady() && renderer->IsReady() && renderer->GetGpuDevice() != nullptr,
              "EngineSceneRenderer initializes GpuDevice and contexts in 100x loop");

        // 4 & 6. Perform real 2D rendering + QImage readback check
        QImage img2D;
        Check(renderer->Render(scene2D, 160, 120, QRectF(-5.0, 5.0, 10.0, -10.0), img2D),
              "100x loop 2D render succeeds");
        Check(!img2D.isNull() && img2D.width() == 160 && img2D.height() == 120 &&
                  HasNonBackgroundPixel(img2D, 30, 34, 42),
              "100x loop 2D QImage readback has valid dimensions and rendered quad pixels");

        // 5 & 6. Perform real 3D rendering + QImage readback check
        QImage img3D;
        Check(renderer->Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, img3D),
              "100x loop 3D render succeeds");
        Check(!img3D.isNull() && img3D.width() == 160 && img3D.height() == 120 &&
                  HasNonBackgroundPixel(img3D, 27, 33, 42),
              "100x loop 3D QImage readback has valid dimensions and rendered mesh pixels");

        // 7 & 8. Check Vulkan validation errors and GPU memory/resource statistics
        Check(renderer->GetGpuDevice()->WaitIdle(), "GpuDevice WaitIdle succeeds");
        const auto counters = renderer->GetGpuDevice()->GetCounters();
        const auto memStats = renderer->GetGpuDevice()->GetMemoryStats();
        totalValidationErrors += counters.validationErrors;
        Check(counters.validationErrors == 0,
              "zero Vulkan validation errors during 100x EngineSceneRenderer iteration");
        // EngineSceneRenderer holds 1 default white texture in GpuDevice and 0 live user buffers
        // (dynamic geometry uses the per-frame ring buffer); no pending destructions or staging leaks.
        Check(memStats.liveBuffers == 0 && memStats.liveTextures == 1 &&
                  memStats.pendingDestructions == 0 && memStats.pendingStagingBytes == 0 &&
                  memStats.allocationCount > 0 && memStats.allocationBytes > 0,
              "active GPU resource counts match exact expected baseline before Shutdown");

        if (i == 0)
            firstIterStats = memStats;
        if (i == 99)
            lastIterStats = memStats;

        // Verify post-shutdown context safety when tearing down
        Renderer2D::Context observer2D(renderer->GetGpuDevice());
        Renderer3D::Context observer3D(renderer->GetGpuDevice());
        Check(observer2D.IsDeviceValid() && observer3D.IsDeviceValid(),
              "observer contexts valid while GpuDevice is alive");

        // 9. Shutdown Renderer and GpuDevice
        renderer->Shutdown();
        Check(!renderer->IsReady() && renderer->GetGpuDevice() == nullptr,
              "EngineSceneRenderer Shutdown releases GpuDevice");
        Check(!observer2D.IsDeviceValid() && !observer3D.IsDeviceValid(),
              "contexts bound to destroyed GpuDevice immediately detect invalid device");
        observer2D.Shutdown();
        observer3D.Shutdown();

        ++completedIterations;
        if ((i + 1) % 20 == 0 || i == 0) {
            std::cerr << "  [100x EngineSceneRenderer] iteration " << (i + 1)
                      << "/100: PASS | liveBuffers=" << memStats.liveBuffers
                      << " liveTextures=" << memStats.liveTextures
                      << " allocCount=" << memStats.allocationCount
                      << " allocBytes=" << memStats.allocationBytes
                      << " deviceLocalUsage=" << memStats.deviceLocalUsage
                      << " validationErrors=" << counters.validationErrors << "\n";
        }
    }
    const double loop100Sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - loop100Start).count();
    Check(completedIterations == 100 &&
              firstIterStats.allocationCount == lastIterStats.allocationCount &&
              firstIterStats.allocationBytes == lastIterStats.allocationBytes &&
              firstIterStats.liveBuffers == lastIterStats.liveBuffers &&
              firstIterStats.liveTextures == lastIterStats.liveTextures,
          "100x EngineSceneRenderer lifecycle completed with zero resource or allocation drift");

    // Also verify 100x SceneViewport2D & SceneViewport3D widget + Context creation cycles
    {
        EditorDocument doc;
        auto shared = std::make_shared<EngineSceneRenderer>();
        Check(shared->EnsureReady(), "shared GPU renderer initializes for 100x widget cycle");
        QImage warmup;
        Check(shared->Render(scene2D, 320, 200, QRectF(-5.0, 5.0, 10.0, -10.0), warmup) &&
                  shared->Render3D(scene3D, 320, 200, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0, warmup),
              "shared GPU renderer warmup renders");
        Check(shared->GetGpuDevice()->WaitIdle(), "shared GpuDevice warmup WaitIdle");
        const auto memBefore = shared->GetGpuDevice()->GetMemoryStats();
        for (int i = 0; i < 100; ++i) {
            Renderer2D::Context ctx2D(shared->GetGpuDevice());
            Renderer3D::Context ctx3D(shared->GetGpuDevice());
            Check(ctx2D.IsDeviceValid() && ctx3D.IsDeviceValid(),
                  "2D/3D contexts valid in 100x widget cycle");
            SceneViewport2D vp2D(nullptr, doc, [](auto) {}, [](auto, const auto&) {},
                                 [](auto, auto, auto, auto) {}, [] {}, [] {}, shared);
            SceneViewport3D vp3D(nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [] {}, [] {},
                                 shared);
            vp2D.resize(320, 200);
            vp3D.resize(320, 200);
        }
        Check(shared->GetGpuDevice()->WaitIdle(), "shared GpuDevice WaitIdle");
        const auto memAfter = shared->GetGpuDevice()->GetMemoryStats();
        const auto counters = shared->GetGpuDevice()->GetCounters();
        totalValidationErrors += counters.validationErrors;
        Check(counters.validationErrors == 0 &&
                  memAfter.allocationCount == memBefore.allocationCount &&
                  memAfter.allocationBytes == memBefore.allocationBytes &&
                  memAfter.liveBuffers == memBefore.liveBuffers &&
                  memAfter.liveTextures == memBefore.liveTextures &&
                  memAfter.pendingDestructions == 0,
              "zero GPU resource drift after 100x SceneViewport2D/3D + Context cycles");
        shared->Shutdown();
    }

    // --- PART 2.2: Multi-Viewport Simultaneous 2D/3D, Close Orders, Resize, Mid-Render Shutdown,
    // Fault Recovery ---
    std::cerr << "[Editor_gpu_lifecycle] PART 2.2: Multi-viewport & edge-case verification\n";
    for (int closeOrder = 0; closeOrder < 2; ++closeOrder) {
        auto rendererA = std::make_unique<EngineSceneRenderer>();
        auto rendererB = std::make_unique<EngineSceneRenderer>();
        QImage imgA, imgB;
        Check(rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgA),
              "viewport A initial 2D render");
        Check(rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0, imgB),
              "viewport B initial 3D render");
        Check(imgA.size() == QSize(200, 150) && imgB.size() == QSize(200, 150),
              "simultaneous 2D and 3D viewports produce expected image sizes");

        if (closeOrder == 0) {
            rendererA.reset();
            Check(rendererB->IsReady(), "viewport B remains ready after viewport A is closed");
            Check(rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0, imgB),
                  "viewport B 3D render succeeds after viewport A destruction");
            Check(rendererB->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgB),
                  "viewport B 2D render succeeds after viewport A destruction");
            totalValidationErrors += rendererB->GetGpuDevice()->GetCounters().validationErrors;
            Check(rendererB->GetGpuDevice()->GetCounters().validationErrors == 0,
                  "viewport B validation clean after A closed first");
        } else {
            rendererB.reset();
            Check(rendererA->IsReady(), "viewport A remains ready after viewport B is closed");
            Check(rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgA),
                  "viewport A 2D render succeeds after viewport B destruction");
            Check(rendererA->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.3, 0.1, 4.0, imgA),
                  "viewport A 3D render succeeds after viewport B destruction");
            totalValidationErrors += rendererA->GetGpuDevice()->GetCounters().validationErrors;
            Check(rendererA->GetGpuDevice()->GetCounters().validationErrors == 0,
                  "viewport A validation clean after B closed first");
        }
    }

    // Repeated Resize + Mid-Render Shutdown Request + Post-Shutdown Context Guard + Fault Recovery
    {
        EngineSceneRenderer renderer;
        QImage output;
        const std::pair<int, int> sizes[] = {
            {160, 120}, {320, 240}, {256, 256}, {400, 220}, {128, 96}, {360, 180}};
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
            // Calling Shutdown() mid-frame must defer teardown until Render() finishes
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
            // Attempting to use contexts after Shutdown must be a safe no-op without crash
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
        // Initialization failure fault injection + Reset recovery
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

        // DeviceLost recovery (up to kMaxDeviceRecoveries = 3) and recovery limit enforcement on 4th
        for (uint32 expectedRecovery = 1; expectedRecovery <= 3; ++expectedRecovery) {
            Vk::InjectFault(Vk::Fault::DeviceLost);
            Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output),
                  "DeviceLost fault fails the active frame cleanly");
            Vk::ClearFaults();
            Check(renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                      renderer.IsReady() && renderer.GetRecoveryCount() == expectedRecovery,
                  "EngineSceneRenderer automatically recovers after DeviceLost within limit");
        }
        // 4th DeviceLost must exceed recovery limit (kMaxDeviceRecoveries == 3)
        Vk::InjectFault(Vk::Fault::DeviceLost);
        Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output),
              "4th DeviceLost fails active frame");
        Vk::ClearFaults();
        Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                  !renderer.IsReady() &&
                  renderer.LastError().contains(QStringLiteral("복구 한도")),
              "EngineSceneRenderer enforces recovery limit after 3 recoveries");

        // Explicit Reset() clears the recovery counter and restores rendering
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

    EngineSceneRenderer::SetForceAllowed(false);
    app.processEvents();

    const double totalSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - overallStart).count();
    std::cerr << "============================================================\n"
              << "[Editor_gpu_lifecycle] SUMMARY REPORT\n"
              << "  Status                 : PASS\n"
              << "  GPU Model              : " << deviceInfo.deviceName << "\n"
              << "  Vulkan API Version     : " << FormatVulkanVersion(deviceInfo.apiVersion) << "\n"
              << "  Vulkan Driver Version  : " << FormatVulkanVersion(deviceInfo.driverVersion)
              << "\n"
              << "  100x Full Lifecycle    : " << completedIterations << "/100 PASS ("
              << loop100Sec << " s)\n"
              << "  Total Execution Time   : " << totalSec << " s\n"
              << "  Validation Errors      : " << totalValidationErrors << "\n"
              << "  Iter 1 Memory Stats    : allocCount=" << firstIterStats.allocationCount
              << ", allocBytes=" << firstIterStats.allocationBytes
              << ", liveBuffers=" << firstIterStats.liveBuffers
              << ", liveTextures=" << firstIterStats.liveTextures << "\n"
              << "  Iter 100 Memory Stats  : allocCount=" << lastIterStats.allocationCount
              << ", allocBytes=" << lastIterStats.allocationBytes
              << ", liveBuffers=" << lastIterStats.liveBuffers
              << ", liveTextures=" << lastIterStats.liveTextures << "\n"
              << "  Memory Delta (1->100)  : allocCount="
              << (static_cast<long long>(lastIterStats.allocationCount) -
                  static_cast<long long>(firstIterStats.allocationCount))
              << ", allocBytes="
              << (static_cast<long long>(lastIterStats.allocationBytes) -
                  static_cast<long long>(firstIterStats.allocationBytes))
              << " bytes\n"
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

    std::string mode = "all";
    if (argc > 1 && argv[1] != nullptr)
        mode = argv[1];

    try {
        if (mode == "ui") {
            return RunHeadlessEditorUiTests(app, settings);
        }
        if (mode == "gpu") {
            return RunGpuLifecycleTests(app);
        }
        const int uiResult = RunHeadlessEditorUiTests(app, settings);
        if (uiResult != 0)
            return uiResult;
        const int gpuResult = RunGpuLifecycleTests(app);
        return gpuResult == kSkipReturnCode ? 0 : gpuResult;
    } catch (const std::exception& error) {
        std::cerr << "[FATAL] " << error.what() << '\n';
        return 1;
    }
}
