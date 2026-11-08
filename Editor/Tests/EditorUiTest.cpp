#include "App/EditorWindow.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <QPushButton>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
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
        if (copyAction && pasteAction) {
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
            undo->undo();
            Check(hierarchy->count() == 2, "undo removes pasted entity");
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
