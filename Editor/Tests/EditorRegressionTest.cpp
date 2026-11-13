#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUndoStack>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
using namespace LuxEngine;
using namespace LuxEditor;

struct GpuUnavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::unique_ptr<EditorWindow> OpenScene(QApplication& app, Scene& scene, QTemporaryDir& files) {
    const QString path = files.filePath(QStringLiteral("regression.lux"));
    Check(scene.Serialize(path.toUtf8().toStdString()), "regression scene saves");
    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastScene"), path);
    auto window = std::make_unique<EditorWindow>();
    window->show();
    app.processEvents();
    auto* hierarchy = window->findChild<QTreeWidget*>(QStringLiteral("sceneOutliner"));
    Check(hierarchy && hierarchy->topLevelItemCount() > 0, "saved scene opens in the editor");
    hierarchy->setCurrentItem(hierarchy->topLevelItem(0));
    return window;
}

void TestTimeline(QApplication& app) {
    for (const bool stationary : {false, true}) {
        QTemporaryDir files;
        Check(files.isValid(), "timeline temporary directory exists");
        Scene scene;
        auto entity = scene.CreateEntity("Animated");
        auto& animation = entity.AddComponent<AnimationComponent>();
        animation.clip.duration = 1;
        animation.clip.loop = false;
        animation.clip.AddKeyframe({0, {0, 0, 0}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({1, {stationary ? 0.0f : 10.0f, 0, 0}, {}, {1, 1, 1}});
        auto window = OpenScene(app, scene, files);
        auto* timeline = window->findChild<QSlider*>(QStringLiteral("animationTimeline"));
        auto* undo = window->findChild<QUndoStack*>();
        Check(timeline && undo, "timeline and history are available");
        timeline->setSliderDown(true);
        timeline->setValue(500);
        timeline->setSliderDown(false);
        Check(undo->canUndo(), "time-only scrubbing also enters history");
        undo->undo();
        Check(timeline->value() == 0, "timeline undo restores the original time");
        undo->redo();
        auto selected = window->GetDocument().GetSelected();
        Check(timeline->value() == 500 &&
                  std::abs(selected.GetComponent<AnimationComponent>().currentTime - .5f) < .0001f,
              "timeline redo restores the playhead and animation time");
        Check(std::abs(selected.GetComponent<TransformComponent>().position.x -
                       (stationary ? 0.0f : 5.0f)) < .0001f,
              "timeline redo restores the sampled transform");
        QPushButton* addKey = nullptr;
        for (auto* button : window->findChildren<QPushButton*>())
            if (button->text() == QStringLiteral("현재 변환 키 추가"))
                addKey = button;
        Check(addKey != nullptr, "keyframe action is available");
        addKey->click();
        const auto& keys =
            window->GetDocument().GetSelected().GetComponent<AnimationComponent>().clip.keyframes;
        Check(keys.size() == 3 && keys[0].time == 0 && keys[0].position.x == 0 &&
                  keys[1].time == .5f,
              "adding a key after redo preserves the original zero-time key");
        undo->undo();
        Check(timeline->value() == 500, "undoing key insertion preserves the editing time");
        undo->redo();
        Check(timeline->value() == 500, "redoing key insertion preserves the editing time");
        window->GetDocument().SetDirty(false);
        window->close();
    }
}

void TestNameLimits(QApplication& app) {
    QTemporaryDir files;
    Scene scene;
    scene.CreateEntity("Seed");
    auto window = OpenScene(app, scene, files);
    QLineEdit* name = nullptr;
    for (auto* field : window->findChildren<QLineEdit*>())
        if (field->text() == QStringLiteral("Seed"))
            name = field;
    Check(name != nullptr, "name editor exists");
    auto* undo = window->findChild<QUndoStack*>();
    const QString koreanBoundary = QString(85, QChar(0xac00)) + QLatin1Char('a');
    for (const auto& valid : {QString(256, QLatin1Char('a')), koreanBoundary}) {
        name->setText(valid);
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        Check(window->GetDocument().Save(), "boundary-length names save");
        EditorDocument reopened;
        Check(reopened.Open(window->GetDocument().GetPath()), "saved name reopens");
        undo->undo();
        undo->redo();
        Check(window->GetDocument().GetSelected().GetComponent<TagComponent>().tag ==
                  valid.toUtf8().toStdString(),
              "name redo round trips");
        window->findChild<QAction*>(QStringLiteral("copyEntity"))->trigger();
        window->findChild<QAction*>(QStringLiteral("pasteEntity"))->trigger();
        Check(window->GetDocument().GetScene().GetEntityCount() == 2,
              "boundary-length names can still be copied and pasted");
        undo->undo();
        const int index = undo->index();
        name->setText(valid + QLatin1Char('b'));
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        Check(undo->index() == index &&
                  window->GetDocument().GetSelected().GetComponent<TagComponent>().tag ==
                      valid.toUtf8().toStdString(),
              "oversized names roll back without history");
        Check(window->GetDocument().Save() && reopened.Open(window->GetDocument().GetPath()),
              "rejected name leaves a reopenable scene");
    }
    Scene invalid;
    auto entity = invalid.CreateEntity(std::string(257, 'a'));
    std::string output = "unchanged";
    Check(!invalid.SerializeToString(output) && output == "unchanged",
          "engine rejects oversized tags");
    entity.GetComponent<TagComponent>().tag = std::string("\xc0\xaf", 2);
    Check(!invalid.SerializeToString(output), "engine rejects invalid UTF-8 tags");
    entity.GetComponent<TagComponent>().tag = std::string("\xf5\x80\x80\x80", 4);
    Check(!invalid.SerializeToString(output), "engine rejects UTF-8 beyond Unicode range");
    entity.GetComponent<TagComponent>().tag = std::string("\xf4\x8f\xbf\xbf", 4);
    Scene boundary;
    Check(invalid.SerializeToString(output) && boundary.DeserializeFromString(output),
          "maximum Unicode code point round trips");
    entity.GetComponent<TagComponent>().tag = "A\r\nB\x1a C";
    const auto controlsFile = PathFromUtf8(files.path().toUtf8().toStdString()) / "controls.lux";
    Check(invalid.Serialize(PathUtf8(controlsFile)) &&
              boundary.Deserialize(PathUtf8(controlsFile)) &&
              boundary.GetEntities()[0].GetComponent<TagComponent>().tag ==
                  entity.GetComponent<TagComponent>().tag,
          "binary scene IO preserves valid UTF-8 control bytes on all platforms");
    entity.GetComponent<TagComponent>().tag = "valid";
    entity.AddComponent<AnimationComponent>().clip.name = std::string(257, 'x');
    Check(!invalid.SerializeToString(output), "engine validates clip-name read/write symmetry");
    window->GetDocument().SetDirty(false);
    window->close();
}

void TestSparseCopy(QApplication& app) {
    for (const auto [hasTag, hasTransform] :
         {std::pair{false, true}, std::pair{true, false}, std::pair{false, false}}) {
        QTemporaryDir files;
        Scene scene;
        auto source = scene.CreateEntity("Sparse");
        source.AddComponent<SpriteRendererComponent>().color = {.2f, .3f, .4f, 1};
        if (!hasTag)
            source.RemoveComponent<TagComponent>();
        if (!hasTransform)
            source.RemoveComponent<TransformComponent>();
        auto window = OpenScene(app, scene, files);
        auto* copy = window->findChild<QAction*>(QStringLiteral("copyEntity"));
        auto* paste = window->findChild<QAction*>(QStringLiteral("pasteEntity"));
        auto* undo = window->findChild<QUndoStack*>();
        Check(copy && paste && undo, "clipboard actions are available");
        copy->trigger();
        paste->trigger();
        auto checkPasted = [&] {
            auto entity = window->GetDocument().GetSelected();
            Check(window->GetDocument().GetScene().GetEntityCount() == 2 && entity,
                  "sparse entity pastes without an escaped exception");
            Check(entity.HasComponent<TagComponent>() == hasTag &&
                      entity.HasComponent<TransformComponent>() == hasTransform &&
                      entity.HasComponent<SpriteRendererComponent>() &&
                      entity.GetComponent<SpriteRendererComponent>().color.y == .3f,
                  "copy/paste preserves present and absent components");
        };
        checkPasted();
        undo->undo();
        Check(window->GetDocument().GetScene().GetEntityCount() == 1, "sparse paste can be undone");
        undo->redo();
        checkPasted();
        const auto copied = QJsonDocument::fromJson(QApplication::clipboard()->mimeData()->data(
                                                        QStringLiteral("application/x-lux-entity")))
                                .object();
        Check(copied.value(QStringLiteral("hasTag")).toBool() == hasTag &&
                  copied.value(QStringLiteral("hasTransform")).toBool() == hasTransform,
              "clipboard records component presence explicitly");
        for (int invalidCase = 0; invalidCase < 5; ++invalidCase) {
            auto invalid = copied;
            if (invalidCase == 0)
                invalid.insert(QStringLiteral("version"), 3);
            if (invalidCase == 1)
                invalid.insert(QStringLiteral("hasTag"), QStringLiteral("false"));
            if (invalidCase == 2)
                invalid.remove(QStringLiteral("version"));
            if (invalidCase == 3)
                invalid.insert(QStringLiteral("hasTag"), !hasTag);
            if (invalidCase == 4)
                invalid.insert(QStringLiteral("hasTransform"), !hasTransform);
            auto* mime = new QMimeData;
            mime->setData(QStringLiteral("application/x-lux-entity"),
                          QJsonDocument(invalid).toJson());
            QApplication::clipboard()->setMimeData(mime);
            const int index = undo->index();
            paste->trigger();
            Check(undo->index() == index && window->GetDocument().GetScene().GetEntityCount() == 2,
                  "invalid presence metadata is rejected without changing the document");
        }
        window->GetDocument().SetDirty(false);
        window->close();
    }
}

void Click(QWidget& view, QPointF point) {
    const QPointF global = view.mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &release);
}

void TestSparseTilemap(QApplication& app) {
    for (const auto tool : {EditorTool::TileBrush, EditorTool::TileErase}) {
        for (const bool selected : {false, true}) {
            EditorDocument document;
            auto sparse = document.CreateEntity("Sparse map");
            sparse.AddComponent<TilemapComponent>();
            sparse.RemoveComponent<TransformComponent>();
            if (!selected)
                document.Select(std::nullopt);
            int edits = 0, paints = 0;
            SceneViewport2D view(
                nullptr, document, [&](auto id) { document.Select(id); }, [](auto, const auto&) {},
                [&](auto, auto, auto, auto) { ++paints; }, [&] { ++edits; }, [&] { --edits; });
            view.resize(640, 480);
            view.SetTool(tool);
            view.show();
            app.processEvents();
            Click(view, {340, 220});
            Check(edits == 0 && paints == 0, "invalid maps cannot start a brush transaction");
            auto valid = document.CreateEntity("Valid map");
            valid.AddComponent<TilemapComponent>();
            document.Select(sparse.GetId());
            Click(view, {340, 220});
            Check(document.GetSelected() == valid && edits == 0 && paints == 1,
                  "brush skips sparse maps and selects a valid map under the cursor");
        }
    }
}

void TestPicking2D(QApplication& app, bool gpu = false) {
    std::shared_ptr<EngineSceneRenderer> renderer;
    if (gpu) {
        renderer = std::make_shared<EngineSceneRenderer>();
        if (!renderer->EnsureReady())
            throw GpuUnavailable(renderer->LastError().toStdString());
    }
    for (const auto [equalZ, withTilemap] : {std::pair{false, false}, std::pair{true, false},
                                             std::pair{false, true}, std::pair{true, true}}) {
        EditorDocument document;
        auto first = document.CreateEntity("First");
        auto& firstTransform = first.GetComponent<TransformComponent>();
        firstTransform.position.z = equalZ ? 0 : 10;
        firstTransform.scale = {4, 4, 1};
        first.AddComponent<SpriteRendererComponent>().color = {1, 0, 0, 1};
        auto second = document.CreateEntity("Second");
        second.GetComponent<TransformComponent>().scale = {4, 4, 1};
        second.AddComponent<SpriteRendererComponent>().color = {0, 0, 1, 1};
        // Tilemaps are drawn in the background pass, regardless of their creation order.
        if (withTilemap) {
            auto map = document.CreateEntity("Tilemap");
            map.GetComponent<TransformComponent>().position = {-1, -1, 20};
            map.AddComponent<TilemapComponent>().Set(0, 0, 1);
        }
        SceneViewport2D view(
            nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
            [](uint32, const TransformComponent&) {}, [](uint32, uint32, uint32, uint8_t) {}, [] {},
            [] {}, renderer);
        view.resize(640, 480);
        view.show();
        app.processEvents();
        QImage image(view.size(), QImage::Format_RGBA8888);
        view.render(&image);
        const QPoint point(340, 260);
        const QColor visible = image.pixelColor(point);
        Check(equalZ ? visible.blue() > 240 : visible.red() > 240,
              "overlap fixture displays the expected front sprite");
        Check(image.pixelColor(QPoint(320, 240)) == image.pixelColor(QPoint(325, 245)),
              "opaque sprite hides the origin grid and its neighboring lines");
        if (gpu) {
            QImage engineImage;
            Check(renderer->Render(document.GetScene(), 640, 480,
                                   QRectF(-640.0 / 144, 480.0 / 144, 640.0 / 72, -480.0 / 72),
                                   engineImage),
                  "Vulkan renders the overlapping scene without falling back to software");
            const QColor engineVisible = engineImage.pixelColor(point);
            Check(equalZ ? engineVisible.blue() > 240 : engineVisible.red() > 240,
                  "Vulkan and software agree on the visible front sprite");
        }
        Click(view, point);
        const auto selected = document.GetSelected();
        Check(selected && selected.GetId() == (equalZ ? second.GetId() : first.GetId()),
              "click selects the visible front sprite with stable equal-Z ordering");
    }
    if (gpu)
        Check(renderer->GetGpuDevice()->GetCounters().validationErrors == 0,
              "2D regression emits no Vulkan validation errors");
}

QPointF ReferenceProjection(Vec3 point, QSize size, double yaw, double pitch) {
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double depth = 22 - point.x * sy * cp + point.y * sp - point.z * cy * cp;
    const double focal = size.height() / (2 * std::tan(std::numbers::pi / 6));
    return {size.width() * .5 + (point.x * cy - point.z * sy) * focal / depth,
            size.height() * .5 -
                (point.x * sy * sp + point.y * cp + point.z * cy * sp) * focal / depth};
}

void TestPicking3D(QApplication& app, bool gpu = false) {
    std::shared_ptr<EngineSceneRenderer> renderer;
    if (gpu) {
        renderer = std::make_shared<EngineSceneRenderer>();
        if (!renderer->EnsureReady())
            throw GpuUnavailable(renderer->LastError().toStdString());
    }
    EditorDocument document;
    auto mesh = document.CreateEntity("Offset mesh");
    mesh.GetComponent<TransformComponent>().position = {6, 0, 6};
    mesh.GetComponent<TransformComponent>().scale = {3, 2, 2};
    mesh.AddComponent<MeshRendererComponent>().material.albedoColor = {1, 0, 1, 1};
    SceneViewport3D view(
        nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
        [](uint32, const TransformComponent&) {}, [] {}, [] {}, renderer);
    for (int variant = 0; variant < 2; ++variant) {
        const double yaw = variant == 0 ? .65 : .89;
        const double pitch = variant == 0 ? -.48 : -.60;
        if (variant == 1) {
            const QPointF start(200, 200), finish(230, 185);
            QMouseEvent press(QEvent::MouseButtonPress, start, view.mapToGlobal(start.toPoint()),
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, finish, view.mapToGlobal(finish.toPoint()),
                             Qt::NoButton, Qt::RightButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, finish,
                                view.mapToGlobal(finish.toPoint()), Qt::RightButton, Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(&view, &press);
            QApplication::sendEvent(&view, &move);
            QApplication::sendEvent(&view, &release);
            auto& transform = mesh.GetComponent<TransformComponent>();
            transform.rotation = {.2f, -.3f, .35f};
            transform.scale = {-3, 2, .75f};
        }
        for (const QSize size : {QSize(800, 600), QSize(1200, 600), QSize(600, 800)}) {
            view.resize(size);
            view.show();
            app.processEvents();
            const QPointF center = ReferenceProjection({6, 0, 6}, size, yaw, pitch);
            const Vec3 eye{static_cast<float>(22 * std::sin(yaw) * std::cos(pitch)),
                           static_cast<float>(-22 * std::sin(pitch)),
                           static_cast<float>(22 * std::cos(yaw) * std::cos(pitch))};
            const Vec3 direction = (Vec3{6, 0, 6} - eye).Normalized();
            auto farMesh = document.CreateEntity("Farther mesh");
            farMesh.GetComponent<TransformComponent>().position = Vec3{6, 0, 6} + direction * 8;
            farMesh.AddComponent<MeshRendererComponent>().material.albedoColor = {0, 0, 1, 1};
            if (gpu) {
                const double ratio = view.devicePixelRatioF();
                QImage image;
                Check(renderer->Render3D(document.GetScene(), std::lround(size.width() * ratio),
                                         std::lround(size.height() * ratio), {}, yaw, pitch, 22,
                                         image),
                      "real Vulkan 3D image renders without a software fallback");
                const QColor pixel = image.pixelColor(std::lround(center.x() * ratio),
                                                      std::lround(center.y() * ratio));
                Check(pixel.red() > 3 && pixel.blue() > 3 && pixel.green() < 15,
                      "reference projection lands on the actual GPU mesh");
            }
            document.Select(std::nullopt);
            Click(view, center);
            Check(document.GetSelected() && document.GetSelected().GetId() == mesh.GetId(),
                  "clicking the rendered mesh center selects it at every aspect ratio");
            Click(view, ReferenceProjection({7, 0, 6}, size, yaw, pitch));
            Check(document.GetSelected() && document.GetSelected().GetId() == mesh.GetId(),
                  "visible mesh surface is selectable beyond the center marker");
            QImage overlay(view.size(), QImage::Format_RGBA8888);
            view.render(&overlay);
            const auto model = mesh.GetComponent<TransformComponent>().GetTransform();
            int alignedCorners = 0;
            for (int corner = 0; corner < 8; ++corner) {
                const Vec3 local{(corner & 1) ? .5f : -.5f, (corner & 2) ? .5f : -.5f,
                                 (corner & 4) ? .5f : -.5f};
                const auto& m = model.elements;
                const Vec3 world{m[0] * local.x + m[4] * local.y + m[8] * local.z + m[12],
                                 m[1] * local.x + m[5] * local.y + m[9] * local.z + m[13],
                                 m[2] * local.x + m[6] * local.y + m[10] * local.z + m[14]};
                const QPoint expected = ReferenceProjection(world, size, yaw, pitch).toPoint();
                bool found = false;
                for (int y = expected.y() - 3; y <= expected.y() + 3; ++y)
                    for (int x = expected.x() - 3; x <= expected.x() + 3; ++x) {
                        if (!overlay.rect().contains(x, y))
                            continue;
                        const auto color = overlay.pixelColor(x, y);
                        found |= color.red() > 220 && color.green() > 160 && color.blue() < 130;
                    }
                alignedCorners += found ? 1 : 0;
            }
            Check(alignedCorners == 8,
                  "selection outline aligns with every transformed GPU corner");
            Click(view, {20, static_cast<double>(size.height() - 20)});
            Check(!document.GetSelected(), "empty viewport space clears the selection");
            document.GetScene().DestroyEntity(farMesh);
        }
    }
    const auto camera = MakeViewportCamera3D(view.size(), {}, .89, .36, 22);
    const Vec3 forward = (camera.GetTarget() - camera.GetPosition()).Normalized();
    Check(!ProjectViewportPoint(camera, view.size(), camera.GetPosition() - forward),
          "points behind the camera are not projected");
    const auto& projection = camera.GetProjectionMatrix().elements;
    const float nearClip = projection[14] / projection[10];
    const float farClip = projection[14] / (projection[10] + 1);
    Check(!ProjectViewportPoint(camera, view.size(),
                                camera.GetPosition() + forward * (nearClip * .5f)),
          "points before the near plane are not projected");
    Check(!ProjectViewportPoint(camera, view.size(),
                                camera.GetPosition() + forward * (farClip * 1.1f)),
          "points beyond the far plane are not projected");
    if (gpu) {
        QTemporaryDir files;
        Check(files.isValid(), "alpha material fixture directory exists");
        const auto model = PathFromUtf8(files.filePath("plane.obj").toUtf8().toStdString());
        std::ofstream(model)
            << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
            << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1 4/4/1\n";
        const auto opaque = files.filePath("opaque.png"), partial = files.filePath("partial.png");
        QImage source(4, 4, QImage::Format_RGBA8888);
        source.fill(QColor(0, 200, 0, 255));
        Check(source.save(opaque), "opaque texture writes");
        source.fill(QColor(0, 200, 0, 128));
        Check(source.save(partial), "partial-alpha texture writes");
        Scene materialScene;
        auto& material =
            materialScene.CreateEntity("Alpha plane").AddComponent<MeshRendererComponent>();
        material.meshPath = PathUtf8(model);
        material.material.baseColorTexture = opaque.toUtf8().toStdString();
        QImage image;
        Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, image),
              "opaque material renders");
        const auto full = image.pixelColor(200, 150);
        material.material.baseColorTexture = partial.toUtf8().toStdString();
        Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, image) &&
                  image.format() == QImage::Format_RGBA8888,
              "3D readback declares the opaque pipeline's straight alpha format");
        const auto alpha = image.pixelColor(200, 150);
        std::cout << "alpha material: opaque=" << full.red() << ',' << full.green() << ','
                  << full.blue() << ',' << full.alpha() << " partial=" << alpha.red() << ','
                  << alpha.green() << ',' << alpha.blue() << ',' << alpha.alpha() << '\n';
        Check(std::abs(alpha.alpha() - 128) <= 1 && std::abs(alpha.green() - full.green()) <= 3,
              "partial-alpha 3D textures retain their straight color when read through Qt");
        QImage composite(400, 300, QImage::Format_RGBA8888);
        composite.fill(Qt::white);
        {
            QPainter painter(&composite);
            painter.drawImage(0, 0, image);
        }
        const auto expected = full.green() * (128.0 / 255) + 255 * (127.0 / 255);
        Check(std::abs(composite.pixelColor(200, 150).green() - expected) <= 3,
              "Qt viewport compositing never applies the material alpha twice");
        auto settings = materialScene.GetSettings();
        settings.render3D.mode = Render3DMode::Pbr;
        settings.render3D.imageBasedLighting = false;
        settings.render3D.shadows = true;
        Check(materialScene.SetSettings(settings), "PBR Qt fixture settings apply");
        material.material.baseColorTexture = opaque.toUtf8().toStdString();
        Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, image),
              "PBR opaque Qt image renders");
        const auto pbrOpaque = image.pixelColor(200, 150);
        material.material.baseColorTexture = partial.toUtf8().toStdString();
        Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, image),
              "PBR partial-alpha Qt image renders");
        const auto pbrPartial = image.pixelColor(200, 150);
        Check(std::abs(pbrPartial.alpha() - 128) <= 1 &&
                  std::abs(pbrPartial.green() - pbrOpaque.green()) <= 3,
              "HDR tone mapping preserves straight-alpha material color");
        Check(image.pixelColor(0, 0).alpha() == 0, "HDR editor background stays transparent");
        settings.exposure = 2.25f;
        Check(materialScene.SetSettings(settings), "shared rendering exposure applies");
        Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, image),
              "editor exposure comparison renders");
        const auto editorPixel = image.pixelColor(200, 150);
        auto* device = renderer->GetGpuDevice();
        Check(device->RequestFrameCapture() && device->BeginFrame() == GpuDevice::FrameStart::Ready,
              "runtime-path comparison frame begins");
        device->Clear(0, 0, 0, 0);
        {
            Renderer3D::Context runtimeContext(device);
            Renderer3D::ScopedContext scoped(runtimeContext);
            const auto parityCamera = MakeViewportCamera3D(QSizeF(400, 300), {}, 0, 0, 5);
            Renderer3D::BeginScene(parityCamera, settings.render3D, settings.exposure);
            materialScene.OnRender3D();
            Renderer3D::EndScene();
        }
        Check(device->EndFrame(), "runtime-path comparison submits after context release");
        GpuFrameCapture runtimeCapture;
        Check(device->ReadFrameCapture(runtimeCapture), "runtime-path comparison capture reads");
        const size_t offset = (150 * 400 + 200) * 4;
        Check(
            std::abs(runtimeCapture.rgba[offset] - editorPixel.red()) <= 2 &&
                std::abs(runtimeCapture.rgba[offset + 1] - editorPixel.green()) <= 2 &&
                std::abs(runtimeCapture.rgba[offset + 2] - editorPixel.blue()) <= 2,
            "editor and runtime scene path agree for identical camera and post-lighting exposure");
        settings.render3D.showSkybox = true;
        Check(materialScene.SetSettings(settings), "skybox composition settings apply");
        QImage background, foreground;
        const auto skyboxDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            Check(renderer->Render3D(materialScene, 400, 300, {}, 0, 0, 5, foreground, &background),
                  "editor skybox and foreground render separately");
            if (background.pixelColor(0, 0).alpha() == 255)
                break;
        } while (std::chrono::steady_clock::now() < skyboxDeadline);
        Check(background.pixelColor(0, 0).alpha() == 255 &&
                  foreground.pixelColor(0, 0).alpha() == 0,
              "skybox remains opaque beneath transparent object-only foreground");
        QImage layered = background;
        {
            QPainter painter(&layered);
            painter.setPen(QColor(255, 0, 0));
            painter.drawLine(0, 0, 30, 0);
            painter.drawImage(0, 0, foreground);
        }
        Check(layered.pixelColor(10, 0).red() == 255 && layered.pixelColor(10, 0).green() == 0,
              "editor grid remains visible above skybox outside object coverage");
    }
    if (gpu)
        Check(renderer->GetGpuDevice()->GetCounters().validationErrors == 0,
              "3D regression emits no Vulkan validation errors");
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxRegressionTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EditorRegression"));
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    EngineSceneRenderer::SetForceAllowed(false);
    try {
        Check(argc == 2, "a regression suite must be specified");
        const std::string suite = argv[1];
        if (suite == "timeline")
            TestTimeline(app);
        else if (suite == "sparse-copy")
            TestSparseCopy(app);
        else if (suite == "names")
            TestNameLimits(app);
        else if (suite == "sparse-tilemap")
            TestSparseTilemap(app);
        else if (suite == "picking2d")
            TestPicking2D(app);
        else if (suite == "picking2d-gpu") {
            EngineSceneRenderer::SetForceAllowed(true);
            TestPicking2D(app, true);
        } else if (suite == "picking3d")
            TestPicking3D(app);
        else if (suite == "picking3d-gpu") {
            EngineSceneRenderer::SetForceAllowed(true);
            TestPicking3D(app, true);
        } else
            throw std::runtime_error("unknown regression suite");
        std::cout << suite << " regression checks passed\n";
        return 0;
    } catch (const GpuUnavailable& error) {
        std::cerr << "GPU unavailable: " << error.what() << '\n';
        return 77;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
