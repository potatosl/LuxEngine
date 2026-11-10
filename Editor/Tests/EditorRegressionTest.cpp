#include "App/EditorWindow.hpp"
#include "Project/PathEncoding.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace LuxEngine;
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
    auto* hierarchy = window->findChild<QListWidget*>(QStringLiteral("sceneOutliner"));
    Check(hierarchy && hierarchy->count() > 0, "saved scene opens in the editor");
    hierarchy->setCurrentRow(0);
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
        else if (suite == "picking2d")
            TestPicking2D(app);
        else if (suite == "picking2d-gpu") {
            EngineSceneRenderer::SetForceAllowed(true);
            TestPicking2D(app, true);
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
