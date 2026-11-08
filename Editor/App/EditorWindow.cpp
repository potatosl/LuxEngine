#include "EditorWindow.hpp"
#include "Debug/DebugPanel.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Project/PathEncoding.hpp"
#include "Project/ProjectArchive.hpp"
#include "Project/ProjectDialog.hpp"
#include "Ui/IconFactory.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include "Viewport/SceneViewport3D.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <numbers>

namespace LuxEngine {
namespace {
QString Q(const std::string& value) {
    return QString::fromUtf8(value);
}
QString Q(const std::filesystem::path& value) {
    return Q(PathUtf8(value));
}
std::string Utf8(const QString& value) {
    return value.toUtf8().toStdString();
}
std::optional<uint32> SelectedId(EditorDocument& document) {
    const Entity entity = document.GetSelected();
    return entity ? std::optional<uint32>(entity.GetId()) : std::nullopt;
}
class SceneEditCommand final : public QUndoCommand {
public:
    SceneEditCommand(QString name, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(std::move(name)), undo_(std::move(undo)), redo_(std::move(redo)) {}
    void undo() override {
        undo_();
    }
    void redo() override {
        if (first_)
            first_ = false;
        else
            redo_();
    }

private:
    std::function<void()> undo_, redo_;
    bool first_ = true;
};
} // namespace
EditorWindow::EditorWindow(QWidget* parent)
    : QMainWindow(parent),
      recentProjects_(
          PathFromUtf8(Utf8(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))) /
          "recent-projects.txt"),
      undo_(new QUndoStack(this)) {
    setWindowTitle(QStringLiteral("LuxEditor"));
    setMinimumSize(960, 600);
    resize(1360, 860);
    setAcceptDrops(true);
    recentProjects_.Load();
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] { AutoSaveScene(); });
    previewTimer_.setInterval(16);
    connect(&previewTimer_, &QTimer::timeout, this, [this] { TickPreview(); });
    connect(undo_, &QUndoStack::indexChanged, this, [this] {
        if (undo_->isClean())
            document_.SetDirty(false);
        else
            document_.MarkDirty();
        UpdateChrome();
    });
    BuildLayout();
    BuildMenus();
    const QSettings settings;
    const auto previous =
        PathFromUtf8(Utf8(settings.value(QStringLiteral("lastProject")).toString()));
    if (!previous.empty() && std::filesystem::is_regular_file(previous))
        OpenProjectFile(previous);
    else {
        const std::string scene = Utf8(settings.value(QStringLiteral("lastScene")).toString());
        if (!scene.empty() && std::filesystem::is_regular_file(PathFromUtf8(scene)))
            document_.Open(scene);
    }
    RefreshAll();
}
EditorWindow::~EditorWindow() {
    autosaveTimer_.stop();
    previewTimer_.stop();
    undo_->clear();
    delete takeCentralWidget();
    for (QDockWidget* dock : docks_) {
        removeDockWidget(dock);
        delete dock;
    }
}
void EditorWindow::BuildMenus() {
    auto add = [this](QMenu* menu, const QString& title, const QString& id,
                      const QKeySequence& fallback, const std::function<void()>& action) {
        QAction* item = menu->addAction(title);
        item->setObjectName(id);
        item->setShortcut(QKeySequence(
            QSettings().value(QStringLiteral("shortcuts/") + id, fallback.toString()).toString()));
        connect(item, &QAction::triggered, this, [action] { action(); });
        shortcutActions_.push_back(item);
        return item;
    };
    QMenu* file = menuBar()->addMenu(QStringLiteral("파일"));
    add(file, QStringLiteral("새 프로젝트..."), QStringLiteral("newProject"), {},
        [this] { NewProject(); });
    add(file, QStringLiteral("프로젝트 열기..."), QStringLiteral("openProject"), {},
        [this] { OpenProjectDialog(); });
    recentMenu_ = file->addMenu(QStringLiteral("최근 프로젝트"));
    projectSettingsAction_ =
        add(file, QStringLiteral("프로젝트 설정..."), QStringLiteral("projectSettings"), {},
            [this] { EditProjectSettings(); });
    cloneAction_ = add(file, QStringLiteral("프로젝트 복제..."), QStringLiteral("cloneProject"), {},
                       [this] { CloneCurrentProject(); });
    add(file, QStringLiteral("프로젝트 가져오기..."), QStringLiteral("importProject"), {},
        [this] { ImportProjectDialog(); });
    exportAction_ = add(file, QStringLiteral("프로젝트 내보내기..."),
                        QStringLiteral("exportProject"), {}, [this] { ExportCurrentProject(); });
    backupAction_ = add(file, QStringLiteral("프로젝트 백업"), QStringLiteral("backupProject"), {},
                        [this] { BackupCurrentProject(); });
    restoreAction_ = add(file, QStringLiteral("프로젝트 백업 복구..."),
                         QStringLiteral("restoreProject"), {}, [this] { RestoreCurrentProject(); });
    rescanAction_ = add(file, QStringLiteral("리소스 재검색"), QStringLiteral("rescanResources"),
                        {}, [this] { RescanProjectResources(); });
    file->addSeparator();
    add(file, QStringLiteral("새 장면"), QStringLiteral("newScene"), QKeySequence::New, [this] {
        if (!ConfirmDiscardChanges())
            return;
        document_.NewScene();
        undo_->clear();
        RefreshAll();
    });
    add(file, QStringLiteral("장면 열기..."), QStringLiteral("openScene"), QKeySequence::Open,
        [this] {
            if (!ConfirmDiscardChanges())
                return;
            const QString file =
                QFileDialog::getOpenFileName(this, QStringLiteral("장면 열기"), {},
                                             QStringLiteral("Lux 장면 (*.lux);;모든 파일 (*)"));
            if (file.isEmpty())
                return;
            if (!document_.Open(Utf8(file))) {
                ShowError(QStringLiteral("열기 실패"),
                          QStringLiteral("장면 파일을 열 수 없습니다."));
                return;
            }
            project_.reset();
            undo_->clear();
            ApplyProjectPreferences();
            RefreshAll();
        });
    add(file, QStringLiteral("저장"), QStringLiteral("saveScene"), QKeySequence::Save,
        [this] { SaveScene(false); });
    add(file, QStringLiteral("다른 이름으로 저장..."), QStringLiteral("saveSceneAs"),
        QKeySequence::SaveAs, [this] { SaveScene(true); });
    file->addSeparator();
    add(file, QStringLiteral("끝내기"), QStringLiteral("quit"), QKeySequence::Quit,
        [this] { close(); });
    QMenu* edit = menuBar()->addMenu(QStringLiteral("편집"));
    undoAction_ = undo_->createUndoAction(this, QStringLiteral("실행 취소"));
    undoAction_->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAction_);
    redoAction_ = undo_->createRedoAction(this, QStringLiteral("다시 실행"));
    redoAction_->setShortcut(QKeySequence::Redo);
    edit->addAction(redoAction_);
    add(edit, QStringLiteral("복사"), QStringLiteral("copyEntity"), QKeySequence::Copy,
        [this] { CopyEntity(); });
    add(edit, QStringLiteral("붙여넣기"), QStringLiteral("pasteEntity"), QKeySequence::Paste,
        [this] { PasteEntity(); });
    edit->addSeparator();
    add(edit, QStringLiteral("2D 엔티티 추가"), QStringLiteral("addEntity"),
        QKeySequence(Qt::CTRL | Qt::Key_E), [this] { AddEntity(); });
    add(edit, QStringLiteral("3D 메시 엔티티 추가"), QStringLiteral("addMeshEntity"), {},
        [this] { AddMeshEntity(); });
    add(edit, QStringLiteral("타일맵 추가"), QStringLiteral("addTilemap"), {},
        [this] { AddTilemap(); });
    add(edit, QStringLiteral("선택 삭제"), QStringLiteral("deleteEntity"), QKeySequence::Delete,
        [this] { DeleteEntity(); });
    QMenu* view = menuBar()->addMenu(QStringLiteral("보기"));
    snapAction_ = view->addAction(QStringLiteral("격자에 맞춤"));
    snapAction_->setCheckable(true);
    snapAction_->setChecked(true);
    connect(snapAction_, &QAction::toggled, this, [this](bool enabled) {
        canvas_->SetSnapToGrid(enabled);
        if (project_) {
            auto settings = project_->GetSettings();
            settings.snapToGrid = enabled;
            std::string error;
            if (!project_->UpdateSettings(settings, error))
                ShowError(QStringLiteral("프로젝트 설정 저장 실패"), Q(error));
        }
    });
    view->addSeparator();
    add(view, QStringLiteral("2D 작업 화면"), QStringLiteral("view2D"), {},
        [this] { viewportTabs_->setCurrentIndex(1); });
    add(view, QStringLiteral("3D 배치 화면"), QStringLiteral("view3D"), {},
        [this] { viewportTabs_->setCurrentIndex(0); });
    add(view, QStringLiteral("선택 항목에 초점"), QStringLiteral("focusSelection"),
        QKeySequence(Qt::Key_F), [this] {
            if (viewportTabs_->currentWidget() == canvas3D_)
                canvas3D_->FocusSelection();
            else
                canvas_->FocusSelection();
        });
    add(view, QStringLiteral("전체 화면"), QStringLiteral("fullscreen"), QKeySequence(Qt::Key_F11),
        [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    colliderAction_ = view->addAction(QStringLiteral("2D 충돌체 표시"));
    colliderAction_->setCheckable(true);
    connect(colliderAction_, &QAction::toggled, this, [this](bool enabled) {
        canvas_->SetDebugColliders(enabled);
        overviewCanvas_->SetDebugColliders(enabled);
    });
    QMenu* debug = menuBar()->addMenu(QStringLiteral("디버그"));
    add(debug, QStringLiteral("장면 실행"), QStringLiteral("previewStart"),
        QKeySequence(Qt::Key_F5), [this] { StartPreview(); });
    add(debug, QStringLiteral("일시정지/재개"), QStringLiteral("previewPause"),
        QKeySequence(Qt::Key_F6), [this] { TogglePreviewPause(); });
    add(debug, QStringLiteral("한 프레임 실행"), QStringLiteral("previewStep"),
        QKeySequence(Qt::Key_F7), [this] { StepPreview(); });
    add(debug, QStringLiteral("실행 중지"), QStringLiteral("previewStop"),
        QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { StopPreview(); });
    debug->addSeparator();
    add(debug, QStringLiteral("별도 창에서 실행"), QStringLiteral("playWindow"),
        QKeySequence(Qt::CTRL | Qt::Key_F5), [this] { RunSceneInWindow(); });
    add(debug, QStringLiteral("게임 창 닫기"), QStringLiteral("playWindowStop"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5), [this] { StopSceneWindow(); });
    breakOnCollisionAction_ = debug->addAction(QStringLiteral("충돌 시 일시정지"));
    breakOnCollisionAction_->setCheckable(true);
    debug->addAction(docks_[7]->toggleViewAction());
    QMenu* buildMenu = menuBar()->addMenu(QStringLiteral("빌드"));
    add(buildMenu, QStringLiteral("Debug 빌드"), QStringLiteral("buildDebug"),
        QKeySequence(Qt::CTRL | Qt::Key_B),
        [this] { StartBuild(BuildConfiguration::Debug, false, BuildFollowUp::None); });
    add(buildMenu, QStringLiteral("Release 빌드"), QStringLiteral("buildRelease"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
        [this] { StartBuild(BuildConfiguration::Release, false, BuildFollowUp::None); });
    add(buildMenu, QStringLiteral("빌드 후 실행 (Debug)"), QStringLiteral("buildRun"),
        QKeySequence(Qt::Key_F9),
        [this] { StartBuild(BuildConfiguration::Debug, false, BuildFollowUp::Run); });
    add(buildMenu, QStringLiteral("클린 빌드 (Debug)"), QStringLiteral("buildClean"), {},
        [this] { StartBuild(BuildConfiguration::Debug, true, BuildFollowUp::None); });
    buildMenu->addSeparator();
    add(buildMenu, QStringLiteral("배포 ZIP 만들기 (Release)..."), QStringLiteral("buildPackage"),
        {}, [this] { StartBuild(BuildConfiguration::Release, false, BuildFollowUp::Package); });
    buildMenu->addSeparator();
    add(buildMenu, QStringLiteral("빌드 중지"), QStringLiteral("buildCancel"), {},
        [this] { CancelBuild(); });
    add(buildMenu, QStringLiteral("엔진 SDK 경로 설정..."), QStringLiteral("buildSdk"), {},
        [this] { ChooseEnginePackage(); });
    QMenu* window = menuBar()->addMenu(QStringLiteral("창"));
    add(window, QStringLiteral("새 에디터 창"), QStringLiteral("newWindow"), {}, [] {
        auto* next = new EditorWindow;
        next->setAttribute(Qt::WA_DeleteOnClose);
        next->show();
    });
    QMenu* displays = window->addMenu(QStringLiteral("화면으로 이동"));
    connect(displays, &QMenu::aboutToShow, this, [this, displays] {
        displays->clear();
        for (QScreen* screen : QGuiApplication::screens())
            displays->addAction(screen->name(), this, [this, screen] {
                const QRect area = screen->availableGeometry();
                move(area.center() - rect().center());
            });
    });
    add(window, QStringLiteral("명령 검색..."), QStringLiteral("commandPalette"),
        QKeySequence(Qt::CTRL | Qt::Key_P), [this] { OpenCommandPalette(); });
    window->addSeparator();
    for (QDockWidget* dock : docks_)
        window->addAction(dock->toggleViewAction());
    window->addSeparator();
    add(window, QStringLiteral("기본 배치 복원"), QStringLiteral("resetLayout"), {},
        [this] { ResetWorkspaceLayout(); });
    QMenu* preferences = menuBar()->addMenu(QStringLiteral("설정"));
    QMenu* themes = preferences->addMenu(QStringLiteral("테마"));
    for (const auto& [id, label] : std::array<std::pair<const char*, const char*>, 2>{
             {{"dark", "다크"}, {"light", "라이트"}}})
        themes->addAction(QString::fromUtf8(label), this, [this, id] {
            theme_ = QString::fromLatin1(id);
            ApplyTheme();
        });
    themes->addAction(QStringLiteral("사용자 정의 스타일 불러오기..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("스타일 파일 선택"),
                                                          {}, QStringLiteral("Qt 스타일 (*.qss)"));
        QFile file(path);
        if (!path.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
            QSettings().setValue(QStringLiteral("customStyle"), QString::fromUtf8(file.readAll()));
            theme_ = QStringLiteral("custom");
            ApplyTheme();
        }
    });
    QMenu* scale = preferences->addMenu(QStringLiteral("UI 배율"));
    for (double factor : {.75, 1.0, 1.25, 1.5, 2.0})
        scale->addAction(QStringLiteral("%1%").arg(static_cast<int>(factor * 100)), this,
                         [this, factor] {
                             uiScale_ = factor;
                             ApplyTheme();
                         });
    add(preferences, QStringLiteral("단축키 설정..."), QStringLiteral("shortcuts"), {},
        [this] { ConfigureShortcuts(); });
    add(preferences, QStringLiteral("설정 내보내기..."), QStringLiteral("exportSettings"), {},
        [this] { ExportEditorSettings(); });
    add(preferences, QStringLiteral("설정 가져오기..."), QStringLiteral("importSettings"), {},
        [this] { ImportEditorSettings(); });
    UpdateRecentMenu();
}
void EditorWindow::UpdateRecentMenu() {
    recentMenu_->clear();
    const auto& files = recentProjects_.Get();
    if (files.empty()) {
        auto* item = recentMenu_->addAction(QStringLiteral("최근 프로젝트가 없습니다."));
        item->setEnabled(false);
        return;
    }
    for (const auto& file : files)
        recentMenu_->addAction(Q(file), this, [this, file] { OpenProjectFile(file); });
}
void EditorWindow::OpenCommandPalette() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("명령 검색"));
    dialog.resize(460, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QStringLiteral("명령을 입력해 주세요."));
    auto* list = new QListWidget(&dialog);
    layout->addWidget(search);
    layout->addWidget(list);
    std::vector<QAction*> commands;
    for (QAction* action : shortcutActions_)
        if (action->isEnabled() && action->text() != QStringLiteral("끝내기"))
            commands.push_back(action);
    auto refresh = [search, list, commands] {
        list->clear();
        for (QAction* action : commands)
            if (action->text().contains(search->text(), Qt::CaseInsensitive))
                list->addItem(action->text());
        if (list->count())
            list->setCurrentRow(0);
    };
    connect(search, &QLineEdit::textChanged, &dialog, refresh);
    connect(search, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    refresh();
    search->setFocus();
    if (dialog.exec() != QDialog::Accepted || !list->currentItem())
        return;
    const QString chosen = list->currentItem()->text();
    for (QAction* action : commands)
        if (action->text() == chosen) {
            action->trigger();
            break;
        }
}
void EditorWindow::ConfigureShortcuts() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("단축키 설정"));
    dialog.resize(540, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    for (QAction* action : shortcutActions_)
        list->addItem(action->text());
    auto* editor = new QKeySequenceEdit(&dialog);
    layout->addWidget(list);
    layout->addWidget(new QLabel(QStringLiteral("선택한 명령의 단축키"), &dialog));
    layout->addWidget(editor);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(list, &QListWidget::currentRowChanged, &dialog, [this, editor](int row) {
        if (row >= 0)
            editor->setKeySequence(shortcutActions_[static_cast<size_t>(row)]->shortcut());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    list->setCurrentRow(0);
    if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0)
        return;
    auto* action = shortcutActions_[static_cast<size_t>(list->currentRow())];
    action->setShortcut(editor->keySequence());
    QSettings().setValue(QStringLiteral("shortcuts/") + action->objectName(),
                         editor->keySequence().toString());
    statusBar()->showMessage(QStringLiteral("단축키를 저장했습니다."), 4000);
}
void EditorWindow::ExportEditorSettings() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("에디터 설정 내보내기"),
                                                      QStringLiteral("LuxEditorSettings.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QJsonObject shortcuts;
    for (QAction* action : shortcutActions_)
        shortcuts.insert(action->objectName(), action->shortcut().toString());
    QJsonObject settings{{QStringLiteral("format"), 1},
                         {QStringLiteral("theme"), theme_},
                         {QStringLiteral("uiScale"), uiScale_},
                         {QStringLiteral("shortcuts"), shortcuts}};
    if (theme_ == QLatin1String("custom"))
        settings.insert(QStringLiteral("customStyle"),
                        QSettings().value(QStringLiteral("customStyle")).toString());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QJsonDocument(settings).toJson()) < 0)
        ShowError(QStringLiteral("설정 내보내기 실패"),
                  QStringLiteral("설정 파일을 저장할 수 없습니다."));
    else
        Log(QStringLiteral("에디터 설정을 내보냈습니다."));
}
void EditorWindow::ImportEditorSettings() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("에디터 설정 가져오기"),
                                                      {}, QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        ShowError(QStringLiteral("설정 가져오기 실패"),
                  QStringLiteral("설정 파일을 읽을 수 없습니다."));
        return;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = doc.object();
    const QString theme = object.value(QStringLiteral("theme")).toString();
    const double scale = object.value(QStringLiteral("uiScale")).toDouble(-1);
    if (parse.error != QJsonParseError::NoError ||
        object.value(QStringLiteral("format")).toInt() != 1 ||
        (theme != QLatin1String("dark") && theme != QLatin1String("light") &&
         theme != QLatin1String("custom")) ||
        scale < .75 || scale > 2.0 || !object.value(QStringLiteral("shortcuts")).isObject()) {
        ShowError(QStringLiteral("설정 가져오기 실패"),
                  QStringLiteral("설정 파일 형식이 올바르지 않습니다."));
        return;
    }
    QSettings settings;
    if (theme == QLatin1String("custom")) {
        const QString style = object.value(QStringLiteral("customStyle")).toString();
        if (style.size() > 1024 * 1024) {
            ShowError(QStringLiteral("설정 가져오기 실패"),
                      QStringLiteral("스타일 파일이 너무 큽니다."));
            return;
        }
        settings.setValue(QStringLiteral("customStyle"), style);
    }
    const QJsonObject shortcuts = object.value(QStringLiteral("shortcuts")).toObject();
    for (QAction* action : shortcutActions_) {
        const auto value = shortcuts.value(action->objectName());
        if (value.isString()) {
            action->setShortcut(QKeySequence(value.toString()));
            settings.setValue(QStringLiteral("shortcuts/") + action->objectName(),
                              value.toString());
        }
    }
    theme_ = theme;
    uiScale_ = scale;
    ApplyTheme();
    Log(QStringLiteral("에디터 설정을 가져왔습니다."));
}
void EditorWindow::Log(const QString& message, bool error) {
    consoleOutput_->appendPlainText(message);
    debugPanel_->AddLog(message, error);
    if (error) {
        issuesOutput_->appendPlainText(message);
        docks_[4]->show();
        docks_[4]->raise();
    }
    statusBar()->showMessage(message, 5000);
}
void EditorWindow::ShowError(const QString& title, const QString& message) {
    Log(title + QStringLiteral(": ") + message, true);
    QMessageBox::critical(this, title, message);
}
void EditorWindow::RunWithProgress(const QString& message, const std::function<void()>& task) {
    QProgressDialog progress(message, QString(), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();
    task();
    progress.close();
}
void EditorWindow::closeEvent(QCloseEvent* event) {
    StopPreview();
    StopSceneWindow();
    CancelBuild();
    if (!ConfirmDiscardChanges()) {
        event->ignore();
        return;
    }
    SaveWorkspaceLayout();
    event->accept();
}
void EditorWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (assetPreviewPanel_)
        assetPreviewPanel_->setVisible(width() >= 1500);
    if (docks_[7]) {
        if (width() < 1180 && docks_[7]->isVisible()) {
            docks_[7]->hide();
            debugDockHiddenForSize_ = true;
        } else if (width() >= 1180 && debugDockHiddenForSize_) {
            docks_[7]->show();
            debugDockHiddenForSize_ = false;
        }
    }
}
void EditorWindow::dragEnterEvent(QDragEnterEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
        if (suffix == QLatin1String("luxproj") || suffix == QLatin1String("lux") ||
            suffix == QLatin1String("luxpack") || suffix == QLatin1String("luxbackup")) {
            event->acceptProposedAction();
            return;
        }
    }
}
void EditorWindow::dropEvent(QDropEvent* event) {
    if (event->mimeData()->urls().isEmpty())
        return;
    const auto path = PathFromUtf8(Utf8(event->mimeData()->urls().first().toLocalFile()));
    if (path.extension() == ".luxproj")
        OpenProjectFile(path);
    else if (path.extension() == ".lux")
        OpenProjectScene(path);
    else if (path.extension() == ".luxpack" || path.extension() == ".luxbackup") {
        auto destination =
            ShowProjectDestinationDialog(this, "프로젝트 가져오기", PathUtf8(path.stem()));
        if (destination) {
            std::string error;
            auto result = ImportProject(path, *destination, error);
            if (result)
                OpenProjectFile(result->GetFile());
            else
                ShowError(QStringLiteral("가져오기 실패"), Q(error));
        }
    }
    event->acceptProposedAction();
}
void EditorWindow::RefreshAll() {
    RefreshHierarchy();
    RefreshProjectExplorer();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::RefreshProjectExplorer() {
    projectFiles_->clear();
    projectFilePaths_.clear();
    if (!project_) {
        assetPreview_->setPixmap(QPixmap());
        assetPreview_->setText(QStringLiteral("프로젝트를 열어 주세요."));
        assetDetails_->clear();
        return;
    }
    const auto root = project_->GetFile().parent_path();
    const QString filter = projectSearch_->text();
    const int category = assetFilter_->currentIndex();
    QFileIconProvider icons;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    for (; !error && it != end && projectFilePaths_.size() < 500; it.increment(error)) {
        const auto relative = it->path().lexically_relative(root);
        if (!relative.empty() && (*relative.begin() == ".git" || *relative.begin() == ".lux")) {
            if (it->is_directory(error))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error)) {
            const QString label = Q(GenericPathUtf8(relative));
            const QString suffix = Q(PathUtf8(it->path().extension())).toLower();
            const bool scene = suffix == QLatin1String(".lux");
            const bool image = suffix == QLatin1String(".png") || suffix == QLatin1String(".jpg") ||
                               suffix == QLatin1String(".jpeg") ||
                               suffix == QLatin1String(".webp") || suffix == QLatin1String(".bmp");
            const bool audio = suffix == QLatin1String(".wav") || suffix == QLatin1String(".ogg") ||
                               suffix == QLatin1String(".mp3");
            if ((filter.isEmpty() || label.contains(filter, Qt::CaseInsensitive)) &&
                (category == 0 || (category == 1 && scene) || (category == 2 && image) ||
                 (category == 3 && audio) || (category == 4 && !scene && !image && !audio))) {
                projectFilePaths_.push_back(it->path());
                auto* item = new QListWidgetItem(label);
                const QPixmap thumbnail(Q(it->path()));
                item->setIcon(thumbnail.isNull() ? icons.icon(QFileInfo(Q(it->path())))
                                                 : QIcon(thumbnail));
                item->setToolTip(label);
                projectFiles_->addItem(item);
            }
        }
    }
}
void EditorWindow::RefreshHierarchy() {
    refreshingHierarchy_ = true;
    hierarchy_->clear();
    hierarchyIds_.clear();
    Entity selected = document_.GetSelected();
    size_t sprites = 0, meshes = 0;
    const auto entities = document_.GetScene().GetEntities();
    for (Entity entity : entities) {
        sprites += entity.HasComponent<SpriteRendererComponent>() ? 1 : 0;
        meshes += entity.HasComponent<MeshRendererComponent>() ? 1 : 0;
    }
    const std::array<size_t, 3> counts{entities.size(), sprites, meshes};
    for (size_t i = 0; i < counts.size(); ++i)
        groupTree_->topLevelItem(static_cast<int>(i))->setText(1, QString::number(counts[i]));
    for (Entity entity : entities) {
        if (groupFilter_ == 1 && !entity.HasComponent<SpriteRendererComponent>())
            continue;
        if (groupFilter_ == 2 && !entity.HasComponent<MeshRendererComponent>())
            continue;
        const QString name = entity.HasComponent<TagComponent>()
                                 ? Q(entity.GetComponent<TagComponent>().tag)
                                 : QStringLiteral("Entity");
        if (!name.contains(hierarchySearch_->text(), Qt::CaseInsensitive))
            continue;
        hierarchyIds_.push_back(entity.GetId());
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(entity.GetId()).arg(name));
        item->setIcon(MakeEditorIcon(
            entity.HasComponent<MeshRendererComponent>() ? EditorIcon::Mesh : EditorIcon::Sprite));
        hierarchy_->addItem(item);
        if (selected && selected.GetId() == entity.GetId())
            hierarchy_->setCurrentRow(static_cast<int>(hierarchyIds_.size() - 1));
    }
    refreshingHierarchy_ = false;
}
void EditorWindow::RefreshInspector() {
    refreshingInspector_ = true;
    Entity selected = document_.GetSelected();
    selectionLabel_->setText(selected ? QStringLiteral("엔티티 #%1").arg(selected.GetId())
                                      : QStringLiteral("선택된 엔티티 없음"));
    inspectorBody_->setEnabled(static_cast<bool>(selected) && !previewActive_);
    if (selected) {
        name_->setText(selected.HasComponent<TagComponent>()
                           ? Q(selected.GetComponent<TagComponent>().tag)
                           : QString());
        const auto value = selected.HasComponent<TransformComponent>()
                               ? selected.GetComponent<TransformComponent>()
                               : TransformComponent{};
        const double degrees = 180.0 / std::numbers::pi;
        const std::array<std::array<double, 3>, 3> rows{
            {{{value.position.x, value.position.y, value.position.z}},
             {{value.rotation.x * degrees, value.rotation.y * degrees, value.rotation.z * degrees}},
             {{value.scale.x, value.scale.y, value.scale.z}}}};
        for (size_t row = 0; row < 3; ++row)
            for (size_t col = 0; col < 3; ++col)
                transform_[row][col]->setValue(rows[row][col]);
        const bool sprite = selected.HasComponent<SpriteRendererComponent>();
        spriteEnabled_->setChecked(sprite);
        const Vec4 color =
            sprite ? selected.GetComponent<SpriteRendererComponent>().color : Vec4{1, 1, 1, 1};
        const std::array<float, 4> channels{color.x, color.y, color.z, color.w};
        for (size_t i = 0; i < 4; ++i) {
            color_[i]->setValue(std::clamp(static_cast<double>(channels[i]), 0.0, 1.0));
            color_[i]->setEnabled(sprite);
        }
        const bool mesh = selected.HasComponent<MeshRendererComponent>();
        meshEnabled_->setChecked(mesh);
        const Material3D material =
            mesh ? selected.GetComponent<MeshRendererComponent>().material : Material3D{};
        metallic_->setValue(std::clamp(static_cast<double>(material.metallic), 0.0, 1.0));
        roughness_->setValue(std::clamp(static_cast<double>(material.roughness), 0.0, 1.0));
        metallic_->setEnabled(mesh);
        roughness_->setEnabled(mesh);
        const bool tiled = selected.HasComponent<TilemapComponent>();
        tilemapEnabled_->setChecked(tiled);
        const TilemapComponent map =
            tiled ? selected.GetComponent<TilemapComponent>() : TilemapComponent{};
        tilemapWidth_->setValue(static_cast<int>(map.width));
        tilemapHeight_->setValue(static_cast<int>(map.height));
        tilemapSize_->setValue(map.tileSize);
        tilemapWidth_->setEnabled(tiled);
        tilemapHeight_->setEnabled(tiled);
        tilemapSize_->setEnabled(tiled);
        const bool hasBody = selected.HasComponent<Rigidbody2DComponent>();
        bodyEnabled_->setChecked(hasBody);
        const Rigidbody2DComponent body =
            hasBody ? selected.GetComponent<Rigidbody2DComponent>() : Rigidbody2DComponent{};
        bodyType_->setCurrentIndex(body.type == BodyType::Static    ? 0
                                   : body.type == BodyType::Dynamic ? 1
                                                                    : 2);
        gravityScale_->setValue(body.gravityScale);
        bodyType_->setEnabled(hasBody);
        gravityScale_->setEnabled(hasBody);
        const bool hasCollider = selected.HasComponent<BoxCollider2DComponent>();
        colliderEnabled_->setChecked(hasCollider);
        const BoxCollider2DComponent collider =
            hasCollider ? selected.GetComponent<BoxCollider2DComponent>()
                        : BoxCollider2DComponent{};
        colliderSize_[0]->setValue(collider.size.x);
        colliderSize_[1]->setValue(collider.size.y);
        colliderOffset_[0]->setValue(collider.offset.x);
        colliderOffset_[1]->setValue(collider.offset.y);
        triggerEnabled_->setChecked(collider.isTrigger);
        for (auto* field : colliderSize_)
            field->setEnabled(hasCollider);
        for (auto* field : colliderOffset_)
            field->setEnabled(hasCollider);
        triggerEnabled_->setEnabled(hasCollider);
        const bool animated = selected.HasComponent<AnimationComponent>();
        animationEnabled_->setChecked(animated);
        const AnimationComponent animation =
            animated ? selected.GetComponent<AnimationComponent>() : AnimationComponent{};
        animationLoop_->setChecked(animation.clip.loop);
        animationDuration_->setValue(animation.clip.duration);
        animationTimeline_->setValue(
            animation.clip.duration > 0
                ? static_cast<int>(
                      std::clamp(animation.currentTime / animation.clip.duration, 0.0f, 1.0f) *
                      1000)
                : 0);
        keyframeList_->clear();
        for (const auto& key : animation.clip.keyframes)
            keyframeList_->addItem(QStringLiteral("%1 s").arg(key.time, 0, 'f', 3));
        animationLoop_->setEnabled(animated);
        animationDuration_->setEnabled(animated);
        animationTimeline_->setEnabled(animated);
        keyframeList_->setEnabled(animated);
    }
    refreshingInspector_ = false;
}
void EditorWindow::UpdateChrome() {
    const QString name = document_.GetPath().empty() ? QStringLiteral("새 장면")
                                                     : QFileInfo(Q(document_.GetPath())).fileName();
    const QString projectName =
        project_ ? Q(project_->GetName()) + QStringLiteral(" — ") : QString();
    setWindowTitle(QStringLiteral("LuxEditor — ") + projectName + name +
                   (document_.IsDirty() ? QStringLiteral(" *") : QString()));
    projectLabel_->setText(project_ ? Q(project_->GetName()) : QStringLiteral("프로젝트 없음"));
    statusBar()->showMessage(QStringLiteral("%1엔티티 %2개  |  %3")
                                 .arg(projectName)
                                 .arg(document_.GetScene().GetEntityCount())
                                 .arg(viewportTabs_->currentWidget() == canvas3D_
                                          ? QStringLiteral("3D 배치 미리보기")
                                          : QStringLiteral("2D 장면")));
    for (QAction* action : {projectSettingsAction_, cloneAction_, exportAction_, backupAction_,
                            restoreAction_, rescanAction_})
        if (action)
            action->setEnabled(project_.has_value());
}
void EditorWindow::SelectEntity(std::optional<uint32> id) {
    document_.Select(id);
    RefreshHierarchy();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
}
void EditorWindow::AddEntity() {
    if (previewActive_)
        return;
    RecordEdit(QStringLiteral("2D 엔티티 추가"), [this] {
        Entity entity = document_.CreateEntity(
            "Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<SpriteRendererComponent>();
    });
    RefreshAll();
}
void EditorWindow::AddMeshEntity() {
    if (previewActive_)
        return;
    RecordEdit(QStringLiteral("3D 메시 엔티티 추가"), [this] {
        Entity entity = document_.CreateEntity(
            "3D Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<MeshRendererComponent>();
    });
    viewportTabs_->setCurrentIndex(0);
    RefreshAll();
}
void EditorWindow::AddTilemap() {
    if (previewActive_)
        return;
    RecordEdit(QStringLiteral("타일맵 추가"), [this] {
        Entity entity = document_.CreateEntity(
            "Tilemap " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<TilemapComponent>().Resize(8, 8);
    });
    viewportTabs_->setCurrentIndex(1);
    SetTool(EditorTool::TileBrush);
    RefreshAll();
}
void EditorWindow::PaintTile(uint32 id, uint32 x, uint32 y, uint8_t tile) {
    if (previewActive_)
        return;
    auto* map = document_.GetScene().TryGetComponent<TilemapComponent>(id);
    if (!map || map->Get(x, y) == tile || !map->Set(x, y, tile))
        return;
    document_.MarkDirty();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::DeleteEntity() {
    RecordEdit(QStringLiteral("엔티티 삭제"), [this] { document_.DeleteSelected(); });
    RefreshAll();
}
void EditorWindow::OnNameChanged() {
    if (refreshingInspector_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    const std::string value = Utf8(name_->text());
    if (selected.GetComponent<TagComponent>().tag == value)
        return;
    RecordEdit(QStringLiteral("이름 변경"), [this, value] {
        document_.GetSelected().GetComponent<TagComponent>().tag = value;
        document_.MarkDirty();
    });
    RefreshHierarchy();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnTransformChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("변환 변경"), [this] {
        auto& value = document_.GetSelected().GetComponent<TransformComponent>();
        value.position = {static_cast<float>(transform_[0][0]->value()),
                          static_cast<float>(transform_[0][1]->value()),
                          static_cast<float>(transform_[0][2]->value())};
        value.rotation = {ToRadians(static_cast<float>(transform_[1][0]->value())),
                          ToRadians(static_cast<float>(transform_[1][1]->value())),
                          ToRadians(static_cast<float>(transform_[1][2]->value()))};
        value.scale = {static_cast<float>(transform_[2][0]->value()),
                       static_cast<float>(transform_[2][1]->value()),
                       static_cast<float>(transform_[2][2]->value())};
        document_.MarkDirty();
    });
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnSpriteChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("스프라이트 변경"), [this] {
        Entity selected = document_.GetSelected();
        if (spriteEnabled_->isChecked() && !selected.HasComponent<SpriteRendererComponent>())
            selected.AddComponent<SpriteRendererComponent>();
        else if (!spriteEnabled_->isChecked() && selected.HasComponent<SpriteRendererComponent>())
            selected.RemoveComponent<SpriteRendererComponent>();
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnColorChanged() {
    if (refreshingInspector_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<SpriteRendererComponent>())
        return;
    RecordEdit(QStringLiteral("색상 변경"), [this] {
        document_.GetSelected().GetComponent<SpriteRendererComponent>().color = {
            static_cast<float>(color_[0]->value()), static_cast<float>(color_[1]->value()),
            static_cast<float>(color_[2]->value()), static_cast<float>(color_[3]->value())};
        document_.MarkDirty();
    });
    canvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnMeshChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("메시 변경"), [this] {
        Entity selected = document_.GetSelected();
        if (meshEnabled_->isChecked() && !selected.HasComponent<MeshRendererComponent>())
            selected.AddComponent<MeshRendererComponent>();
        else if (!meshEnabled_->isChecked() && selected.HasComponent<MeshRendererComponent>())
            selected.RemoveComponent<MeshRendererComponent>();
        if (selected.HasComponent<MeshRendererComponent>()) {
            auto& material = selected.GetComponent<MeshRendererComponent>().material;
            material.metallic = static_cast<float>(metallic_->value());
            material.roughness = static_cast<float>(roughness_->value());
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas3D_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnTilemapChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("타일맵 변경"), [this] {
        Entity entity = document_.GetSelected();
        if (tilemapEnabled_->isChecked() && !entity.HasComponent<TilemapComponent>())
            entity.AddComponent<TilemapComponent>();
        else if (!tilemapEnabled_->isChecked() && entity.HasComponent<TilemapComponent>())
            entity.RemoveComponent<TilemapComponent>();
        if (entity.HasComponent<TilemapComponent>()) {
            auto& map = entity.GetComponent<TilemapComponent>();
            map.Resize(static_cast<uint32>(tilemapWidth_->value()),
                       static_cast<uint32>(tilemapHeight_->value()));
            map.tileSize = static_cast<float>(tilemapSize_->value());
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnPhysicsChanged() {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("2D 물리 변경"), [this] {
        Entity entity = document_.GetSelected();
        if (bodyEnabled_->isChecked() && !entity.HasComponent<Rigidbody2DComponent>())
            entity.AddComponent<Rigidbody2DComponent>();
        else if (!bodyEnabled_->isChecked() && entity.HasComponent<Rigidbody2DComponent>())
            entity.RemoveComponent<Rigidbody2DComponent>();
        if (entity.HasComponent<Rigidbody2DComponent>()) {
            auto& body = entity.GetComponent<Rigidbody2DComponent>();
            body.type = bodyType_->currentIndex() == 0   ? BodyType::Static
                        : bodyType_->currentIndex() == 1 ? BodyType::Dynamic
                                                         : BodyType::Kinematic;
            body.gravityScale = static_cast<float>(gravityScale_->value());
        }
        if (colliderEnabled_->isChecked() && !entity.HasComponent<BoxCollider2DComponent>())
            entity.AddComponent<BoxCollider2DComponent>();
        else if (!colliderEnabled_->isChecked() && entity.HasComponent<BoxCollider2DComponent>())
            entity.RemoveComponent<BoxCollider2DComponent>();
        if (entity.HasComponent<BoxCollider2DComponent>()) {
            auto& collider = entity.GetComponent<BoxCollider2DComponent>();
            collider.size = {static_cast<float>(colliderSize_[0]->value()),
                             static_cast<float>(colliderSize_[1]->value())};
            collider.offset = {static_cast<float>(colliderOffset_[0]->value()),
                               static_cast<float>(colliderOffset_[1]->value())};
            collider.isTrigger = triggerEnabled_->isChecked();
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::OnAnimationChanged() {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected())
        return;
    RecordEdit(QStringLiteral("애니메이션 변경"), [this] {
        Entity entity = document_.GetSelected();
        if (animationEnabled_->isChecked() && !entity.HasComponent<AnimationComponent>()) {
            auto& animation = entity.AddComponent<AnimationComponent>();
            animation.clip.name = "Transform";
            animation.clip.AddKeyframe({0, entity.GetComponent<TransformComponent>().position,
                                        entity.GetComponent<TransformComponent>().rotation,
                                        entity.GetComponent<TransformComponent>().scale});
        } else if (!animationEnabled_->isChecked() && entity.HasComponent<AnimationComponent>())
            entity.RemoveComponent<AnimationComponent>();
        if (entity.HasComponent<AnimationComponent>()) {
            auto& clip = entity.GetComponent<AnimationComponent>().clip;
            clip.loop = animationLoop_->isChecked();
            clip.duration = static_cast<float>(animationDuration_->value());
            if (!clip.keyframes.empty())
                clip.duration = std::max(clip.duration, clip.keyframes.back().time);
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}
void EditorWindow::AddKeyframe() {
    if (previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<AnimationComponent>())
        return;
    RecordEdit(QStringLiteral("애니메이션 키 추가"), [this] {
        Entity entity = document_.GetSelected();
        auto& animation = entity.GetComponent<AnimationComponent>();
        const auto& transform = entity.GetComponent<TransformComponent>();
        const float time = animation.clip.duration * animationTimeline_->value() / 1000.0f;
        animation.clip.AddKeyframe({time, transform.position, transform.rotation, transform.scale});
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}
void EditorWindow::RemoveKeyframe() {
    if (previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<AnimationComponent>())
        return;
    const int row = keyframeList_->currentRow();
    if (row < 0)
        return;
    RecordEdit(QStringLiteral("애니메이션 키 삭제"), [this, row] {
        auto& keys = document_.GetSelected().GetComponent<AnimationComponent>().clip.keyframes;
        if (static_cast<size_t>(row) < keys.size())
            keys.erase(keys.begin() + row);
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}
void EditorWindow::ScrubAnimation(int value) {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<AnimationComponent>())
        return;
    auto& animation = document_.GetSelected().GetComponent<AnimationComponent>();
    animation.currentTime = animation.clip.duration * value / 1000.0f;
    if (!animation.clip.keyframes.empty())
        document_.GetSelected().GetComponent<TransformComponent>() =
            animation.clip.Sample(animation.currentTime);
    document_.MarkDirty();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::EndTimelineEdit() {
    const QByteArray after = SnapshotScene();
    if (!timelineBefore_.isEmpty() && !after.isEmpty() && timelineBefore_ != after) {
        const QByteArray before = timelineBefore_;
        const auto id = SelectedId(document_);
        undo_->push(new SceneEditCommand(
            QStringLiteral("애니메이션 시간 이동"),
            [this, before, id] { RestoreScene(before, id); },
            [this, after, id] { RestoreScene(after, id); }));
    }
    timelineBefore_.clear();
}
void EditorWindow::StartPreview() {
    // 개발 중: 전체 게임 런타임 임베딩 대신 엔진 Scene의 물리·애니메이션만 실행합니다.
    if (previewActive_)
        return;
    previewBefore_ = SnapshotScene();
    if (previewBefore_.isEmpty()) {
        Log(QStringLiteral("장면 실행용 복사본을 만들지 못했습니다."), true);
        return;
    }
    previewSelected_ = SelectedId(document_);
    previewWasDirty_ = document_.IsDirty();
    previewFrame_ = 0;
    SetTool(EditorTool::Select);
    previewActive_ = true;
    previewPaused_ = false;
    for (auto* button : toolButtons_)
        button->setEnabled(false);
    for (auto* action : toolActions_)
        action->setEnabled(false);
    previewStartAction_->setEnabled(false);
    previewPauseAction_->setEnabled(true);
    previewStepAction_->setEnabled(true);
    previewStopAction_->setEnabled(true);
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    inspectorBody_->setEnabled(false);
    docks_[7]->show();
    docks_[7]->raise();
    previewTimer_.start();
    Log(QStringLiteral("장면 미리보기를 시작했습니다."));
}
void EditorWindow::StopPreview() {
    if (!previewActive_)
        return;
    previewTimer_.stop();
    previewActive_ = false;
    previewPaused_ = false;
    RestoreScene(previewBefore_, previewSelected_);
    document_.SetDirty(previewWasDirty_);
    previewBefore_.clear();
    previewSelected_.reset();
    for (auto* button : toolButtons_)
        button->setEnabled(true);
    for (auto* action : toolActions_)
        action->setEnabled(true);
    previewStartAction_->setEnabled(true);
    previewPauseAction_->setEnabled(false);
    previewStepAction_->setEnabled(false);
    previewStopAction_->setEnabled(false);
    undoAction_->setEnabled(undo_->canUndo());
    redoAction_->setEnabled(undo_->canRedo());
    RefreshAll();
    Log(QStringLiteral("장면 미리보기를 중지하고 편집 상태를 복원했습니다."));
}
void EditorWindow::TogglePreviewPause() {
    if (!previewActive_)
        return;
    previewPaused_ = !previewPaused_;
    if (previewPaused_)
        previewTimer_.stop();
    else
        previewTimer_.start();
    Log(previewPaused_ ? QStringLiteral("장면 미리보기를 일시정지했습니다.")
                       : QStringLiteral("장면 미리보기를 재개했습니다."));
}
void EditorWindow::StepPreview() {
    if (!previewActive_)
        StartPreview();
    if (!previewActive_)
        return;
    previewPaused_ = true;
    previewTimer_.stop();
    TickPreview();
}
void EditorWindow::TickPreview() {
    if (!previewActive_)
        return;
    const auto start = std::chrono::steady_clock::now();
    try {
        document_.GetScene().OnUpdate(1.0 / 60.0);
    } catch (const std::exception& error) {
        Log(QStringLiteral("장면 실행 오류: %1").arg(QString::fromUtf8(error.what())), true);
        StopPreview();
        return;
    }
    ++previewFrame_;
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto selected = SelectedId(document_);
    debugPanel_->UpdateScene(document_.GetScene(), selected.value_or(0), previewFrame_, elapsed);
    for (const auto& event : document_.GetScene().GetCollisionEvents2D())
        debugPanel_->AddLog(
            QStringLiteral("프레임 %1: #%2 ↔ #%3 %4")
                .arg(previewFrame_)
                .arg(event.entityA)
                .arg(event.entityB)
                .arg(event.isTrigger ? QStringLiteral("트리거") : QStringLiteral("충돌")),
            false);
    if (breakOnCollisionAction_->isChecked() &&
        !document_.GetScene().GetCollisionEvents2D().empty()) {
        previewPaused_ = true;
        previewTimer_.stop();
        Log(QStringLiteral("충돌 이벤트에서 실행을 일시정지했습니다."));
    }
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
}
void EditorWindow::SetTool(EditorTool tool) {
    tool_ = tool;
    if (tool == EditorTool::TileBrush || tool == EditorTool::TileErase)
        viewportTabs_->setCurrentIndex(1);
    for (size_t i = 0; i < toolButtons_.size(); ++i) {
        toolButtons_[i]->setChecked(i == static_cast<size_t>(tool));
        toolActions_[i]->setChecked(i == static_cast<size_t>(tool));
    }
    canvas_->SetTool(tool);
    canvas3D_->SetTool(tool);
    const std::array<QString, 6> descriptions{
        QStringLiteral("선택 도구\n객체를 클릭해 속성을 편집합니다."),
        QStringLiteral("이동 도구\n선택한 객체를 드래그해 배치합니다."),
        QStringLiteral("회전 도구\n선택한 객체를 드래그해 회전합니다."),
        QStringLiteral("크기 도구\n선택한 객체를 드래그해 크기를 바꿉니다."),
        QStringLiteral("타일 붓\n2D 화면에서 타일을 칠합니다."),
        QStringLiteral("타일 지우개\n2D 화면에서 타일을 지웁니다.")};
    toolHint_->setText(descriptions[static_cast<size_t>(tool)]);
    tilePalette_->setEnabled(tool == EditorTool::TileBrush);
    const char* names[] = {"선택", "이동", "회전", "크기", "타일 붓", "타일 지우개"};
    statusBar()->showMessage(QStringLiteral("%1 도구를 선택했습니다.")
                                 .arg(QString::fromUtf8(names[static_cast<int>(tool)])),
                             3000);
}
void EditorWindow::BeginViewportEdit() {
    viewportEditBefore_ = SnapshotScene();
    viewportEditSelected_ = SelectedId(document_);
}
void EditorWindow::ApplyViewportTransform(uint32 id, const TransformComponent& value) {
    if (previewActive_)
        return;
    Entity selected = document_.GetSelected();
    if (!selected || selected.GetId() != id || !selected.HasComponent<TransformComponent>())
        return;
    selected.GetComponent<TransformComponent>() = value;
    document_.MarkDirty();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}
void EditorWindow::EndViewportEdit() {
    const QByteArray after = SnapshotScene();
    const auto next = SelectedId(document_);
    if (!viewportEditBefore_.isEmpty() && !after.isEmpty() && viewportEditBefore_ != after) {
        const char* names[] = {"선택", "이동", "회전", "크기", "타일 붓", "타일 지우개"};
        const QString title =
            QString::fromUtf8(names[static_cast<int>(tool_)]) + QStringLiteral(" 도구");
        const QByteArray before = viewportEditBefore_;
        const auto previous = viewportEditSelected_;
        undo_->push(new SceneEditCommand(
            title, [this, before, previous] { RestoreScene(before, previous); },
            [this, after, next] { RestoreScene(after, next); }));
    }
    viewportEditBefore_.clear();
    viewportEditSelected_.reset();
}
QByteArray EditorWindow::SnapshotScene() {
    std::string serialized;
    if (!document_.GetScene().SerializeToString(serialized))
        return {};
    return QByteArray(serialized.data(), static_cast<qsizetype>(serialized.size()));
}
void EditorWindow::RestoreScene(const QByteArray& bytes, std::optional<uint32> selected) {
    if (bytes.isEmpty())
        return;
    const std::string serialized(bytes.constData(), static_cast<size_t>(bytes.size()));
    restoringHistory_ = true;
    if (document_.ReplaceSceneFromData(serialized, selected))
        RefreshAll();
    restoringHistory_ = false;
}
void EditorWindow::RecordEdit(const QString& name, const std::function<void()>& edit) {
    if (previewActive_)
        return;
    if (restoringHistory_) {
        edit();
        return;
    }
    const auto before = SnapshotScene();
    const auto previous = SelectedId(document_);
    try {
        edit();
    } catch (...) {
        if (!before.isEmpty())
            RestoreScene(before, previous);
        return;
    }
    const auto after = SnapshotScene();
    if (!before.isEmpty() && after.isEmpty()) {
        RestoreScene(before, previous);
        return;
    }
    const auto next = SelectedId(document_);
    if (!before.isEmpty() && !after.isEmpty() && before != after)
        undo_->push(new SceneEditCommand(
            name, [this, before, previous] { RestoreScene(before, previous); },
            [this, after, next] { RestoreScene(after, next); }));
}
void EditorWindow::CopyEntity() {
    Entity selected = document_.GetSelected();
    if (!selected)
        return;
    QJsonObject object;
    object.insert(QStringLiteral("name"), Q(selected.GetComponent<TagComponent>().tag));
    const auto& t = selected.GetComponent<TransformComponent>();
    object.insert(QStringLiteral("position"), QJsonArray{t.position.x, t.position.y, t.position.z});
    object.insert(QStringLiteral("rotation"), QJsonArray{t.rotation.x, t.rotation.y, t.rotation.z});
    object.insert(QStringLiteral("scale"), QJsonArray{t.scale.x, t.scale.y, t.scale.z});
    if (selected.HasComponent<SpriteRendererComponent>()) {
        const auto& c = selected.GetComponent<SpriteRendererComponent>().color;
        object.insert(QStringLiteral("sprite"), QJsonArray{c.x, c.y, c.z, c.w});
    }
    if (selected.HasComponent<MeshRendererComponent>()) {
        const auto& m = selected.GetComponent<MeshRendererComponent>().material;
        const auto& c = m.albedoColor;
        object.insert(QStringLiteral("mesh"),
                      QJsonArray{m.metallic, m.roughness, c.x, c.y, c.z, c.w});
    }
    if (selected.HasComponent<CameraComponent>()) {
        const auto& cam = selected.GetComponent<CameraComponent>();
        QJsonObject camObj;
        camObj.insert(QStringLiteral("primary"), cam.isPrimary);
        camObj.insert(QStringLiteral("perspective"), cam.isPerspective);
        camObj.insert(QStringLiteral("fov"), cam.fov);
        camObj.insert(QStringLiteral("orthoSize"), cam.orthoSize);
        object.insert(QStringLiteral("camera"), camObj);
    }
    if (selected.HasComponent<TilemapComponent>()) {
        const auto& map = selected.GetComponent<TilemapComponent>();
        QJsonObject mapObj;
        mapObj.insert(QStringLiteral("width"), static_cast<int>(map.width));
        mapObj.insert(QStringLiteral("height"), static_cast<int>(map.height));
        mapObj.insert(QStringLiteral("tileSize"), map.tileSize);
        QJsonArray tilesArr;
        for (uint8_t tile : map.tiles)
            tilesArr.append(static_cast<int>(tile));
        mapObj.insert(QStringLiteral("tiles"), tilesArr);
        object.insert(QStringLiteral("tilemap"), mapObj);
    }
    if (selected.HasComponent<Rigidbody2DComponent>()) {
        const auto& rb = selected.GetComponent<Rigidbody2DComponent>();
        QJsonObject rbObj;
        rbObj.insert(QStringLiteral("type"), static_cast<int>(rb.type));
        rbObj.insert(QStringLiteral("velocity"), QJsonArray{rb.velocity.x, rb.velocity.y});
        rbObj.insert(QStringLiteral("mass"), rb.mass);
        rbObj.insert(QStringLiteral("gravityScale"), rb.gravityScale);
        object.insert(QStringLiteral("rigidbody2d"), rbObj);
    }
    if (selected.HasComponent<BoxCollider2DComponent>()) {
        const auto& col = selected.GetComponent<BoxCollider2DComponent>();
        QJsonObject colObj;
        colObj.insert(QStringLiteral("size"), QJsonArray{col.size.x, col.size.y});
        colObj.insert(QStringLiteral("offset"), QJsonArray{col.offset.x, col.offset.y});
        colObj.insert(QStringLiteral("trigger"), col.isTrigger);
        object.insert(QStringLiteral("collider2d"), colObj);
    }
    if (selected.HasComponent<Rigidbody3DComponent>()) {
        const auto& rb = selected.GetComponent<Rigidbody3DComponent>();
        QJsonObject rbObj;
        rbObj.insert(QStringLiteral("type"), static_cast<int>(rb.type));
        rbObj.insert(QStringLiteral("velocity"),
                     QJsonArray{rb.velocity.x, rb.velocity.y, rb.velocity.z});
        rbObj.insert(QStringLiteral("mass"), rb.mass);
        rbObj.insert(QStringLiteral("gravityScale"), rb.gravityScale);
        object.insert(QStringLiteral("rigidbody3d"), rbObj);
    }
    if (selected.HasComponent<BoxCollider3DComponent>()) {
        const auto& col = selected.GetComponent<BoxCollider3DComponent>();
        QJsonObject colObj;
        colObj.insert(QStringLiteral("size"), QJsonArray{col.size.x, col.size.y, col.size.z});
        colObj.insert(QStringLiteral("offset"), QJsonArray{col.offset.x, col.offset.y, col.offset.z});
        colObj.insert(QStringLiteral("trigger"), col.isTrigger);
        object.insert(QStringLiteral("collider3d"), colObj);
    }
    if (selected.HasComponent<AnimationComponent>()) {
        const auto& anim = selected.GetComponent<AnimationComponent>();
        QJsonObject animObj;
        animObj.insert(QStringLiteral("name"), Q(anim.clip.name));
        animObj.insert(QStringLiteral("duration"), anim.clip.duration);
        animObj.insert(QStringLiteral("loop"), anim.clip.loop);
        animObj.insert(QStringLiteral("playing"), anim.playing);
        QJsonArray keysArr;
        for (const auto& k : anim.clip.keyframes) {
            QJsonObject kObj;
            kObj.insert(QStringLiteral("time"), k.time);
            kObj.insert(QStringLiteral("position"),
                        QJsonArray{k.position.x, k.position.y, k.position.z});
            kObj.insert(QStringLiteral("rotation"),
                        QJsonArray{k.rotation.x, k.rotation.y, k.rotation.z});
            kObj.insert(QStringLiteral("scale"), QJsonArray{k.scale.x, k.scale.y, k.scale.z});
            keysArr.append(kObj);
        }
        animObj.insert(QStringLiteral("keyframes"), keysArr);
        object.insert(QStringLiteral("animation"), animObj);
    }
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-lux-entity"),
                  QJsonDocument(object).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
    Log(QStringLiteral("엔티티를 복사했습니다."));
}
namespace {
constexpr qsizetype kMaxClipboardJsonBytes = 1024 * 1024;
constexpr float kMaxClipboardCoord = 100000.0f;
constexpr float kMaxClipboardMass = 1000000.0f;
constexpr float kMaxClipboardGravityScale = 10000.0f;
constexpr float kMaxClipboardDuration = 86400.0f;

bool IsStrictValidUtf8(const QByteArray& bytes) {
    const auto* s = reinterpret_cast<const unsigned char*>(bytes.constData());
    const size_t len = static_cast<size_t>(bytes.size());
    size_t i = 0;
    while (i < len) {
        const unsigned char c = s[i];
        if (c == 0x00)
            return false;
        if (c <= 0x7F) {
            ++i;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80)
                return false;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80)
                return false;
            if (c == 0xE0 && s[i + 1] < 0xA0)
                return false; // Overlong
            if (c == 0xED && s[i + 1] >= 0xA0)
                return false; // UTF-16 surrogate
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 ||
                (s[i + 3] & 0xC0) != 0x80)
                return false;
            if (c == 0xF0 && s[i + 1] < 0x90)
                return false; // Overlong
            if (c == 0xF4 && s[i + 1] > 0x8F)
                return false; // > U+10FFFF
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool ParseFiniteFloat(const QJsonValue& val, float& out, float minVal = -kMaxClipboardCoord,
                      float maxVal = kMaxClipboardCoord) {
    if (!val.isDouble())
        return false;
    const double d = val.toDouble();
    if (!std::isfinite(d))
        return false;
    const float f = static_cast<float>(d);
    if (!std::isfinite(f) || f < minVal || f > maxVal)
        return false;
    out = f;
    return true;
}

bool ParseExactInt(const QJsonValue& val, int& out, int minVal, int maxVal) {
    if (!val.isDouble())
        return false;
    const double d = val.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < static_cast<double>(minVal) ||
        d > static_cast<double>(maxVal))
        return false;
    out = static_cast<int>(d);
    return true;
}

bool ParseVec2(const QJsonValue& val, Vec2& out, float minVal = -kMaxClipboardCoord,
               float maxVal = kMaxClipboardCoord) {
    if (!val.isArray())
        return false;
    const auto arr = val.toArray();
    if (arr.size() != 2)
        return false;
    return ParseFiniteFloat(arr[0], out.x, minVal, maxVal) &&
           ParseFiniteFloat(arr[1], out.y, minVal, maxVal);
}

bool ParseVec3(const QJsonValue& val, Vec3& out, float minVal = -kMaxClipboardCoord,
               float maxVal = kMaxClipboardCoord) {
    if (!val.isArray())
        return false;
    const auto arr = val.toArray();
    if (arr.size() != 3)
        return false;
    return ParseFiniteFloat(arr[0], out.x, minVal, maxVal) &&
           ParseFiniteFloat(arr[1], out.y, minVal, maxVal) &&
           ParseFiniteFloat(arr[2], out.z, minVal, maxVal);
}

bool ParseVec4(const QJsonValue& val, Vec4& out, float minVal = -kMaxClipboardCoord,
               float maxVal = kMaxClipboardCoord) {
    if (!val.isArray())
        return false;
    const auto arr = val.toArray();
    if (arr.size() != 4)
        return false;
    return ParseFiniteFloat(arr[0], out.x, minVal, maxVal) &&
           ParseFiniteFloat(arr[1], out.y, minVal, maxVal) &&
           ParseFiniteFloat(arr[2], out.z, minVal, maxVal) &&
           ParseFiniteFloat(arr[3], out.w, minVal, maxVal);
}

struct ValidatedClipboardEntity {
    QString name;
    TransformComponent transform;
    std::optional<SpriteRendererComponent> sprite;
    std::optional<MeshRendererComponent> mesh;
    std::optional<CameraComponent> camera;
    std::optional<TilemapComponent> tilemap;
    std::optional<Rigidbody2DComponent> rigidbody2d;
    std::optional<BoxCollider2DComponent> collider2d;
    std::optional<Rigidbody3DComponent> rigidbody3d;
    std::optional<BoxCollider3DComponent> collider3d;
    std::optional<AnimationComponent> animation;
};

bool ValidateClipboardJson(const QJsonObject& object, ValidatedClipboardEntity& parsed,
                           QString& errorMsg) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString& key = it.key();
        if (key != QStringLiteral("name") && key != QStringLiteral("position") &&
            key != QStringLiteral("rotation") && key != QStringLiteral("scale") &&
            key != QStringLiteral("sprite") && key != QStringLiteral("mesh") &&
            key != QStringLiteral("camera") && key != QStringLiteral("tilemap") &&
            key != QStringLiteral("rigidbody2d") && key != QStringLiteral("collider2d") &&
            key != QStringLiteral("rigidbody3d") && key != QStringLiteral("collider3d") &&
            key != QStringLiteral("animation")) {
            errorMsg = QStringLiteral("알 수 없는 클립보드 필드가 포함되어 있습니다: %1").arg(key);
            return false;
        }
    }

    const QJsonValue nameVal = object.value(QStringLiteral("name"));
    if (!nameVal.isString()) {
        errorMsg = QStringLiteral("엔티티 이름(name) 필드가 누락되었거나 문자열이 아닙니다.");
        return false;
    }
    parsed.name = nameVal.toString().trimmed();
    if (parsed.name.isEmpty() || parsed.name.size() > 256 ||
        parsed.name.contains(QChar::ReplacementCharacter)) {
        errorMsg = QStringLiteral("엔티티 이름이 비어 있거나 허용 길이를 초과했습니다.");
        return false;
    }

    if (!object.contains(QStringLiteral("position")) ||
        !object.contains(QStringLiteral("rotation")) ||
        !object.contains(QStringLiteral("scale"))) {
        errorMsg = QStringLiteral("필수 Transform 필드(position, rotation, scale)가 누락되었습니다.");
        return false;
    }
    if (!ParseVec3(object.value(QStringLiteral("position")), parsed.transform.position) ||
        !ParseVec3(object.value(QStringLiteral("rotation")), parsed.transform.rotation) ||
        !ParseVec3(object.value(QStringLiteral("scale")), parsed.transform.scale)) {
        errorMsg = QStringLiteral("Transform 수치가 유효하지 않거나 허용 범위를 벗어났습니다.");
        return false;
    }
    if (std::abs(parsed.transform.scale.x) < 1e-5f || std::abs(parsed.transform.scale.y) < 1e-5f ||
        std::abs(parsed.transform.scale.z) < 1e-5f) {
        errorMsg = QStringLiteral("Transform 크기는 0이 될 수 없습니다.");
        return false;
    }

    if (object.contains(QStringLiteral("sprite"))) {
        SpriteRendererComponent comp;
        if (!ParseVec4(object.value(QStringLiteral("sprite")), comp.color, 0.0f, 100.0f)) {
            errorMsg = QStringLiteral("스프라이트 색상 데이터가 유효하지 않습니다.");
            return false;
        }
        parsed.sprite = comp;
    }

    if (object.contains(QStringLiteral("mesh"))) {
        const QJsonValue meshVal = object.value(QStringLiteral("mesh"));
        if (!meshVal.isArray()) {
            errorMsg = QStringLiteral("메시 데이터가 배열 형식이 아닙니다.");
            return false;
        }
        const auto meshArr = meshVal.toArray();
        if (meshArr.size() != 2 && meshArr.size() != 6) {
            errorMsg = QStringLiteral("메시 배열 길이가 올바르지 않습니다.");
            return false;
        }
        MeshRendererComponent comp;
        if (!ParseFiniteFloat(meshArr[0], comp.material.metallic, 0.0f, 1.0f) ||
            !ParseFiniteFloat(meshArr[1], comp.material.roughness, 0.0f, 1.0f)) {
            errorMsg = QStringLiteral("메시 재질 수치가 허용 범위(0~1)를 벗어났습니다.");
            return false;
        }
        if (meshArr.size() == 6) {
            if (!ParseFiniteFloat(meshArr[2], comp.material.albedoColor.x, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[3], comp.material.albedoColor.y, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[4], comp.material.albedoColor.z, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[5], comp.material.albedoColor.w, 0.0f, 100.0f)) {
                errorMsg = QStringLiteral("메시 알베도 색상이 유효하지 않습니다.");
                return false;
            }
        }
        parsed.mesh = comp;
    }

    if (object.contains(QStringLiteral("camera"))) {
        const QJsonValue camVal = object.value(QStringLiteral("camera"));
        if (!camVal.isObject()) {
            errorMsg = QStringLiteral("카메라 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto camObj = camVal.toObject();
        const QJsonValue primVal = camObj.value(QStringLiteral("primary"));
        const QJsonValue perspVal = camObj.value(QStringLiteral("perspective"));
        CameraComponent comp;
        if (!primVal.isBool() || !perspVal.isBool() ||
            !ParseFiniteFloat(camObj.value(QStringLiteral("fov")), comp.fov, 0.0f, 180.0f) ||
            comp.fov <= 0.0f || comp.fov >= 180.0f ||
            !ParseFiniteFloat(camObj.value(QStringLiteral("orthoSize")), comp.orthoSize, 0.0f,
                              kMaxClipboardCoord) ||
            comp.orthoSize <= 0.0f) {
            errorMsg = QStringLiteral("카메라 FOV 또는 직교 크기가 유효 범위를 벗어났습니다.");
            return false;
        }
        comp.isPrimary = primVal.toBool();
        comp.isPerspective = perspVal.toBool();
        parsed.camera = comp;
    }

    if (object.contains(QStringLiteral("tilemap"))) {
        const QJsonValue mapVal = object.value(QStringLiteral("tilemap"));
        if (!mapVal.isObject()) {
            errorMsg = QStringLiteral("타일맵 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto mapObj = mapVal.toObject();
        int w = 0, h = 0;
        float tileSize = 0.0f;
        if (!ParseExactInt(mapObj.value(QStringLiteral("width")), w, 1, 256) ||
            !ParseExactInt(mapObj.value(QStringLiteral("height")), h, 1, 256) ||
            !ParseFiniteFloat(mapObj.value(QStringLiteral("tileSize")), tileSize, 0.0f, 64.0f) ||
            tileSize <= 0.0f) {
            errorMsg = QStringLiteral("타일맵 크기(최대 256x256) 또는 타일 크기가 유효하지 않습니다.");
            return false;
        }
        const QJsonValue tilesVal = mapObj.value(QStringLiteral("tiles"));
        if (!tilesVal.isArray()) {
            errorMsg = QStringLiteral("타일맵 tiles 배열이 누락되었습니다.");
            return false;
        }
        const auto tilesArr = tilesVal.toArray();
        if (tilesArr.size() != static_cast<qsizetype>(w * h)) {
            errorMsg = QStringLiteral("타일맵 셀 개수가 가로x세로 크기와 일치하지 않습니다.");
            return false;
        }
        TilemapComponent comp;
        if (!comp.Resize(static_cast<uint32>(w), static_cast<uint32>(h))) {
            errorMsg = QStringLiteral("타일맵 할당에 실패했습니다.");
            return false;
        }
        comp.tileSize = tileSize;
        for (qsizetype i = 0; i < tilesArr.size(); ++i) {
            int tv = -1;
            if (!ParseExactInt(tilesArr[i], tv, 0, 5)) {
                errorMsg = QStringLiteral("타일맵 셀 값이 허용 범위(0~5)를 벗어났습니다.");
                return false;
            }
            comp.tiles[static_cast<size_t>(i)] = static_cast<uint8_t>(tv);
        }
        parsed.tilemap = std::move(comp);
    }

    if (object.contains(QStringLiteral("rigidbody2d"))) {
        const QJsonValue rbVal = object.value(QStringLiteral("rigidbody2d"));
        if (!rbVal.isObject()) {
            errorMsg = QStringLiteral("2D 강체 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto rbObj = rbVal.toObject();
        int type = -1;
        Rigidbody2DComponent comp;
        if (!ParseExactInt(rbObj.value(QStringLiteral("type")), type, 0, 2) ||
            !ParseVec2(rbObj.value(QStringLiteral("velocity")), comp.velocity) ||
            !ParseFiniteFloat(rbObj.value(QStringLiteral("mass")), comp.mass, 0.0f,
                              kMaxClipboardMass) ||
            comp.mass <= 0.0f ||
            !ParseFiniteFloat(rbObj.value(QStringLiteral("gravityScale")), comp.gravityScale,
                              -kMaxClipboardGravityScale, kMaxClipboardGravityScale)) {
            errorMsg = QStringLiteral("2D 강체 속성(질량, 속도, 중력 배율)이 유효하지 않습니다.");
            return false;
        }
        comp.type = static_cast<BodyType>(type);
        parsed.rigidbody2d = comp;
    }

    if (object.contains(QStringLiteral("collider2d"))) {
        const QJsonValue colVal = object.value(QStringLiteral("collider2d"));
        if (!colVal.isObject()) {
            errorMsg = QStringLiteral("2D 콜라이더 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto colObj = colVal.toObject();
        const QJsonValue trigVal = colObj.value(QStringLiteral("trigger"));
        BoxCollider2DComponent comp;
        if (!trigVal.isBool() ||
            !ParseVec2(colObj.value(QStringLiteral("size")), comp.size, 0.0f, kMaxClipboardCoord) ||
            comp.size.x <= 0.0f || comp.size.y <= 0.0f ||
            !ParseVec2(colObj.value(QStringLiteral("offset")), comp.offset)) {
            errorMsg = QStringLiteral("2D 콜라이더 크기 또는 오프셋이 유효하지 않습니다.");
            return false;
        }
        comp.isTrigger = trigVal.toBool();
        parsed.collider2d = comp;
    }

    if (object.contains(QStringLiteral("rigidbody3d"))) {
        const QJsonValue rbVal = object.value(QStringLiteral("rigidbody3d"));
        if (!rbVal.isObject()) {
            errorMsg = QStringLiteral("3D 강체 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto rbObj = rbVal.toObject();
        int type = -1;
        Rigidbody3DComponent comp;
        if (!ParseExactInt(rbObj.value(QStringLiteral("type")), type, 0, 2) ||
            !ParseVec3(rbObj.value(QStringLiteral("velocity")), comp.velocity) ||
            !ParseFiniteFloat(rbObj.value(QStringLiteral("mass")), comp.mass, 0.0f,
                              kMaxClipboardMass) ||
            comp.mass <= 0.0f ||
            !ParseFiniteFloat(rbObj.value(QStringLiteral("gravityScale")), comp.gravityScale,
                              -kMaxClipboardGravityScale, kMaxClipboardGravityScale)) {
            errorMsg = QStringLiteral("3D 강체 속성(질량, 속도, 중력 배율)이 유효하지 않습니다.");
            return false;
        }
        comp.type = static_cast<BodyType>(type);
        parsed.rigidbody3d = comp;
    }

    if (object.contains(QStringLiteral("collider3d"))) {
        const QJsonValue colVal = object.value(QStringLiteral("collider3d"));
        if (!colVal.isObject()) {
            errorMsg = QStringLiteral("3D 콜라이더 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto colObj = colVal.toObject();
        const QJsonValue trigVal = colObj.value(QStringLiteral("trigger"));
        BoxCollider3DComponent comp;
        if (!trigVal.isBool() ||
            !ParseVec3(colObj.value(QStringLiteral("size")), comp.size, 0.0f, kMaxClipboardCoord) ||
            comp.size.x <= 0.0f || comp.size.y <= 0.0f || comp.size.z <= 0.0f ||
            !ParseVec3(colObj.value(QStringLiteral("offset")), comp.offset)) {
            errorMsg = QStringLiteral("3D 콜라이더 크기 또는 오프셋이 유효하지 않습니다.");
            return false;
        }
        comp.isTrigger = trigVal.toBool();
        parsed.collider3d = comp;
    }

    if (object.contains(QStringLiteral("animation"))) {
        const QJsonValue animVal = object.value(QStringLiteral("animation"));
        if (!animVal.isObject()) {
            errorMsg = QStringLiteral("애니메이션 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto animObj = animVal.toObject();
        const QJsonValue clipNameVal = animObj.value(QStringLiteral("name"));
        const QJsonValue loopVal = animObj.value(QStringLiteral("loop"));
        const QJsonValue playingVal = animObj.value(QStringLiteral("playing"));
        const QJsonValue keysVal = animObj.value(QStringLiteral("keyframes"));
        AnimationComponent comp;
        if (!clipNameVal.isString() || !loopVal.isBool() || !playingVal.isBool() ||
            !keysVal.isArray() ||
            !ParseFiniteFloat(animObj.value(QStringLiteral("duration")), comp.clip.duration, 0.0f,
                              kMaxClipboardDuration) ||
            comp.clip.duration <= 0.0f) {
            errorMsg = QStringLiteral("애니메이션 클립 속성 또는 재생 시간이 유효하지 않습니다.");
            return false;
        }
        const QString clipName = clipNameVal.toString().trimmed();
        if (clipName.isEmpty() || clipName.size() > 256 ||
            clipName.contains(QChar::ReplacementCharacter)) {
            errorMsg = QStringLiteral("애니메이션 클립 이름이 유효하지 않습니다.");
            return false;
        }
        const auto keysArr = keysVal.toArray();
        if (keysArr.size() > 4096) {
            errorMsg = QStringLiteral("애니메이션 키프레임 수가 최대 허용치(4096개)를 초과했습니다.");
            return false;
        }
        comp.clip.name = clipName.toUtf8().toStdString();
        comp.clip.loop = loopVal.toBool();
        comp.playing = playingVal.toBool();
        for (const auto& kv : keysArr) {
            if (!kv.isObject()) {
                errorMsg = QStringLiteral("애니메이션 키프레임이 객체 형식이 아닙니다.");
                return false;
            }
            const auto kObj = kv.toObject();
            Keyframe key;
            if (!ParseFiniteFloat(kObj.value(QStringLiteral("time")), key.time, 0.0f,
                                  kMaxClipboardDuration) ||
                key.time < 0.0f ||
                !ParseVec3(kObj.value(QStringLiteral("position")), key.position) ||
                !ParseVec3(kObj.value(QStringLiteral("rotation")), key.rotation) ||
                !ParseVec3(kObj.value(QStringLiteral("scale")), key.scale) ||
                std::abs(key.scale.x) < 1e-5f || std::abs(key.scale.y) < 1e-5f ||
                std::abs(key.scale.z) < 1e-5f) {
                errorMsg = QStringLiteral("애니메이션 키프레임 수치가 유효하지 않습니다.");
                return false;
            }
            comp.clip.AddKeyframe(key);
        }
        parsed.animation = std::move(comp);
    }
    return true;
}
} // namespace

void EditorWindow::PasteEntity() {
    const auto* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QStringLiteral("application/x-lux-entity")))
        return;
    const QByteArray rawBytes = mime->data(QStringLiteral("application/x-lux-entity"));
    if (rawBytes.isEmpty() || rawBytes.size() > kMaxClipboardJsonBytes) {
        Log(QStringLiteral("붙여넣기 실패: 클립보드 데이터가 비어 있거나 허용 크기(1MB)를 초과했습니다."));
        return;
    }
    if (!IsStrictValidUtf8(rawBytes)) {
        Log(QStringLiteral("붙여넣기 실패: 클립보드 데이터에 잘못된 UTF-8 인코딩이 포함되어 있습니다."));
        return;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(rawBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        Log(QStringLiteral("붙여넣기 실패: 클립보드 JSON 구문이 손상되었습니다."));
        return;
    }
    ValidatedClipboardEntity validated;
    QString validationError;
    if (!ValidateClipboardJson(doc.object(), validated, validationError)) {
        Log(QStringLiteral("붙여넣기 실패: %1").arg(validationError));
        return;
    }

    RecordEdit(QStringLiteral("엔티티 붙여넣기"), [this, validated = std::move(validated)] {
        Entity entity = document_.CreateEntity(Utf8(validated.name + QStringLiteral(" 복사본")));
        try {
            entity.GetComponent<TransformComponent>() = validated.transform;
            if (validated.sprite)
                entity.AddComponent<SpriteRendererComponent>(*validated.sprite);
            if (validated.mesh)
                entity.AddComponent<MeshRendererComponent>(*validated.mesh);
            if (validated.camera)
                entity.AddComponent<CameraComponent>(*validated.camera);
            if (validated.tilemap)
                entity.AddComponent<TilemapComponent>(*validated.tilemap);
            if (validated.rigidbody2d)
                entity.AddComponent<Rigidbody2DComponent>(*validated.rigidbody2d);
            if (validated.collider2d)
                entity.AddComponent<BoxCollider2DComponent>(*validated.collider2d);
            if (validated.rigidbody3d)
                entity.AddComponent<Rigidbody3DComponent>(*validated.rigidbody3d);
            if (validated.collider3d)
                entity.AddComponent<BoxCollider3DComponent>(*validated.collider3d);
            if (validated.animation)
                entity.AddComponent<AnimationComponent>(*validated.animation);

            std::string verifySerialized;
            if (!document_.GetScene().SerializeToString(verifySerialized)) {
                document_.GetScene().DestroyEntity(entity);
                throw std::runtime_error("Clipboard entity failed scene serialization invariant");
            }
        } catch (...) {
            if (entity.IsValid())
                document_.GetScene().DestroyEntity(entity);
            throw;
        }
    });
    RefreshAll();
}
} // namespace LuxEngine
