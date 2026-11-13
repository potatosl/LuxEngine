#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Mouse(QWidget* target, QEvent::Type type, QPointF point, Qt::MouseButton button,
           Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, point, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxAnimationTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimationWorkspace"));
    QTemporaryDir files;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, files.path());
    LuxEditor::EngineSceneRenderer::SetForceAllowed(false);
    try {
        Check(files.isValid(), "Animation test directory is unavailable");
        LuxEngine::Scene scene;
        auto entity = scene.CreateEntity("Animated");
        auto& animation = entity.AddComponent<LuxEngine::AnimationComponent>();
        animation.clip.loop = false;
        animation.clip.duration = 2;
        animation.clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({1, {5, 1, 0}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({2, {10, 2, 0}, {}, {1, 1, 1}});
        const auto path = files.filePath(QStringLiteral("Animation.lux"));
        Check(scene.Serialize(path.toUtf8().toStdString()), "Animation fixture did not save");
        QSettings().setValue(QStringLiteral("lastScene"), path);
        LuxEditor::EditorWindow window;
        window.resize(1600, 1000);
        window.show();
        window.ExecuteRecordedEdit(QStringLiteral("select"), [&] {
            window.GetDocument().Select(
                window.GetDocument().GetScene().GetEntities().front().GetId());
        });
        app.processEvents();
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("AnimationWorkspace"));
        Check(dock, "Animation workspace is unavailable");
        auto* openWorkspace = window.findChild<QAction*>(QStringLiteral("openAnimationWorkspace"));
        Check(openWorkspace, "Animation menu action is unavailable");
        openWorkspace->trigger();
        app.processEvents();
        auto* workspace = window.findChild<QTabBar*>(QStringLiteral("workspaceTabs"));
        if (!dock->isVisible())
            std::cerr << "Animation state: tab=" << (workspace ? workspace->currentIndex() : -1)
                      << " hidden=" << dock->isHidden() << " ancestorVisible=" << window.isVisible()
                      << " actionChecked=" << dock->toggleViewAction()->isChecked() << '\n';
        Check(dock->isVisible(), "Animation menu action did not open the workspace");
        QAction* moveCommand = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->shortcut() == QKeySequence(Qt::Key_W))
                moveCommand = action;
        Check(moveCommand, "Editor move shortcut command is unavailable");
        moveCommand->trigger();
        auto* moveTool = window.findChild<QToolButton*>(QStringLiteral("animationTool1"));
        Check(moveTool && moveTool->isChecked(),
              "Editor move command was not routed to animation tools");
        auto* timeline = window.findChild<QSlider*>(QStringLiteral("animationTimeline"));
        auto* keys = window.findChild<QListWidget*>(QStringLiteral("animationKeys"));
        auto* keyTime = window.findChild<QDoubleSpinBox*>(QStringLiteral("animationKeyTime"));
        auto* keyX = window.findChild<QDoubleSpinBox*>(QStringLiteral("animationKeyValue00"));
        auto* undo = window.findChild<QUndoStack*>();
        Check(timeline && keys && keyTime && keyX && undo,
              "Animation editing controls are unavailable");
        Check(window.findChild<QWidget*>(QStringLiteral("animationPreview2D")) &&
                  window.findChild<QWidget*>(QStringLiteral("animationPreview3D")),
              "Dedicated animation previews are unavailable");
        keys->setCurrentRow(1);
        Check(std::abs(keyTime->value() - 1) < .0001, "Key selection did not populate details");
        keyX->setValue(7.5);
        auto current = [&]() -> LuxEngine::AnimationComponent& {
            return window.GetDocument().GetSelected().GetComponent<LuxEngine::AnimationComponent>();
        };
        Check(current().clip.keyframes[1].position.x == 7.5f,
              "Key value edit did not update the clip");
        undo->undo();
        Check(current().clip.keyframes[1].position.x == 5, "Key value undo failed");
        undo->redo();
        auto* autoPlay = window.findChild<QCheckBox*>(QStringLiteral("animationAutoPlay"));
        Check(autoPlay && autoPlay->isChecked(), "Runtime autoplay state is unavailable");
        const auto countBeforePause = current().clip.keyframes.size();
        autoPlay->setChecked(false);
        Check(!current().playing && current().clip.keyframes.size() == countBeforePause &&
                  current().clip.keyframes[1].position.x == 7.5f,
              "Disabling autoplay discarded authored animation keys");
        undo->undo();
        Check(current().playing && current().clip.keyframes.size() == countBeforePause,
              "Autoplay undo did not preserve the clip");
        keys->setCurrentRow(1);
        keyTime->setValue(1.25);
        Check(current().clip.keyframes[1].time == 1.25f, "Key time edit did not update the clip");
        undo->undo();
        Check(current().clip.keyframes[1].time == 1, "Key time undo failed");
        undo->redo();
        keys->setCurrentRow(1);
        timeline->resize(1000, 220);
        const double span = timeline->width() - 174 - 18;
        const QPointF start(174 + span * 1.25 / 2, 86);
        const QPointF end(174 + span * 1.5 / 2, 86);
        Mouse(timeline, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        Mouse(timeline, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        Mouse(timeline, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        Check(std::abs(current().clip.keyframes[1].time - 1.5f) < .0001f,
              "Dragging a key on the dopesheet did not move it");
        undo->undo();
        Check(current().clip.keyframes[1].time == 1.25f, "Key drag undo failed");
        if (const char* capture = std::getenv("LUX_ANIMATION_CAPTURE")) {
            app.processEvents();
            Check(window.grab().save(QString::fromUtf8(capture)),
                  "Animation screenshot did not save");
        }
        auto* next = window.findChild<QPushButton*>(QStringLiteral("animationNextFrame"));
        auto* add = window.findChild<QPushButton*>(QStringLiteral("animationAddKey"));
        Check(next && add, "Animation frame transport is unavailable");
        current().clip.duration = 120;
        current().currentTime = 0;
        next->click();
        const float preciseTime = current().currentTime;
        add->click();
        Check(preciseTime > 0 && std::abs(current().clip.keyframes[1].time - preciseTime) < .0001f,
              "Key insertion lost sub-slider frame precision on a long clip");
        const auto keysBeforeDelete = current().clip.keyframes.size();
        const auto entitiesBeforeDelete = window.GetDocument().GetScene().GetEntityCount();
        QKeyEvent overrideDelete(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
        QKeyEvent deleteKey(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(timeline, &overrideDelete);
        QApplication::sendEvent(timeline, &deleteKey);
        Check(overrideDelete.isAccepted() &&
                  current().clip.keyframes.size() + 1 == keysBeforeDelete &&
                  window.GetDocument().GetScene().GetEntityCount() == entitiesBeforeDelete,
              "Timeline Delete removed the scene object instead of its key");
        undo->undo();
        auto* save = window.findChild<QPushButton*>(QStringLiteral("animationSave"));
        Check(save, "Animation save command is unavailable");
        save->click();
        Check(!window.GetDocument().IsDirty(), "Animation save did not persist the clip");
        LuxEngine::Scene persisted;
        Check(persisted.Deserialize(path.toUtf8().toStdString()),
              "Saved animation scene did not reopen");
        Check(persisted.GetEntities()
                      .front()
                      .GetComponent<LuxEngine::AnimationComponent>()
                      .clip.keyframes.size() == current().clip.keyframes.size(),
              "Saved animation keys were lost");
        window.GetDocument().SetDirty(false);
        window.close();
        std::cout << "Animation workspace key editing, drag, undo and frame precision passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
