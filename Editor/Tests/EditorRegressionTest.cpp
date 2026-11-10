#include "App/EditorWindow.hpp"
#include "Project/PathEncoding.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMimeData>
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
        else
            throw std::runtime_error("unknown regression suite");
        std::cout << suite << " regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
