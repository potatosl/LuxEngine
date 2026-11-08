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
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
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

void RunMultiViewportGpuLifetimeTests(QApplication& app) {
    using namespace LuxEngine;
    std::cerr << "[ui-test] running multi-viewport GPU lifetime tests\n";
    EngineSceneRenderer::SetForceAllowed(true);

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

    // 1. 2D viewport widget + renderer context creation & destruction 100 times
    {
        EditorDocument doc;
        auto shared2D = std::make_shared<EngineSceneRenderer>();
        Check(shared2D->EnsureReady(), "2D GPU renderer initializes");
        for (int i = 0; i < 100; ++i) {
            Renderer2D::Context localCtx(shared2D->GetGpuDevice());
            Check(localCtx.IsDeviceValid(), "2D context valid during 100x lifecycle");
            SceneViewport2D vp(nullptr, doc, [](auto) {}, [](auto, const auto&) {},
                               [](auto, auto, auto, auto) {}, [] {}, [] {}, shared2D);
            vp.resize(320, 200);
            if (i % 25 == 0) {
                QImage out;
                Check(shared2D->Render(scene2D, 160, 120, QRectF(-5.0, 5.0, 10.0, -10.0), out) &&
                          !out.isNull(),
                      "2D viewport renders during 100x lifecycle");
            }
        }
        Check(shared2D->GetGpuDevice()->GetCounters().validationErrors == 0 &&
                  shared2D->GetGpuDevice()->GetMemoryStats().liveBuffers == 0,
              "zero validation errors or buffer leaks after 100x 2D viewport cycles");
    }

    // 2. 3D viewport widget + renderer context creation & destruction 100 times
    {
        EditorDocument doc;
        auto shared3D = std::make_shared<EngineSceneRenderer>();
        Check(shared3D->EnsureReady(), "3D GPU renderer initializes");
        for (int i = 0; i < 100; ++i) {
            Renderer3D::Context localCtx(shared3D->GetGpuDevice());
            Check(localCtx.IsDeviceValid(), "3D context valid during 100x lifecycle");
            SceneViewport3D vp(nullptr, doc, [](auto) {}, [](auto, const auto&) {}, [] {}, [] {},
                               shared3D);
            vp.resize(320, 200);
            if (i % 25 == 0) {
                QImage out;
                Check(shared3D->Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.4, 0.3, 5.0,
                                         out) &&
                          !out.isNull(),
                      "3D viewport renders during 100x lifecycle");
            }
        }
        Check(shared3D->GetGpuDevice()->GetCounters().validationErrors == 0 &&
                  shared3D->GetGpuDevice()->GetMemoryStats().liveBuffers == 0,
              "zero validation errors or buffer leaks after 100x 3D viewport cycles");
    }

    // 3. Simultaneous 2D and 3D viewports active & closing in different orders (A then B, B then A)
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
            // Destroy A first, verify B continues rendering both 3D and 2D without contamination
            rendererA.reset();
            Check(rendererB->IsReady(), "viewport B remains ready after viewport A is closed");
            Check(rendererB->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.5, 0.2, 4.0, imgB),
                  "viewport B 3D render succeeds after viewport A destruction");
            Check(rendererB->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgB),
                  "viewport B 2D render succeeds after viewport A destruction");
            Check(rendererB->GetGpuDevice()->GetCounters().validationErrors == 0,
                  "viewport B validation clean after A closed first");
        } else {
            // Destroy B first, verify A continues rendering both 2D and 3D without contamination
            rendererB.reset();
            Check(rendererA->IsReady(), "viewport A remains ready after viewport B is closed");
            Check(rendererA->Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), imgA),
                  "viewport A 2D render succeeds after viewport B destruction");
            Check(rendererA->Render3D(scene3D, 200, 150, {0.0f, 0.0f, 0.0f}, 0.3, 0.1, 4.0, imgA),
                  "viewport A 3D render succeeds after viewport B destruction");
            Check(rendererA->GetGpuDevice()->GetCounters().validationErrors == 0,
                  "viewport A validation clean after B closed first");
        }
    }

    // 4. Resize during rendering loop + Shutdown/Reset API lifecycle + Fault Injection
    {
        EngineSceneRenderer renderer;
        QImage output;
        const std::pair<int, int> sizes[] = {{160, 120}, {320, 240}, {256, 256}, {400, 220}};
        for (const auto& [w, h] : sizes) {
            Check(renderer.Render(scene2D, w, h, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                      output.width() == w && output.height() == h,
                  "2D viewport resize during rendering matches target dimensions");
            Check(renderer.Render3D(scene3D, w, h, {0.0f, 0.0f, 0.0f}, 0.2, 0.2, 5.0, output) &&
                      output.width() == w && output.height() == h,
                  "3D viewport resize during rendering matches target dimensions");
        }

#if defined(LUX_GPU_FAULT_INJECTION) && LUX_GPU_FAULT_INJECTION
        // Device Lost fault injection & automatic recovery on next frame
        Vk::ClearFaults();
        Vk::InjectFault(Vk::Fault::DeviceLost);
        Check(!renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output),
              "DeviceLost fault fails the active frame cleanly");
        Vk::ClearFaults();
        Check(renderer.Render(scene2D, 200, 150, QRectF(-5.0, 5.0, 10.0, -10.0), output) &&
                  renderer.IsReady() && renderer.GetRecoveryCount() == 1,
              "EngineSceneRenderer automatically recovers after DeviceLost");

        // GPU initialization failure fault injection & Reset recovery
        renderer.Shutdown();
        Check(!renderer.IsReady() && renderer.GetGpuDevice() == nullptr,
              "Shutdown releases GPU device");
        renderer.Reset();
        Vk::InjectFault(Vk::Fault::InstanceCreation);
        Check(!renderer.EnsureReady() && !renderer.LastError().isEmpty(),
              "GPU initialization failure reported cleanly");
        Vk::ClearFaults();
        renderer.Reset();
        Check(renderer.EnsureReady() &&
                  renderer.Render3D(scene3D, 160, 120, {0.0f, 0.0f, 0.0f}, 0.0, 0.0, 5.0, output),
              "EngineSceneRenderer recovers after Reset following init failure");
#endif
        Check(renderer.GetGpuDevice()->GetCounters().validationErrors == 0,
              "viewport renderer validation clean after resize and recovery tests");
        renderer.Shutdown();
    }

    EngineSceneRenderer::SetForceAllowed(false);
    app.processEvents();
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
    try {
        std::cerr << "[ui-test] creating window\n";
        LuxEngine::EditorWindow window;
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
        QMouseEvent press(QEvent::MouseButtonPress, tile, globalTile, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, tile, globalTile, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QApplication::sendEvent(canvas, &release);
        Check(undo->count() == beforePaint + 1, "tile painting enters undo history");
        for (const QString& id :
             {QStringLiteral("editorBody2D"), QStringLiteral("editorCollider2D"),
              QStringLiteral("editorAnimation")}) {
            auto* checkbox = window.findChild<QCheckBox*>(id);
            Check(checkbox != nullptr, "component editor exists");
            checkbox->setChecked(true);
            Check(checkbox->isChecked(), "component can be added in inspector");
        }
        auto* copyAction = window.findChild<QAction*>(QStringLiteral("copyEntity"));
        auto* pasteAction = window.findChild<QAction*>(QStringLiteral("pasteEntity"));
        Check(copyAction && pasteAction, "copy and paste actions exist");

        // --- PART C: Clipboard Validation & Undo/Redo Regression ---
        copyAction->trigger();
        pasteAction->trigger();
        Check(hierarchy->count() == 3, "copy and paste entity duplicates all components");
        for (const QString& id :
             {QStringLiteral("editorBody2D"), QStringLiteral("editorCollider2D"),
              QStringLiteral("editorAnimation")}) {
            auto* checkbox = window.findChild<QCheckBox*>(id);
            Check(checkbox && checkbox->isChecked(),
                  "pasted entity retains physics, collider and animation components");
        }
        // Multiple Undo / Redo cycles on pasted entity
        for (int cycle = 0; cycle < 3; ++cycle) {
            undo->undo();
            Check(hierarchy->count() == 2, "undo removes pasted entity");
            undo->redo();
            Check(hierarchy->count() == 3, "redo restores pasted entity");
        }
        undo->undo();
        Check(hierarchy->count() == 2, "undo before comprehensive clipboard tests");

        // Paste full all-component valid JSON, verify Undo/Redo, and verify all invalid JSONs are rejected
        SetClipboardJsonObject(MakeValidClipboardEntityJson());
        pasteAction->trigger();
        Check(hierarchy->count() == 3, "valid full-component clipboard JSON pasted");
        for (const QString& id :
             {QStringLiteral("editorSprite"), QStringLiteral("editorMesh"),
              QStringLiteral("editorTilemap"), QStringLiteral("editorBody2D"),
              QStringLiteral("editorCollider2D"), QStringLiteral("editorAnimation")}) {
            auto* checkbox = window.findChild<QCheckBox*>(id);
            Check(checkbox && checkbox->isChecked(),
                  "inspector checkboxes reflect pasted entity components");
        }
        // Copy the pasted entity back to clipboard and verify all 9 components roundtrip intact
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
                  "all 9 optional components preserved on pasted entity");
        }

        const int stableEntityCount = hierarchy->count();
        const int stableUndoCount = undo->count();
        const int stableUndoIndex = undo->index();

        auto assertRejected = [&](const char* label) {
            pasteAction->trigger();
            Check(hierarchy->count() == stableEntityCount && undo->count() == stableUndoCount &&
                      undo->index() == stableUndoIndex,
                  label);
        };

        // 1. NaN / Infinity / float overflow (1e308 overflows float to +Inf)
        SetClipboardJsonBytes(
            R"({"name":"InfTest","position":[1e308,0,0],"rotation":[0,0,0],"scale":[1,1,1]})");
        assertRejected("float overflow to Infinity rejected");

        // 2. Float coordinate bounds exceeded (> 100000)
        {
            auto bad = MakeValidClipboardEntityJson();
            bad.insert(QStringLiteral("position"), QJsonArray{250000.0, 0.0, 0.0});
            SetClipboardJsonObject(bad);
            assertRejected("out-of-bounds transform position rejected");
        }

        // 3. Negative or zero mass
        {
            auto bad = MakeValidClipboardEntityJson();
            QJsonObject rb = bad.value(QStringLiteral("rigidbody2d")).toObject();
            rb.insert(QStringLiteral("mass"), -2.0);
            bad.insert(QStringLiteral("rigidbody2d"), rb);
            SetClipboardJsonObject(bad);
            assertRejected("negative rigidbody2d mass rejected");
        }

        // 4. Negative or zero collider size
        {
            auto bad = MakeValidClipboardEntityJson();
            QJsonObject col = bad.value(QStringLiteral("collider2d")).toObject();
            col.insert(QStringLiteral("size"), QJsonArray{-1.0, 2.0});
            bad.insert(QStringLiteral("collider2d"), col);
            SetClipboardJsonObject(bad);
            assertRejected("negative collider2d size rejected");

            auto bad3d = MakeValidClipboardEntityJson();
            QJsonObject col3 = bad3d.value(QStringLiteral("collider3d")).toObject();
            col3.insert(QStringLiteral("size"), QJsonArray{1.0, 1.0, 0.0});
            bad3d.insert(QStringLiteral("collider3d"), col3);
            SetClipboardJsonObject(bad3d);
            assertRejected("zero collider3d size rejected");
        }

        // 5. Invalid Transform (zero scale or wrong array length)
        {
            auto bad = MakeValidClipboardEntityJson();
            bad.insert(QStringLiteral("scale"), QJsonArray{0.0, 1.0, 1.0});
            SetClipboardJsonObject(bad);
            assertRejected("zero transform scale rejected");

            bad.insert(QStringLiteral("scale"), QJsonArray{1.0, 1.0});
            SetClipboardJsonObject(bad);
            assertRejected("wrong transform array length rejected");
        }

        // 6. Abnormal Camera FOV
        {
            auto bad = MakeValidClipboardEntityJson();
            QJsonObject cam = bad.value(QStringLiteral("camera")).toObject();
            cam.insert(QStringLiteral("fov"), 185.0);
            bad.insert(QStringLiteral("camera"), cam);
            SetClipboardJsonObject(bad);
            assertRejected("camera FOV >= 180 rejected");

            cam.insert(QStringLiteral("fov"), 0.0);
            bad.insert(QStringLiteral("camera"), cam);
            SetClipboardJsonObject(bad);
            assertRejected("camera FOV <= 0 rejected");
        }

        // 7. Tilemap exceeding 256x256
        {
            auto bad = MakeValidClipboardEntityJson();
            QJsonObject map = bad.value(QStringLiteral("tilemap")).toObject();
            map.insert(QStringLiteral("width"), 300);
            map.insert(QStringLiteral("height"), 300);
            bad.insert(QStringLiteral("tilemap"), map);
            SetClipboardJsonObject(bad);
            assertRejected("tilemap > 256x256 rejected");
        }

        // 8. Keyframes exceeding 4,096
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
            assertRejected("> 4096 keyframes rejected");
        }

        // 9. Invalid Animation Duration (<= 0)
        {
            auto bad = MakeValidClipboardEntityJson();
            QJsonObject anim = bad.value(QStringLiteral("animation")).toObject();
            anim.insert(QStringLiteral("duration"), -1.0);
            bad.insert(QStringLiteral("animation"), anim);
            SetClipboardJsonObject(bad);
            assertRejected("negative animation duration rejected");
        }

        // 10. Corrupted JSON
        SetClipboardJsonBytes("{corrupted json: [1, 2, ");
        assertRejected("corrupted JSON syntax rejected");

        // 11. Invalid UTF-8 bytes
        SetClipboardJsonBytes(
            QByteArray("{\"name\":\"\xFF\xFE\xC0\xAF\",\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1]}"));
        assertRejected("invalid UTF-8 sequence rejected");

        // 12. Huge JSON (> 1 MB)
        {
            QByteArray huge(1024 * 1024 + 512, ' ');
            huge[0] = '{';
            huge[huge.size() - 1] = '}';
            SetClipboardJsonBytes(huge);
            assertRejected("oversized >1MB clipboard JSON rejected");
        }

        // 13. Missing required fields ("name", "position", "rotation", "scale")
        {
            auto bad = MakeValidClipboardEntityJson();
            bad.remove(QStringLiteral("position"));
            SetClipboardJsonObject(bad);
            assertRejected("missing position field rejected");

            auto badName = MakeValidClipboardEntityJson();
            badName.remove(QStringLiteral("name"));
            SetClipboardJsonObject(badName);
            assertRejected("missing name field rejected");
        }

        // Undo the valid full-component paste to return to 2 entities
        undo->undo();
        Check(hierarchy->count() == 2, "undo after rejected clipboard pastes works cleanly");

        // Verify Scene Save & Reopen after paste via EditorDocument
        {
            LuxEngine::EditorDocument doc;
            auto ent = doc.CreateEntity("저장검증엔티티");
            ent.GetComponent<LuxEngine::TransformComponent>().position = {7.0f, 8.0f, 9.0f};
            ent.AddComponent<LuxEngine::Rigidbody2DComponent>().mass = 3.5f;
            ent.AddComponent<LuxEngine::BoxCollider2DComponent>().size = {2.0f, 4.0f};
            const QString savedScenePath = settings.filePath(QStringLiteral("pasted_roundtrip.lux"));
            Check(doc.SaveAs(savedScenePath.toUtf8().toStdString()),
                  "scene saves after entity edits");
            LuxEngine::EditorDocument reopened;
            Check(reopened.Open(savedScenePath.toUtf8().toStdString()) &&
                      reopened.GetScene().GetEntityCount() == 1,
                  "saved scene reopens with exact entity count");
            auto loadedEnt = reopened.GetScene().GetEntities().front();
            Check(loadedEnt.HasComponent<LuxEngine::Rigidbody2DComponent>() &&
                      loadedEnt.HasComponent<LuxEngine::BoxCollider2DComponent>() &&
                       std::abs(loadedEnt.GetComponent<LuxEngine::Rigidbody2DComponent>().mass -
                               3.5f) < 1e-4f,
                  "reopened scene preserves all components");
        }

        auto* startPreview = window.findChild<QAction*>(QStringLiteral("previewStart"));
        auto* stepPreview = window.findChild<QAction*>(QStringLiteral("previewStep"));
        auto* stopPreview = window.findChild<QAction*>(QStringLiteral("previewStop"));
        Check(startPreview && stepPreview && stopPreview, "preview controls exist");
        std::cerr << "[ui-test] preview\n";
        startPreview->trigger();
        stepPreview->trigger();
        auto* debugState = window.findChild<QTreeWidget*>(QStringLiteral("debugState"));
        Check(debugState && debugState->topLevelItemCount() > 0, "debugger shows runtime state");
        stopPreview->trigger();
        Check(hierarchy->count() == 2, "preview restores editable scene");
        tabs->setCurrentIndex(0);
        std::cerr << "[ui-test] render\n";
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
        std::cerr << "[ui-test] closing\n";
        window.close();

        // --- PART B: Multi-Viewport Vulkan GPU Lifetime Tests ---
        RunMultiViewportGpuLifetimeTests(app);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
