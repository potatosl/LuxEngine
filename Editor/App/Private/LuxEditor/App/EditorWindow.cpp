#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Debug/DebugPanel.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEditor/Project/ProjectDialog.hpp>
#include <LuxEditor/Scene/SceneHistory.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace LuxEditor {
namespace {
LuxUI::String Q(const std::string& value) {
    return LuxUI::String::fromUtf8(value);
}

LuxUI::String Q(const std::filesystem::path& value) {
    return Q(PathUtf8(value));
}

std::string Utf8(const LuxUI::String& value) {
    return value.toUtf8().toStdString();
}

std::optional<LuxEngine::uint32> SelectedId(EditorDocument& document) {
    const LuxEngine::Entity entity = document.GetSelected();
    return entity ? std::optional<LuxEngine::uint32>(entity.GetId()) : std::nullopt;
}
} // namespace

EditorWindow::EditorWindow(LuxUI::Widget* parent)
    : LuxUI::MainWindow(parent),
      recentProjects_(PathFromUtf8(Utf8(LuxUI::StandardPaths::writableLocation(
                          LuxUI::StandardPaths::AppDataLocation))) /
                      "recent-projects.txt"),
      undo_(new LuxUI::UndoStack(this)) {
    undo_->setUndoLimit(128);
    setWindowTitle(LUX_UI_TEXT("LuxEditor"));
    setMinimumSize(960, 600);
    resize(1600, 960);
    setAcceptDrops(true);
    recentProjects_.Load();
    connect(&autosaveTimer_, &LuxUI::Timer::timeout, this, [this] { AutoSaveScene(); });
    previewTimer_.setInterval(16);
    connect(&previewTimer_, &LuxUI::Timer::timeout, this, [this] { TickPreview(); });
    connect(undo_, &LuxUI::UndoStack::indexChanged, this, [this] { UpdateChrome(); });
    sceneLoadTimer_.setInterval(25);
    connect(&sceneLoadTimer_, &LuxUI::Timer::timeout, this, [this] { PollSceneLoads(); });
    sceneStreamingTimer_.setInterval(100);
    connect(&sceneStreamingTimer_, &LuxUI::Timer::timeout, this, [this] { TickSceneStreaming(); });
    sceneStreamingTimer_.start();
    BuildLayout();
    BuildMenus();
    BuildDevelopmentWorkspace();
    BuildModelingWorkspace();
    const LuxUI::Settings settings;
    const auto previous = PathFromUtf8(Utf8(settings.value(LUX_UI_TEXT("lastProject")).toString()));
    if (!previous.empty() && std::filesystem::is_regular_file(previous))
        OpenProjectFile(previous,
                        PathFromUtf8(Utf8(settings.value(LUX_UI_TEXT("lastScene")).toString())));
    else {
        const std::string scene = Utf8(settings.value(LUX_UI_TEXT("lastScene")).toString());
        if (!scene.empty() && std::filesystem::is_regular_file(PathFromUtf8(scene)))
            document_.Open(scene);
    }
    const auto active = document_.GetActiveTabId();
    const auto activeProject = project_;

    struct Recovery {
        EditorDocument::TabId tab = 0;
        std::string path;
        std::filesystem::path autosave;
        EditorProject context;
    };

    std::vector<Recovery> recoveries;
    const auto savedDocuments = settings.value(LUX_UI_TEXT("openSceneDocuments")).toByteArray();
    if (savedDocuments.size() <= 1024 * 1024) {
        const auto records = LuxUI::JsonDocument::fromJson(savedDocuments).array();
        for (qsizetype index = 0; index < std::min<qsizetype>(records.size(), 128); ++index) {
            const auto record = records[index].toObject();
            const auto path = Utf8(record.value(LUX_UI_TEXT("path")).toString());
            if (path.empty() || document_.FindTabByPath(path))
                continue;
            std::optional<EditorProject> context;
            const auto projectPath = Utf8(record.value(LUX_UI_TEXT("project")).toString());
            std::string message;
            if (!projectPath.empty())
                context = EditorProject::OpenForRecovery(PathFromUtf8(projectPath), message);
            if (context && !context->ContainsScene(PathFromUtf8(path)))
                context.reset();
            const auto id = document_.OpenTab(path);
            if (id) {
                sceneProjects_[id] = context;
                cleanScenes_[id] = SnapshotScene(id);
            }
            if (context && (!id || context->HasNewerAutosave(PathFromUtf8(path)))) {
                const auto autosave = context->GetAutosaveFile(PathFromUtf8(path));
                LuxEngine::Scene validation;
                if (!autosave.empty() && validation.Deserialize(PathUtf8(autosave)))
                    recoveries.push_back({id, path, autosave, *context});
            }
        }
    }
    if (!recoveries.empty() &&
        LuxUI::MessageBox::question(
            this, LUX_UI_TEXT("씬 자동 저장 복구"),
            LUX_UI_TEXT("자동 저장된 씬 %1개를 복구하시겠습니까?").arg(recoveries.size()),
            LuxUI::MessageBox::Yes | LuxUI::MessageBox::No,
            LuxUI::MessageBox::Yes) == LuxUI::MessageBox::Yes) {
        for (const auto& recovery : recoveries) {
            const auto id = recovery.tab ? recovery.tab : document_.NewTab();
            document_.ActivateTab(id);
            if (document_.Recover(recovery.path, PathUtf8(recovery.autosave))) {
                sceneProjects_[id] = recovery.context;
                RememberSceneBaseline(false);
                undo_->resetClean();
            } else if (!recovery.tab)
                document_.CloseTab(id, true);
        }
    }
    document_.ActivateTab(active);
    project_ = activeProject;
    RememberSceneBaseline(!document_.IsDirty());
    sceneProjects_[document_.GetActiveTabId()] = project_;
    ApplyProjectPreferences();
    RefreshAll();
}

EditorWindow::~EditorWindow() {
    CancelViewportEdit();
    StopAnimationPlayback();
    developmentTimer_.stop();
    autosaveTimer_.stop();
    previewTimer_.stop();
    sceneLoadTimer_.stop();
    sceneStreamingTimer_.stop();
    StopPreview();
    StopSceneWindow();
    CancelBuild();
    pendingSceneLoads_.clear();
    undo_->clear();
    removeDockWidget(animationDock_);
    delete animationDock_;
    animationDock_ = nullptr;
    if (modelingDock_) {
        modelingDock_->blockSignals(true);
        removeDockWidget(modelingDock_);
        delete modelingDock_;
        modelingDock_ = nullptr;
    }
    delete takeCentralWidget();
    for (LuxUI::DockWidget* dock : docks_) {
        removeDockWidget(dock);
        delete dock;
    }
}

void EditorWindow::BuildMenus() {
    auto add = [this](LuxUI::Menu* menu, const LuxUI::String& title, const LuxUI::String& id,
                      const LuxUI::KeySequence& fallback, const std::function<void()>& action) {
        LuxUI::Action* item = menu->addAction(title);
        item->setObjectName(id);
        item->setShortcut(
            LuxUI::KeySequence(LuxUI::Settings()
                                   .value(LUX_UI_TEXT("shortcuts/") + id, fallback.toString())
                                   .toString()));
        connect(item, &LuxUI::Action::triggered, this, [action] { action(); });
        shortcutActions_.push_back(item);
        return item;
    };
    LuxUI::Menu* file = menuBar()->addMenu(LUX_UI_TEXT("파일"));
    add(file, LUX_UI_TEXT("새 프로젝트..."), LUX_UI_TEXT("newProject"), {},
        [this] { NewProject(); });
    add(file, LUX_UI_TEXT("프로젝트 열기..."), LUX_UI_TEXT("openProject"), {},
        [this] { OpenProjectDialog(); });
    recentMenu_ = file->addMenu(LUX_UI_TEXT("최근 프로젝트"));
    projectSettingsAction_ =
        add(file, LUX_UI_TEXT("프로젝트 설정..."), LUX_UI_TEXT("projectSettings"), {},
            [this] { EditProjectSettings(); });
    cloneAction_ = add(file, LUX_UI_TEXT("프로젝트 복제..."), LUX_UI_TEXT("cloneProject"), {},
                       [this] { CloneCurrentProject(); });
    add(file, LUX_UI_TEXT("프로젝트 가져오기..."), LUX_UI_TEXT("importProject"), {},
        [this] { ImportProjectDialog(); });
    exportAction_ = add(file, LUX_UI_TEXT("프로젝트 내보내기..."), LUX_UI_TEXT("exportProject"), {},
                        [this] { ExportCurrentProject(); });
    backupAction_ = add(file, LUX_UI_TEXT("프로젝트 백업"), LUX_UI_TEXT("backupProject"), {},
                        [this] { BackupCurrentProject(); });
    restoreAction_ = add(file, LUX_UI_TEXT("프로젝트 백업 복구..."), LUX_UI_TEXT("restoreProject"),
                         {}, [this] { RestoreCurrentProject(); });
    rescanAction_ = add(file, LUX_UI_TEXT("리소스 재검색"), LUX_UI_TEXT("rescanResources"), {},
                        [this] { RescanProjectResources(); });
    file->addSeparator();
    add(file, LUX_UI_TEXT("새 씬"), LUX_UI_TEXT("newScene"), LuxUI::KeySequence::New,
        [this] { NewSceneDocument(); });
    add(file, LUX_UI_TEXT("장면 열기..."), LUX_UI_TEXT("openScene"), LuxUI::KeySequence::Open,
        [this] {
            const LuxUI::String file = LuxUI::FileDialog::getOpenFileName(
                this, LUX_UI_TEXT("장면 열기"), {}, LUX_UI_TEXT("Lux 장면 (*.lux);;모든 파일 (*)"));
            if (!file.isEmpty())
                OpenProjectScene(PathFromUtf8(Utf8(file)));
        });
    add(file, LUX_UI_TEXT("저장"), LUX_UI_TEXT("saveScene"), LuxUI::KeySequence::Save,
        [this] { SaveScene(false); });
    add(file, LUX_UI_TEXT("다른 이름으로 저장..."), LUX_UI_TEXT("saveSceneAs"),
        LuxUI::KeySequence::SaveAs, [this] { SaveScene(true); });
    file->addSeparator();
    add(file, LUX_UI_TEXT("끝내기"), LUX_UI_TEXT("quit"), LuxUI::KeySequence::Quit,
        [this] { close(); });
    BuildSceneMenus();
    LuxUI::Menu* edit = menuBar()->addMenu(LUX_UI_TEXT("편집"));
    undoAction_ = add(edit, LUX_UI_TEXT("실행 취소"), LUX_UI_TEXT("undoEdit"),
                      LuxUI::KeySequence::Undo, [this] {
                          if (!viewportEditBefore_.isEmpty())
                              CancelViewportEdit();
                          else
                              undo_->undo();
                      });
    redoAction_ = add(edit, LUX_UI_TEXT("다시 실행"), LUX_UI_TEXT("redoEdit"),
                      LuxUI::KeySequence::Redo, [this] {
                          if (!viewportEditBefore_.isEmpty())
                              CancelViewportEdit();
                          else
                              undo_->redo();
                      });
    add(edit, LUX_UI_TEXT("복사"), LUX_UI_TEXT("copyEntity"), LuxUI::KeySequence::Copy,
        [this] { CopyEntity(); });
    add(edit, LUX_UI_TEXT("붙여넣기"), LUX_UI_TEXT("pasteEntity"), LuxUI::KeySequence::Paste,
        [this] { PasteEntity(); });
    edit->addSeparator();
    add(edit, LUX_UI_TEXT("2D 엔티티 추가"), LUX_UI_TEXT("addEntity"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::Key_E), [this] { AddEntity(); });
    add(edit, LUX_UI_TEXT("3D 메시 엔티티 추가"), LUX_UI_TEXT("addMeshEntity"), {},
        [this] { AddMeshEntity(); });
    AddBasicModelMenu(edit);
    add(edit, LUX_UI_TEXT("타일맵 추가"), LUX_UI_TEXT("addTilemap"), {}, [this] { AddTilemap(); });
    add(edit, LUX_UI_TEXT("선택 삭제"), LUX_UI_TEXT("deleteEntity"), LuxUI::KeySequence::Delete,
        [this] { DeleteEntity(); });
    LuxUI::Menu* view = menuBar()->addMenu(LUX_UI_TEXT("보기"));
    add(view, LUX_UI_TEXT("애니메이션 에디터"), LUX_UI_TEXT("openAnimationWorkspace"), {},
        [this] { SetWorkspace(true); });
    view->addSeparator();
    add(view, LUX_UI_TEXT("2D 작업 화면"), LUX_UI_TEXT("view2D"), {},
        [this] { viewportTabs_->setCurrentIndex(1); });
    add(view, LUX_UI_TEXT("3D 배치 화면"), LUX_UI_TEXT("view3D"), {},
        [this] { viewportTabs_->setCurrentIndex(0); });
    add(view, LUX_UI_TEXT("선택 항목에 초점"), LUX_UI_TEXT("focusSelection"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F), [this] {
            if (viewportTabs_->currentWidget() == canvas3D_)
                canvas3D_->FocusSelection();
            else
                canvas_->FocusSelection();
        });
    add(view, LUX_UI_TEXT("전체 화면"), LUX_UI_TEXT("fullscreen"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F11),
        [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    colliderAction_ = view->addAction(LUX_UI_TEXT("2D 충돌체 표시"));
    colliderAction_->setCheckable(true);
    connect(colliderAction_, &LuxUI::Action::toggled, this, [this](bool enabled) {
        canvas_->SetDebugColliders(enabled);
        overviewCanvas_->SetDebugColliders(enabled);
    });
    auto* cameraMenu = menuBar()->addMenu(LUX_UI_TEXT("카메라"));
    AddCommand(cameraMenu, "alignCameraToSceneView", "선택 카메라를 씬 뷰에 맞추기",
               [this] { AlignCameraToSceneView(); });
    AddCommand(cameraMenu, "viewThroughSelectedCamera", "선택 카메라 시점으로 보기",
               [this] { ViewThroughSelectedCamera(); });
    LuxUI::Menu* debug = menuBar()->addMenu(LUX_UI_TEXT("디버그"));
    add(debug, LUX_UI_TEXT("장면 실행"), LUX_UI_TEXT("previewStart"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F5), [this] { StartPreview(); });
    add(debug, LUX_UI_TEXT("일시정지/재개"), LUX_UI_TEXT("previewPause"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F6), [this] { TogglePreviewPause(); });
    add(debug, LUX_UI_TEXT("한 프레임 실행"), LUX_UI_TEXT("previewStep"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F7), [this] { StepPreview(); });
    add(debug, LUX_UI_TEXT("실행 중지"), LUX_UI_TEXT("previewStop"),
        LuxUI::KeySequence(LuxUI::Flags::SHIFT | LuxUI::Flags::Key_F5), [this] { StopPreview(); });
    debug->addSeparator();
    add(debug, LUX_UI_TEXT("별도 창에서 실행"), LUX_UI_TEXT("playWindow"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::Key_F5),
        [this] { RunSceneInWindow(); });
    add(debug, LUX_UI_TEXT("게임 창 닫기"), LUX_UI_TEXT("playWindowStop"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::SHIFT | LuxUI::Flags::Key_F5),
        [this] { StopSceneWindow(); });
    breakOnCollisionAction_ = debug->addAction(LUX_UI_TEXT("충돌 시 일시정지"));
    breakOnCollisionAction_->setCheckable(true);
    debug->addAction(docks_[7]->toggleViewAction());
    LuxUI::Menu* buildMenu = menuBar()->addMenu(LUX_UI_TEXT("빌드"));
    add(buildMenu, LUX_UI_TEXT("Debug 빌드"), LUX_UI_TEXT("buildDebug"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::Key_B),
        [this] { StartBuild(BuildConfiguration::Debug, false, BuildFollowUp::None); });
    add(buildMenu, LUX_UI_TEXT("Release 빌드"), LUX_UI_TEXT("buildRelease"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::SHIFT | LuxUI::Flags::Key_B),
        [this] { StartBuild(BuildConfiguration::Release, false, BuildFollowUp::None); });
    add(buildMenu, LUX_UI_TEXT("빌드 후 실행 (Debug)"), LUX_UI_TEXT("buildRun"),
        LuxUI::KeySequence(LuxUI::Flags::Key_F9),
        [this] { StartBuild(BuildConfiguration::Debug, false, BuildFollowUp::Run); });
    add(buildMenu, LUX_UI_TEXT("클린 빌드 (Debug)"), LUX_UI_TEXT("buildClean"), {},
        [this] { StartBuild(BuildConfiguration::Debug, true, BuildFollowUp::None); });
    buildMenu->addSeparator();
    add(buildMenu, LUX_UI_TEXT("배포 ZIP 만들기 (Release)..."), LUX_UI_TEXT("buildPackage"), {},
        [this] { StartBuild(BuildConfiguration::Release, false, BuildFollowUp::Package); });
    buildMenu->addSeparator();
    add(buildMenu, LUX_UI_TEXT("빌드 중지"), LUX_UI_TEXT("buildCancel"), {},
        [this] { CancelBuild(); });
    add(buildMenu, LUX_UI_TEXT("엔진 SDK 경로 설정..."), LUX_UI_TEXT("buildSdk"), {},
        [this] { ChooseEnginePackage(); });
    LuxUI::Menu* window = menuBar()->addMenu(LUX_UI_TEXT("창"));
    add(window, LUX_UI_TEXT("새 에디터 창"), LUX_UI_TEXT("newWindow"), {}, [] {
        auto* next = new EditorWindow;
        next->setAttribute(LuxUI::Flags::WA_DeleteOnClose);
        next->show();
    });
    LuxUI::Menu* displays = window->addMenu(LUX_UI_TEXT("화면으로 이동"));
    connect(displays, &LuxUI::Menu::aboutToShow, this, [this, displays] {
        displays->clear();
        for (LuxUI::Screen* screen : LuxUI::GuiApplication::screens())
            displays->addAction(screen->name(), this, [this, screen] {
                const LuxUI::Rect area = screen->availableGeometry();
                move(area.center() - rect().center());
            });
    });
    add(window, LUX_UI_TEXT("명령 검색..."), LUX_UI_TEXT("commandPalette"),
        LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::Key_P),
        [this] { OpenCommandPalette(); });
    window->addSeparator();
    for (LuxUI::DockWidget* dock : docks_)
        window->addAction(dock->toggleViewAction());
    window->addSeparator();
    add(window, LUX_UI_TEXT("기본 배치 복원"), LUX_UI_TEXT("resetLayout"), {},
        [this] { ResetWorkspaceLayout(); });
    LuxUI::Menu* preferences = menuBar()->addMenu(LUX_UI_TEXT("설정"));
    LuxUI::Menu* themes = preferences->addMenu(LUX_UI_TEXT("테마"));
    for (const auto& [id, label] : std::array<std::pair<const char*, const char*>, 2>{
             {{"dark", "다크"}, {"light", "라이트"}}})
        themes->addAction(LuxUI::String::fromUtf8(label), this, [this, id] {
            theme_ = LuxUI::String::fromLatin1(id);
            ApplyTheme();
        });
    themes->addAction(LUX_UI_TEXT("사용자 정의 스타일 불러오기..."), this, [this] {
        const LuxUI::String path = LuxUI::FileDialog::getOpenFileName(
            this, LUX_UI_TEXT("스타일 파일 선택"), {}, LUX_UI_TEXT("Qt 스타일 (*.qss)"));
        LuxUI::File file(path);
        if (!path.isEmpty() && file.open(LuxUI::IODevice::ReadOnly) && file.size() <= 1024 * 1024) {
            LuxUI::Settings().setValue(LUX_UI_TEXT("customStyle"),
                                       LuxUI::String::fromUtf8(file.readAll()));
            theme_ = LUX_UI_TEXT("custom");
            ApplyTheme();
        }
    });
    LuxUI::Menu* scale = preferences->addMenu(LUX_UI_TEXT("UI 배율"));
    for (double factor : {.75, 1.0, 1.25, 1.5, 2.0})
        scale->addAction(LUX_UI_TEXT("%1%").arg(static_cast<int>(factor * 100)), this,
                         [this, factor] {
                             uiScale_ = factor;
                             ApplyTheme();
                         });
    add(preferences, LUX_UI_TEXT("단축키 설정..."), LUX_UI_TEXT("shortcuts"), {},
        [this] { ConfigureShortcuts(); });
    add(preferences, LUX_UI_TEXT("설정 내보내기..."), LUX_UI_TEXT("exportSettings"), {},
        [this] { ExportEditorSettings(); });
    add(preferences, LUX_UI_TEXT("설정 가져오기..."), LUX_UI_TEXT("importSettings"), {},
        [this] { ImportEditorSettings(); });
    UpdateRecentMenu();
}

void EditorWindow::UpdateRecentMenu() {
    recentMenu_->clear();
    const auto& files = recentProjects_.Get();
    if (files.empty()) {
        auto* item = recentMenu_->addAction(LUX_UI_TEXT("최근 프로젝트가 없습니다."));
        item->setEnabled(false);
        return;
    }
    for (const auto& file : files)
        recentMenu_->addAction(Q(file), this, [this, file] { OpenProjectFile(file); });
}

void EditorWindow::OpenCommandPalette() {
    LuxUI::Dialog dialog(this);
    dialog.setWindowTitle(LUX_UI_TEXT("명령 검색"));
    dialog.resize(460, 420);
    auto* layout = new LuxUI::VBoxLayout(&dialog);
    auto* search = new LuxUI::LineEdit(&dialog);
    search->setPlaceholderText(LUX_UI_TEXT("명령을 입력해 주세요."));
    auto* list = new LuxUI::ListWidget(&dialog);
    layout->addWidget(search);
    layout->addWidget(list);
    std::vector<LuxUI::Action*> commands;
    for (LuxUI::Action* action : shortcutActions_)
        if (action->isEnabled() && action->text() != LUX_UI_TEXT("끝내기"))
            commands.push_back(action);
    auto refresh = [search, list, commands] {
        list->clear();
        for (LuxUI::Action* action : commands)
            if (action->text().contains(search->text(), LuxUI::Flags::CaseInsensitive))
                list->addItem(action->text());
        if (list->count())
            list->setCurrentRow(0);
    };
    connect(search, &LuxUI::LineEdit::textChanged, &dialog, refresh);
    connect(search, &LuxUI::LineEdit::returnPressed, &dialog, &LuxUI::Dialog::accept);
    connect(list, &LuxUI::ListWidget::itemDoubleClicked, &dialog, &LuxUI::Dialog::accept);
    refresh();
    search->setFocus();
    if (dialog.exec() != LuxUI::Dialog::Accepted || !list->currentItem())
        return;
    const LuxUI::String chosen = list->currentItem()->text();
    for (LuxUI::Action* action : commands)
        if (action->text() == chosen) {
            action->trigger();
            break;
        }
}

void EditorWindow::ConfigureShortcuts() {
    LuxUI::Dialog dialog(this);
    dialog.setWindowTitle(LUX_UI_TEXT("단축키 설정"));
    dialog.resize(540, 500);
    auto* layout = new LuxUI::VBoxLayout(&dialog);
    auto* list = new LuxUI::ListWidget(&dialog);
    for (LuxUI::Action* action : shortcutActions_)
        list->addItem(action->text());
    auto* editor = new LuxUI::KeySequenceEdit(&dialog);
    layout->addWidget(list);
    layout->addWidget(new LuxUI::Label(LUX_UI_TEXT("선택한 명령의 단축키"), &dialog));
    layout->addWidget(editor);
    auto* buttons = new LuxUI::DialogButtonBox(
        LuxUI::DialogButtonBox::Save | LuxUI::DialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(list, &LuxUI::ListWidget::currentRowChanged, &dialog, [this, editor](int row) {
        if (row >= 0)
            editor->setKeySequence(shortcutActions_[static_cast<size_t>(row)]->shortcut());
    });
    connect(buttons, &LuxUI::DialogButtonBox::accepted, &dialog, &LuxUI::Dialog::accept);
    connect(buttons, &LuxUI::DialogButtonBox::rejected, &dialog, &LuxUI::Dialog::reject);
    list->setCurrentRow(0);
    if (dialog.exec() != LuxUI::Dialog::Accepted || list->currentRow() < 0)
        return;
    auto* action = shortcutActions_[static_cast<size_t>(list->currentRow())];
    action->setShortcut(editor->keySequence());
    LuxUI::Settings().setValue(LUX_UI_TEXT("shortcuts/") + action->objectName(),
                               editor->keySequence().toString());
    statusBar()->showMessage(LUX_UI_TEXT("단축키를 저장했습니다."), 4000);
}

void EditorWindow::ExportEditorSettings() {
    const LuxUI::String path = LuxUI::FileDialog::getSaveFileName(
        this, LUX_UI_TEXT("에디터 설정 내보내기"), LUX_UI_TEXT("LuxEditorSettings.json"),
        LUX_UI_TEXT("JSON (*.json)"));
    if (path.isEmpty())
        return;
    LuxUI::JsonObject shortcuts;
    for (LuxUI::Action* action : shortcutActions_)
        shortcuts.insert(action->objectName(), action->shortcut().toString());
    LuxUI::JsonObject settings{{LUX_UI_TEXT("format"), 1},
                               {LUX_UI_TEXT("theme"), theme_},
                               {LUX_UI_TEXT("uiScale"), uiScale_},
                               {LUX_UI_TEXT("shortcuts"), shortcuts}};
    if (theme_ == LuxUI::Latin1String("custom"))
        settings.insert(LUX_UI_TEXT("customStyle"),
                        LuxUI::Settings().value(LUX_UI_TEXT("customStyle")).toString());
    LuxUI::File file(path);
    if (!file.open(LuxUI::IODevice::WriteOnly | LuxUI::IODevice::Truncate) ||
        file.write(LuxUI::JsonDocument(settings).toJson()) < 0)
        ShowError(LUX_UI_TEXT("설정 내보내기 실패"),
                  LUX_UI_TEXT("설정 파일을 저장할 수 없습니다."));
    else
        Log(LUX_UI_TEXT("에디터 설정을 내보냈습니다."));
}

void EditorWindow::ImportEditorSettings() {
    const LuxUI::String path = LuxUI::FileDialog::getOpenFileName(
        this, LUX_UI_TEXT("에디터 설정 가져오기"), {}, LUX_UI_TEXT("JSON (*.json)"));
    if (path.isEmpty())
        return;
    LuxUI::File file(path);
    if (!file.open(LuxUI::IODevice::ReadOnly) || file.size() > 1024 * 1024) {
        ShowError(LUX_UI_TEXT("설정 가져오기 실패"), LUX_UI_TEXT("설정 파일을 읽을 수 없습니다."));
        return;
    }
    LuxUI::JsonParseError parse;
    const auto doc = LuxUI::JsonDocument::fromJson(file.readAll(), &parse);
    const auto object = doc.object();
    const LuxUI::String theme = object.value(LUX_UI_TEXT("theme")).toString();
    const double scale = object.value(LUX_UI_TEXT("uiScale")).toDouble(-1);
    if (parse.error != LuxUI::JsonParseError::NoError ||
        object.value(LUX_UI_TEXT("format")).toInt() != 1 ||
        (theme != LuxUI::Latin1String("dark") && theme != LuxUI::Latin1String("light") &&
         theme != LuxUI::Latin1String("custom")) ||
        scale < .75 || scale > 2.0 || !object.value(LUX_UI_TEXT("shortcuts")).isObject()) {
        ShowError(LUX_UI_TEXT("설정 가져오기 실패"),
                  LUX_UI_TEXT("설정 파일 형식이 올바르지 않습니다."));
        return;
    }
    LuxUI::Settings settings;
    if (theme == LuxUI::Latin1String("custom")) {
        const LuxUI::String style = object.value(LUX_UI_TEXT("customStyle")).toString();
        if (style.size() > 1024 * 1024) {
            ShowError(LUX_UI_TEXT("설정 가져오기 실패"), LUX_UI_TEXT("스타일 파일이 너무 큽니다."));
            return;
        }
        settings.setValue(LUX_UI_TEXT("customStyle"), style);
    }
    const LuxUI::JsonObject shortcuts = object.value(LUX_UI_TEXT("shortcuts")).toObject();
    for (LuxUI::Action* action : shortcutActions_) {
        const auto value = shortcuts.value(action->objectName());
        if (value.isString()) {
            action->setShortcut(LuxUI::KeySequence(value.toString()));
            settings.setValue(LUX_UI_TEXT("shortcuts/") + action->objectName(), value.toString());
        }
    }
    theme_ = theme;
    uiScale_ = scale;
    ApplyTheme();
    Log(LUX_UI_TEXT("에디터 설정을 가져왔습니다."));
}

void EditorWindow::Log(const LuxUI::String& message, bool error) {
    lastLogMessage_ = message;
    if (developmentConsole_)
        developmentConsole_->Append(error ? LuxUI::Severity::Error : LuxUI::Severity::Info,
                                    "에디터", message.toUtf8().toStdString());
    if (error) {
        consoleOutput_->appendPlainText(message);
        debugPanel_->AddLog(message, true);
        issuesOutput_->appendPlainText(message);
        docks_[4]->show();
        docks_[4]->raise();
    }
    statusBar()->showMessage(message, 5000);
}

void EditorWindow::ShowError(const LuxUI::String& title, const LuxUI::String& message) {
    Log(title + LUX_UI_TEXT(": ") + message, true);
    LuxUI::MessageBox::critical(this, title, message);
}

void EditorWindow::RunWithProgress(const LuxUI::String& message,
                                   const std::function<void()>& task) {
    LuxUI::ProgressDialog progress(message, LuxUI::String(), 0, 0, this);
    progress.setWindowModality(LuxUI::Flags::WindowModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();
    LuxUI::Application::processEvents();
    task();
    progress.close();
}

void EditorWindow::closeEvent(LuxUI::CloseEvent* event) {
    CancelViewportEdit();
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

void EditorWindow::resizeEvent(LuxUI::ResizeEvent* event) {
    LuxUI::MainWindow::resizeEvent(event);
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

void EditorWindow::dragEnterEvent(LuxUI::DragEnterEvent* event) {
    for (const LuxUI::Url& url : event->mimeData()->urls()) {
        const LuxUI::String suffix = LuxUI::FileInfo(url.toLocalFile()).suffix().toLower();
        if (suffix == LuxUI::Latin1String("luxproject") ||
            suffix == LuxUI::Latin1String("luxproj") || suffix == LuxUI::Latin1String("lux") ||
            suffix == LuxUI::Latin1String("luxpack") ||
            suffix == LuxUI::Latin1String("luxbackup")) {
            event->acceptProposedAction();
            return;
        }
    }
}

void EditorWindow::dropEvent(LuxUI::DropEvent* event) {
    if (event->mimeData()->urls().isEmpty())
        return;
    const auto path = PathFromUtf8(Utf8(event->mimeData()->urls().first().toLocalFile()));
    if (path.extension() == ".luxproject" || path.extension() == ".luxproj")
        OpenProjectFile(path);
    else if (LuxUI::String::fromUtf8(PathUtf8(path.extension())).toLower() ==
             LuxUI::Latin1String(".luxprefab"))
        AddPrefabAsset(path);
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
                ShowError(LUX_UI_TEXT("가져오기 실패"), Q(error));
        }
    }
    event->acceptProposedAction();
}

void EditorWindow::RefreshAll() {
    RefreshSceneTabs();
    RefreshHierarchy();
    RefreshProjectExplorer();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    gameCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::RefreshProjectExplorer() {
    RefreshAssetFolders();
    const auto row = projectFiles_->currentRow();
    const auto selectedPath = row >= 0 && static_cast<size_t>(row) < projectFilePaths_.size()
                                  ? projectFilePaths_[static_cast<size_t>(row)]
                                  : std::filesystem::path{};
    projectFiles_->clear();
    projectFilePaths_.clear();
    assetPreview_->clear();
    assetDetails_->clear();
    if (!project_) {
        assetCount_->setText(LUX_UI_TEXT("프로젝트 없음"));
        return;
    }
    const auto root = project_->GetFile().parent_path();
    const LuxUI::String filter = projectSearch_->text();
    const int category = assetFilter_->currentIndex();
    LuxUI::FileIconProvider icons;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    for (; !error && it != end && projectFilePaths_.size() < 500; it.increment(error)) {
        const auto relative = it->path().lexically_relative(root);
        const auto filename = it->path().filename().string();
        if (it->is_symlink(error) ||
            (it->is_directory(error) &&
             (filename.starts_with('.') || filename == "out" || filename == "build" ||
              filename.starts_with("cmake-build")))) {
            it.disable_recursion_pending();
            continue;
        }
        if (!relative.empty() && (*relative.begin() == ".git" || *relative.begin() == ".lux")) {
            if (it->is_directory(error))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error)) {
            if (!assetFolder_.empty()) {
                const auto inside = relative.lexically_relative(assetFolder_);
                if (inside.empty() || *inside.begin() == "..")
                    continue;
            }
            const LuxUI::String label = Q(GenericPathUtf8(relative));
            const LuxUI::String suffix = Q(PathUtf8(it->path().extension())).toLower();
            const bool scene = suffix == LuxUI::Latin1String(".lux") ||
                               suffix == LuxUI::Latin1String(".luxprefab");
            const bool image =
                suffix == LuxUI::Latin1String(".png") || suffix == LuxUI::Latin1String(".jpg") ||
                suffix == LuxUI::Latin1String(".jpeg") || suffix == LuxUI::Latin1String(".webp") ||
                suffix == LuxUI::Latin1String(".bmp");
            const bool audio = suffix == LuxUI::Latin1String(".wav") ||
                               suffix == LuxUI::Latin1String(".ogg") ||
                               suffix == LuxUI::Latin1String(".mp3");
            const bool model =
                suffix == LuxUI::Latin1String(".obj") || suffix == LuxUI::Latin1String(".fbx");
            if ((filter.isEmpty() || label.contains(filter, LuxUI::Flags::CaseInsensitive)) &&
                (category == 0 || (category == 1 && scene) || (category == 2 && image) ||
                 (category == 3 && audio) || (category == 4 && model) ||
                 (category == 5 && !scene && !image && !audio && !model))) {
                projectFilePaths_.push_back(it->path());
                auto* item = new LuxUI::ListWidgetItem(label);
                item->setData(LuxUI::Flags::UserRole, Q(it->path()));
                LuxUI::ImageReader reader(Q(it->path()));
                const auto imageSize = reader.size();
                LuxUI::Pixmap thumbnail;
                if (image && imageSize.isValid() && imageSize.width() <= 8192 &&
                    imageSize.height() <= 8192) {
                    reader.setScaledSize(imageSize.scaled(128, 128, LuxUI::Flags::KeepAspectRatio));
                    thumbnail = LuxUI::Pixmap::fromImage(reader.read());
                }
                item->setIcon(model                ? MakeEditorIcon(EditorIcon::Mesh)
                              : thumbnail.isNull() ? icons.icon(LuxUI::FileInfo(Q(it->path())))
                                                   : LuxUI::Icon(thumbnail));
                item->setToolTip(label);
                projectFiles_->addItem(item);
            }
        }
    }
    assetCount_->setText(LUX_UI_TEXT("%1개 항목").arg(projectFilePaths_.size()));
    if (!selectedPath.empty()) {
        const auto found =
            std::find(projectFilePaths_.begin(), projectFilePaths_.end(), selectedPath);
        if (found != projectFilePaths_.end())
            projectFiles_->setCurrentRow(
                static_cast<int>(std::distance(projectFilePaths_.begin(), found)));
    }
}

void EditorWindow::RefreshHierarchy() {
    refreshingHierarchy_ = true;
    const LuxUI::SignalBlocker block(hierarchy_);
    std::unordered_set<LuxEngine::uint32> expanded;
    for (LuxUI::TreeWidgetItemIterator it(hierarchy_); *it; ++it)
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, LuxUI::Flags::UserRole).toUInt());
    const bool initial = hierarchy_->topLevelItemCount() == 0;
    hierarchy_->clear();
    const auto selected = document_.GetSelected();
    auto& scene = document_.GetScene();
    const auto entities = scene.GetEntities();
    std::unordered_set<LuxEngine::uint32> visible;
    size_t sprites = 0, meshes = 0;
    for (const auto& entity : entities) {
        sprites += entity.HasComponent<LuxEngine::SpriteRendererComponent>();
        meshes += entity.HasComponent<LuxEngine::MeshRendererComponent>();
        const LuxUI::String name = entity.HasComponent<LuxEngine::TagComponent>()
                                       ? Q(entity.GetComponent<LuxEngine::TagComponent>().tag)
                                       : LUX_UI_TEXT("#%1").arg(entity.GetId());
        if ((groupFilter_ != 1 || entity.HasComponent<LuxEngine::SpriteRendererComponent>()) &&
            (groupFilter_ != 2 || entity.HasComponent<LuxEngine::MeshRendererComponent>()) &&
            name.contains(hierarchySearch_->text(), LuxUI::Flags::CaseInsensitive)) {
            auto current = entity.GetId();
            while (current && scene.IsAlive(current) && visible.insert(current).second)
                current = scene.GetParent(current);
        }
    }
    const std::array<size_t, 3> counts{entities.size(), sprites, meshes};
    for (size_t index = 0; index < counts.size(); ++index)
        groupTree_->topLevelItem(static_cast<int>(index))
            ->setText(1, LuxUI::String::number(counts[index]));
    std::unordered_map<LuxEngine::uint32, LuxUI::TreeWidgetItem*> items;
    for (const auto& entity : entities) {
        if (!visible.contains(entity.GetId()))
            continue;
        auto* item = new LuxUI::TreeWidgetItem;
        item->setData(0, LuxUI::Flags::UserRole, entity.GetId());
        item->setText(0, entity.HasComponent<LuxEngine::TagComponent>()
                             ? Q(entity.GetComponent<LuxEngine::TagComponent>().tag)
                             : LUX_UI_TEXT("#%1").arg(entity.GetId()));
        item->setToolTip(0, LUX_UI_TEXT("#%1").arg(entity.GetId()));
        item->setIcon(0, MakeEditorIcon(entity.HasComponent<LuxEngine::MeshRendererComponent>()
                                            ? EditorIcon::Mesh
                                            : EditorIcon::Sprite));
        items.emplace(entity.GetId(), item);
    }
    for (const auto& entity : entities) {
        const auto found = items.find(entity.GetId());
        if (found == items.end())
            continue;
        const auto parent = items.find(scene.GetParent(entity.GetId()));
        if (parent != items.end() && parent->second != found->second)
            parent->second->addChild(found->second);
        else
            hierarchy_->addTopLevelItem(found->second);
        found->second->setExpanded(initial || expanded.contains(entity.GetId()) ||
                                   !hierarchySearch_->text().isEmpty());
        found->second->setSelected(document_.IsSelected(entity.GetId()));
    }
    if (selected) {
        const auto active = items.find(selected.GetId());
        if (active != items.end())
            hierarchy_->setCurrentItem(active->second, 0, LuxUI::ItemSelectionModel::NoUpdate);
    }
    refreshingHierarchy_ = false;
}

void EditorWindow::RefreshInspector() {
    refreshingInspector_ = true;
    if (auto* space = findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("transformSpace"))) {
        const LuxUI::SignalBlocker blocker(space);
        space->setCurrentIndex(static_cast<int>(document_.GetTransformSpace()));
    }
    if (auto* pivot = findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("selectionPivot"))) {
        const LuxUI::SignalBlocker blocker(pivot);
        pivot->setCurrentIndex(static_cast<int>(document_.GetSelectionPivotMode()));
    }
    RefreshScriptDetails();
    RefreshPhysics3DDetails();
    LuxEngine::Entity selected = document_.GetSelected();
    const auto count = document_.GetSelection().size();
    selectionLabel_->setText(
        count > 1  ? LUX_UI_TEXT("%1개 선택 · 활성 #%2").arg(count).arg(selected.GetId())
        : selected ? LUX_UI_TEXT("엔티티 #%1").arg(selected.GetId())
                   : LUX_UI_TEXT("선택된 엔티티 없음"));
    selectionLabel_->setToolTip(
        LUX_UI_TEXT("표시 값은 활성 오브젝트 기준입니다. Transform 필드를 변경하면 선택된 "
                    "오브젝트에 함께 적용합니다."));
    animationPanel_->setEnabled(
        selected && selected.HasComponent<LuxEngine::TransformComponent>() && !previewActive_);
    animationTarget_->setText(selected ? LUX_UI_TEXT("대상 · 엔티티 #%1").arg(selected.GetId())
                                       : LUX_UI_TEXT("대상 없음"));
    inspectorBody_->setVisible(static_cast<bool>(selected));
    inspectorBody_->setEnabled(static_cast<bool>(selected));
    const auto* reference =
        selected ? selected.TryGetComponent<LuxEngine::SceneReferenceComponent>() : nullptr;
    if (reference) {
        sceneReferencePath_->setText(Q(reference->path));
        sceneReferencePath_->setToolTip(Q(reference->path));
        const auto mode =
            reference->mode == LuxEngine::SceneReferenceMode::Inherited  ? LUX_UI_TEXT("상속")
            : reference->mode == LuxEngine::SceneReferenceMode::Additive ? LUX_UI_TEXT("Additive")
                                                                         : LUX_UI_TEXT("중첩");
        sceneReferenceState_->setText(
            mode + LUX_UI_TEXT(" · ") +
            (reference->loaded ? LUX_UI_TEXT("로드됨") : LUX_UI_TEXT("언로드됨")));
    }
    if (selected) {
        name_->setEnabled(selected.HasComponent<LuxEngine::TagComponent>());
        name_->setText(selected.HasComponent<LuxEngine::TagComponent>()
                           ? Q(selected.GetComponent<LuxEngine::TagComponent>().tag)
                           : LuxUI::String());
        const auto value = selected.HasComponent<LuxEngine::TransformComponent>()
                               ? selected.GetComponent<LuxEngine::TransformComponent>()
                               : LuxEngine::TransformComponent{};
        const double degrees = 180.0 / std::numbers::pi;
        const std::array<std::array<double, 3>, 3> rows{
            {{{value.position.x, value.position.y, value.position.z}},
             {{value.rotation.x * degrees, value.rotation.y * degrees, value.rotation.z * degrees}},
             {{value.scale.x, value.scale.y, value.scale.z}}}};
        for (size_t row = 0; row < 3; ++row)
            for (size_t col = 0; col < 3; ++col) {
                transform_[row][col]->setValue(rows[row][col]);
                transform_[row][col]->setEnabled(
                    selected.HasComponent<LuxEngine::TransformComponent>());
            }
        const bool sprite = selected.HasComponent<LuxEngine::SpriteRendererComponent>();
        spriteEnabled_->setChecked(sprite);
        const LuxEngine::Vec4 color =
            sprite ? selected.GetComponent<LuxEngine::SpriteRendererComponent>().color
                   : LuxEngine::Vec4{1, 1, 1, 1};
        const std::array<float, 4> channels{color.x, color.y, color.z, color.w};
        for (size_t i = 0; i < 4; ++i) {
            color_[i]->setValue(std::clamp(static_cast<double>(channels[i]), 0.0, 1.0));
            color_[i]->setEnabled(sprite);
        }
        const bool mesh = selected.HasComponent<LuxEngine::MeshRendererComponent>();
        meshEnabled_->setChecked(mesh);
        meshPath_->setText(
            mesh ? Q(selected.GetComponent<LuxEngine::MeshRendererComponent>().meshPath)
                 : LuxUI::String{});
        meshPath_->setEnabled(mesh);
        baseColorTexturePath_->setText(
            mesh ? Q(selected.GetComponent<LuxEngine::MeshRendererComponent>()
                         .material.baseColorTexture)
                 : LuxUI::String{});
        baseColorTexturePath_->setEnabled(mesh);
        const LuxEngine::Material3D material =
            mesh ? selected.GetComponent<LuxEngine::MeshRendererComponent>().material
                 : LuxEngine::Material3D{};
        metallic_->setValue(std::clamp(static_cast<double>(material.metallic), 0.0, 1.0));
        roughness_->setValue(std::clamp(static_cast<double>(material.roughness), 0.0, 1.0));
        metallic_->setEnabled(mesh);
        roughness_->setEnabled(mesh);
        materialAlphaMode_->setCurrentIndex(static_cast<int>(material.alphaMode));
        materialAlphaMode_->setEnabled(mesh);
        materialAlphaCutoff_->setValue(material.alphaCutoff);
        materialAlphaCutoff_->setEnabled(mesh &&
                                         material.alphaMode == LuxEngine::MaterialAlphaMode::Mask);
        materialDoubleSided_->setChecked(material.doubleSided);
        materialDoubleSided_->setEnabled(mesh);
        const std::array<std::string, 4> paths{material.normalTexture, material.metallicTexture,
                                               material.roughnessTexture,
                                               material.occlusionTexture};
        for (size_t i = 0; i < paths.size(); ++i) {
            materialMapPaths_[i]->setText(Q(paths[i]));
            materialMapPaths_[i]->setEnabled(mesh);
        }
        materialNormalScale_->setValue(material.normalScale);
        materialNormalScale_->setEnabled(mesh);
        materialOcclusionStrength_->setValue(material.occlusionStrength);
        materialOcclusionStrength_->setEnabled(mesh);
        materialMipmaps_->setChecked(material.textureSampling.mipmaps);
        materialMipmaps_->setEnabled(mesh);
        materialAddressMode_->setCurrentIndex(
            static_cast<int>(material.textureSampling.addressMode));
        materialAddressMode_->setEnabled(mesh);
        materialAnisotropic_->setChecked(material.textureSampling.anisotropic);
        materialAnisotropic_->setEnabled(mesh);
        const bool tiled = selected.HasComponent<LuxEngine::TilemapComponent>();
        tilemapEnabled_->setChecked(tiled);
        const LuxEngine::TilemapComponent map =
            tiled ? selected.GetComponent<LuxEngine::TilemapComponent>()
                  : LuxEngine::TilemapComponent{};
        tilemapWidth_->setValue(static_cast<int>(map.width));
        tilemapHeight_->setValue(static_cast<int>(map.height));
        tilemapSize_->setValue(map.tileSize);
        tilemapWidth_->setEnabled(tiled);
        tilemapHeight_->setEnabled(tiled);
        tilemapSize_->setEnabled(tiled);
        const bool hasBody = selected.HasComponent<LuxEngine::Rigidbody2DComponent>();
        bodyEnabled_->setChecked(hasBody);
        const LuxEngine::Rigidbody2DComponent body =
            hasBody ? selected.GetComponent<LuxEngine::Rigidbody2DComponent>()
                    : LuxEngine::Rigidbody2DComponent{};
        bodyType_->setCurrentIndex(body.type == LuxEngine::BodyType::Static    ? 0
                                   : body.type == LuxEngine::BodyType::Dynamic ? 1
                                                                               : 2);
        gravityScale_->setValue(body.gravityScale);
        bodyType_->setEnabled(hasBody);
        gravityScale_->setEnabled(hasBody);
        const bool hasCollider = selected.HasComponent<LuxEngine::BoxCollider2DComponent>();
        colliderEnabled_->setChecked(hasCollider);
        const LuxEngine::BoxCollider2DComponent collider =
            hasCollider ? selected.GetComponent<LuxEngine::BoxCollider2DComponent>()
                        : LuxEngine::BoxCollider2DComponent{};
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
        const bool animated = selected.HasComponent<LuxEngine::AnimationComponent>();
        const bool canAnimate = selected.HasComponent<LuxEngine::TransformComponent>();
        animationEnabled_->setChecked(animated);
        animationEnabled_->setEnabled(canAnimate);
        const LuxEngine::AnimationComponent animation =
            animated ? selected.GetComponent<LuxEngine::AnimationComponent>()
                     : LuxEngine::AnimationComponent{};
        animationName_->setEnabled(animated);
        animationTime_->setEnabled(animated);
        animationName_->setText(LuxUI::String::fromUtf8(animation.clip.name));
        animationTime_->setValue(animation.currentTime);
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
            keyframeList_->addItem(LUX_UI_TEXT("%1 s").arg(key.time, 0, 'f', 3));
        animationLoop_->setEnabled(animated && canAnimate);
        animationDuration_->setEnabled(animated && canAnimate);
        animationTimeline_->setEnabled(animated && canAnimate);
        keyframeList_->setEnabled(animated);
    }
    RefreshCameraDetails();
    refreshingInspector_ = false;
    FilterDetails(detailsSearch_->text());
    RefreshAnimationWorkspace();
}

void EditorWindow::UpdateChrome() {
    if (gameCanvas_)
        gameCanvas_->RefreshScene();
    RefreshModelingWorkspace();
    if (undoAction_)
        undoAction_->setEnabled(!viewportEditBefore_.isEmpty() || undo_->canUndo());
    if (redoAction_)
        redoAction_->setEnabled(!viewportEditBefore_.isEmpty() || undo_->canRedo());
    const LuxUI::String name = document_.GetPath().empty()
                                   ? LUX_UI_TEXT("새 장면")
                                   : LuxUI::FileInfo(Q(document_.GetPath())).fileName();
    const LuxUI::String projectName =
        project_ ? Q(project_->GetName()) + LUX_UI_TEXT(" — ") : LuxUI::String();
    setWindowTitle(LUX_UI_TEXT("LuxEditor — ") + projectName + name +
                   (document_.IsDirty() ? LUX_UI_TEXT(" *") : LuxUI::String()));
    RefreshSceneTabs();
    projectLabel_->setText(project_ ? Q(project_->GetName()) : LUX_UI_TEXT("프로젝트 없음"));
    if (sceneStatus_)
        sceneStatus_->setText(
            LUX_UI_TEXT("%1개 오브젝트  ·  %2%3")
                .arg(document_.GetScene().GetEntityCount())
                .arg(viewportTabs_->currentWidget() == gameCanvas_ ? LUX_UI_TEXT("Game")
                     : viewportTabs_->currentWidget() == canvas3D_ ? LUX_UI_TEXT("3D")
                                                                   : LUX_UI_TEXT("2D"))
                .arg(document_.IsDirty() ? LUX_UI_TEXT("  ·  저장되지 않음") : LuxUI::String()));
    for (LuxUI::Action* action : {projectSettingsAction_, cloneAction_, exportAction_,
                                  backupAction_, restoreAction_, rescanAction_})
        if (action)
            action->setEnabled(project_.has_value());
}

void EditorWindow::AddEntity() {
    RecordEdit(LUX_UI_TEXT("2D 엔티티 추가"), [this] {
        LuxEngine::Entity entity = document_.CreateEntity(
            "Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<LuxEngine::SpriteRendererComponent>();
    });
    RefreshAll();
}

void EditorWindow::AddMeshEntity() {
    RecordEdit(LUX_UI_TEXT("3D 메시 엔티티 추가"), [this] {
        LuxEngine::Entity entity = document_.CreateEntity(
            "3D Entity " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<LuxEngine::MeshRendererComponent>();
    });
    viewportTabs_->setCurrentIndex(0);
    RefreshAll();
}

void EditorWindow::AddTilemap() {
    RecordEdit(LUX_UI_TEXT("타일맵 추가"), [this] {
        LuxEngine::Entity entity = document_.CreateEntity(
            "Tilemap " + std::to_string(document_.GetScene().GetEntityCount() + 1));
        entity.AddComponent<LuxEngine::TilemapComponent>().Resize(8, 8);
    });
    viewportTabs_->setCurrentIndex(1);
    SetTool(EditorTool::TileBrush);
    RefreshAll();
}

void EditorWindow::PaintTile(LuxEngine::uint32 id, LuxEngine::uint32 x, LuxEngine::uint32 y,
                             uint8_t tile) {
    auto* map = document_.GetScene().TryGetComponent<LuxEngine::TilemapComponent>(id);
    if (!map || map->Get(x, y) == tile || !map->Set(x, y, tile))
        return;
    document_.MarkDirty();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::DeleteEntity() {
    const auto selected = document_.GetSelected();
    if (!selected)
        return;
    const auto ids = document_.GetSelection();
    RecordEdit(LUX_UI_TEXT("엔티티 삭제"), [this, ids] {
        auto& scene = document_.GetScene();
        for (const auto id : ids)
            scene.DestroyEntity({id, &scene});
        document_.Select(std::nullopt);
    });
    RefreshAll();
}

void EditorWindow::OnNameChanged() {
    if (refreshingInspector_)
        return;
    LuxEngine::Entity selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::TagComponent>())
        return;
    const std::string value = Utf8(name_->text());
    if (selected.GetComponent<LuxEngine::TagComponent>().tag == value)
        return;
    if (value.size() > 256) {
        Log(LUX_UI_TEXT("엔티티 이름은 UTF-8 기준 256바이트까지 입력할 수 있습니다."), true);
        RefreshInspector();
        return;
    }
    RecordEdit(LUX_UI_TEXT("이름 변경"), [this, value] {
        document_.GetSelected().GetComponent<LuxEngine::TagComponent>().tag = value;
        document_.MarkDirty();
    });
    RefreshHierarchy();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnTransformChanged(int row, int axis) {
    if (refreshingInspector_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::TransformComponent>())
        return;
    // Display values can be rounded or clamped; write only the field that the user edited.
    RecordEdit(LUX_UI_TEXT("변환 변경"), [this, row, axis] {
        const auto edited = static_cast<float>(transform_[row][axis]->value());
        for (const auto id : document_.GetSelection()) {
            auto* value = document_.GetScene().TryGetComponent<LuxEngine::TransformComponent>(id);
            if (!value)
                continue;
            auto& vector = row == 0 ? value->position : row == 1 ? value->rotation : value->scale;
            const std::array<float*, 3> components{&vector.x, &vector.y, &vector.z};
            *components[axis] = row == 1 ? LuxEngine::ToRadians(edited) : edited;
        }
        document_.MarkDirty();
    });
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnSpriteChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(LUX_UI_TEXT("스프라이트 변경"), [this] {
        LuxEngine::Entity selected = document_.GetSelected();
        if (spriteEnabled_->isChecked() &&
            !selected.HasComponent<LuxEngine::SpriteRendererComponent>())
            selected.AddComponent<LuxEngine::SpriteRendererComponent>();
        else if (!spriteEnabled_->isChecked() &&
                 selected.HasComponent<LuxEngine::SpriteRendererComponent>())
            selected.RemoveComponent<LuxEngine::SpriteRendererComponent>();
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnColorChanged(int channel) {
    if (refreshingInspector_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::SpriteRendererComponent>())
        return;
    RecordEdit(LUX_UI_TEXT("색상 변경"), [this, channel] {
        auto& color =
            document_.GetSelected().GetComponent<LuxEngine::SpriteRendererComponent>().color;
        const std::array<float*, 4> components{&color.x, &color.y, &color.z, &color.w};
        *components[channel] = static_cast<float>(color_[channel]->value());
        document_.MarkDirty();
    });
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnMeshChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(LUX_UI_TEXT("메시 변경"), [this, field] {
        LuxEngine::Entity selected = document_.GetSelected();
        if (field == meshEnabled_) {
            if (meshEnabled_->isChecked() &&
                !selected.HasComponent<LuxEngine::MeshRendererComponent>())
                selected.AddComponent<LuxEngine::MeshRendererComponent>();
            else if (!meshEnabled_->isChecked() &&
                     selected.HasComponent<LuxEngine::MeshRendererComponent>())
                selected.RemoveComponent<LuxEngine::MeshRendererComponent>();
        } else if (selected.HasComponent<LuxEngine::MeshRendererComponent>()) {
            auto& material = selected.GetComponent<LuxEngine::MeshRendererComponent>().material;
            if (field == metallic_)
                material.metallic = static_cast<float>(metallic_->value());
            else if (field == roughness_)
                material.roughness = static_cast<float>(roughness_->value());
            else if (field == materialAlphaMode_)
                material.alphaMode =
                    static_cast<LuxEngine::MaterialAlphaMode>(materialAlphaMode_->currentIndex());
            else if (field == materialAlphaCutoff_)
                material.alphaCutoff = static_cast<float>(materialAlphaCutoff_->value());
            else if (field == materialDoubleSided_)
                material.doubleSided = materialDoubleSided_->isChecked();
            else if (field == materialNormalScale_)
                material.normalScale = static_cast<float>(materialNormalScale_->value());
            else if (field == materialOcclusionStrength_)
                material.occlusionStrength =
                    static_cast<float>(materialOcclusionStrength_->value());
            else if (field == materialMipmaps_)
                material.textureSampling.mipmaps = materialMipmaps_->isChecked();
            else if (field == materialAddressMode_)
                material.textureSampling.addressMode = static_cast<LuxEngine::TextureAddressMode>(
                    materialAddressMode_->currentIndex());
            else if (field == materialAnisotropic_)
                material.textureSampling.anisotropic = materialAnisotropic_->isChecked();
            const std::array<std::string*, 4> paths{
                &material.normalTexture, &material.metallicTexture, &material.roughnessTexture,
                &material.occlusionTexture};
            for (size_t i = 0; i < paths.size(); ++i) {
                if (field != materialMapPaths_[i])
                    continue;
                const auto path = materialMapPaths_[i]->text().toUtf8().toStdString();
                if (path.size() > 4096 || path.find('\0') != std::string::npos)
                    return;
                if (!path.empty() && !LuxEngine::AssetManager::ValidateTextureFile(
                                         document_.GetScene().ResolveAssetPath(path)))
                    return;
                *paths[i] = path;
            }
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas3D_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnTilemapChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(LUX_UI_TEXT("타일맵 변경"), [this, field] {
        LuxEngine::Entity entity = document_.GetSelected();
        if (field == tilemapEnabled_) {
            if (tilemapEnabled_->isChecked() && !entity.HasComponent<LuxEngine::TilemapComponent>())
                entity.AddComponent<LuxEngine::TilemapComponent>();
            else if (!tilemapEnabled_->isChecked() &&
                     entity.HasComponent<LuxEngine::TilemapComponent>())
                entity.RemoveComponent<LuxEngine::TilemapComponent>();
        } else if (entity.HasComponent<LuxEngine::TilemapComponent>()) {
            auto& map = entity.GetComponent<LuxEngine::TilemapComponent>();
            if (field == tilemapWidth_)
                map.Resize(static_cast<LuxEngine::uint32>(tilemapWidth_->value()), map.height);
            else if (field == tilemapHeight_)
                map.Resize(map.width, static_cast<LuxEngine::uint32>(tilemapHeight_->value()));
            else if (field == tilemapSize_)
                map.tileSize = static_cast<float>(tilemapSize_->value());
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::OnPhysicsChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    RecordEdit(LUX_UI_TEXT("2D 물리 변경"), [this, field] {
        LuxEngine::Entity entity = document_.GetSelected();
        if (field == bodyEnabled_) {
            if (bodyEnabled_->isChecked() &&
                !entity.HasComponent<LuxEngine::Rigidbody2DComponent>())
                entity.AddComponent<LuxEngine::Rigidbody2DComponent>();
            else if (!bodyEnabled_->isChecked() &&
                     entity.HasComponent<LuxEngine::Rigidbody2DComponent>())
                entity.RemoveComponent<LuxEngine::Rigidbody2DComponent>();
        } else if (entity.HasComponent<LuxEngine::Rigidbody2DComponent>()) {
            auto& body = entity.GetComponent<LuxEngine::Rigidbody2DComponent>();
            if (field == bodyType_)
                body.type = bodyType_->currentIndex() == 0   ? LuxEngine::BodyType::Static
                            : bodyType_->currentIndex() == 1 ? LuxEngine::BodyType::Dynamic
                                                             : LuxEngine::BodyType::Kinematic;
            else if (field == gravityScale_)
                body.gravityScale = static_cast<float>(gravityScale_->value());
        }
        if (field == colliderEnabled_) {
            if (colliderEnabled_->isChecked() &&
                !entity.HasComponent<LuxEngine::BoxCollider2DComponent>())
                entity.AddComponent<LuxEngine::BoxCollider2DComponent>();
            else if (!colliderEnabled_->isChecked() &&
                     entity.HasComponent<LuxEngine::BoxCollider2DComponent>())
                entity.RemoveComponent<LuxEngine::BoxCollider2DComponent>();
        } else if (entity.HasComponent<LuxEngine::BoxCollider2DComponent>()) {
            auto& collider = entity.GetComponent<LuxEngine::BoxCollider2DComponent>();
            if (field == colliderSize_[0])
                collider.size.x = static_cast<float>(colliderSize_[0]->value());
            else if (field == colliderSize_[1])
                collider.size.y = static_cast<float>(colliderSize_[1]->value());
            else if (field == colliderOffset_[0])
                collider.offset.x = static_cast<float>(colliderOffset_[0]->value());
            else if (field == colliderOffset_[1])
                collider.offset.y = static_cast<float>(colliderOffset_[1]->value());
            else if (field == triggerEnabled_)
                collider.isTrigger = triggerEnabled_->isChecked();
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    canvas_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::StartPreview() {
    CancelViewportEdit();
    if (previewActive_)
        return;
    StopAnimationPlayback();
    // Authoring scenes and undo history stay alive; physics and scripts own a disposable clone.
    std::string serialized;
    auto runtime = std::make_unique<LuxEngine::Scene>();
    if (!document_.GetScene().SerializeToString(serialized) ||
        !runtime->DeserializeFromString(serialized)) {
        Log(LUX_UI_TEXT("게임 실행용 씬 복사본을 만들지 못했습니다."), true);
        return;
    }
    runtime->SetSourcePath(document_.GetScene().GetSourcePath());
    const auto scriptEntities = runtime->GetComponentEntityIds<LuxEngine::ScriptComponent>();
    const bool hasScripts =
        std::any_of(scriptEntities.begin(), scriptEntities.end(), [&runtime](auto id) {
            const auto& script = runtime->GetComponent<LuxEngine::ScriptComponent>(id);
            return script.enabled && !script.typeName.empty();
        });
    if (hasScripts) {
        LuxEngine::ScriptRuntimeConfig config;
        const auto projectFile = project_ ? project_->GetFile() : std::filesystem::path{};
        if (playAssemblyProject_ == projectFile)
            config.assemblyPath = playAssembly_;
        if (config.assemblyPath.empty() && project_)
            config.assemblyPath = project_->GetFile().parent_path() / "Scripts/GameScripts.dll";
        if (!previewScripts_.Initialize(*runtime, config)) {
            const auto error = previewScripts_.GetLastError();
            previewScripts_.Shutdown();
            Log(LuxUI::String::fromUtf8(error), true);
            return;
        }
    }
    runtimeScene_ = std::move(runtime);
    previewFrame_ = 0;
    lastUpdateMs_ = 0;
    previewActive_ = true;
    previewPaused_ = false;
    gameCanvas_->SetPaused(false);
    gameCanvas_->SetInputEnabled(true);
    gameCanvas_->RefreshScene();
    RefreshModelingWorkspace();
    viewportTabs_->setCurrentWidget(gameCanvas_);
    gameCanvas_->setFocus(LuxUI::Flags::OtherFocusReason);
    previewStartAction_->setEnabled(false);
    previewPauseAction_->setEnabled(true);
    previewStepAction_->setEnabled(true);
    previewStopAction_->setEnabled(true);
    previewTimer_.start();
    Log(LUX_UI_TEXT("게임 실행을 시작했습니다."));
}

void EditorWindow::StopPreview() {
    if (!previewActive_)
        return;
    previewTimer_.stop();
    previewActive_ = false;
    previewPaused_ = false;
    previewScripts_.Shutdown();
    gameCanvas_->SetInputEnabled(false);
    gameCanvas_->SetPaused(false);
    runtimeScene_.reset();
    previewStartAction_->setEnabled(true);
    previewPauseAction_->setEnabled(false);
    previewStepAction_->setEnabled(false);
    previewStopAction_->setEnabled(false);
    RefreshAll();
    Log(LUX_UI_TEXT("게임 실행을 중지했습니다."));
}

void EditorWindow::TogglePreviewPause() {
    if (!previewActive_)
        return;
    previewPaused_ = !previewPaused_;
    gameCanvas_->SetPaused(previewPaused_);
    if (previewPaused_)
        previewTimer_.stop();
    else
        previewTimer_.start();
}

void EditorWindow::StepPreview() {
    if (!previewActive_)
        StartPreview();
    if (!previewActive_)
        return;
    previewPaused_ = true;
    previewTimer_.stop();
    gameCanvas_->SetPaused(true);
    TickPreview();
}

void EditorWindow::TickPreview() {
    if (!previewActive_ || !runtimeScene_)
        return;
    auto& scene = *runtimeScene_;
    const auto start = std::chrono::steady_clock::now();
    try {
        if (previewScripts_.IsRunning() && !previewScripts_.Update(1.0 / 60.0))
            throw std::runtime_error(previewScripts_.GetLastError());
        scene.OnUpdate(1.0 / 60.0);
        LuxEngine::Input::AdvanceFrame();
    } catch (const std::exception& error) {
        previewPaused_ = true;
        previewTimer_.stop();
        gameCanvas_->SetPaused(true);
        Log(LUX_UI_TEXT("게임 실행 오류: %1 — 실행을 일시정지했습니다.")
                .arg(LuxUI::String::fromUtf8(error.what())),
            true);
        gameCanvas_->RefreshScene();
        return;
    }
    ++previewFrame_;
    lastUpdateMs_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto selected = SelectedId(document_);
    debugPanel_->UpdateScene(scene, selected.value_or(0), previewFrame_, lastUpdateMs_);
    for (const auto& event : scene.GetCollisionEvents2D())
        debugPanel_->AddLog(LUX_UI_TEXT("프레임 %1: #%2 ↔ #%3 %4")
                                .arg(previewFrame_)
                                .arg(event.entityA)
                                .arg(event.entityB)
                                .arg(event.isTrigger ? LUX_UI_TEXT("트리거") : LUX_UI_TEXT("충돌")),
                            false);
    for (const auto& event : scene.GetCollisionEvents3D())
        debugPanel_->AddLog(LUX_UI_TEXT("3D 프레임 %1: #%2 ↔ #%3 %4")
                                .arg(previewFrame_)
                                .arg(event.entityA)
                                .arg(event.entityB)
                                .arg(event.isTrigger ? LUX_UI_TEXT("트리거") : LUX_UI_TEXT("충돌")),
                            false);
    if (breakOnCollisionAction_->isChecked() &&
        (!scene.GetCollisionEvents2D().empty() || !scene.GetCollisionEvents3D().empty())) {
        previewPaused_ = true;
        previewTimer_.stop();
        gameCanvas_->SetPaused(true);
        Log(LUX_UI_TEXT("충돌 이벤트에서 게임 실행을 일시정지했습니다."));
    }
    gameCanvas_->RefreshScene();
}

void EditorWindow::SetTool(EditorTool tool) {
    if (workspaceStack_ && workspaceStack_->currentIndex() == 1) {
        SetAnimationTool(tool);
        return;
    }
    CancelViewportEdit();
    EndTimelineEdit();
    canvas_->CancelInteraction();
    overviewCanvas_->CancelInteraction();
    canvas3D_->CancelInteraction();
    tool_ = tool;
    if (tool == EditorTool::TileBrush || tool == EditorTool::TileErase)
        viewportTabs_->setCurrentIndex(1);
    for (size_t i = 0; i < toolButtons_.size(); ++i) {
        toolButtons_[i]->setChecked(i == static_cast<size_t>(tool));
        toolActions_[i]->setChecked(i == static_cast<size_t>(tool));
    }
    canvas_->SetTool(tool);
    canvas3D_->SetTool(tool);
    const std::array<LuxUI::String, 6> descriptions{
        LUX_UI_TEXT("선택"), LUX_UI_TEXT("이동"),    LUX_UI_TEXT("회전"),
        LUX_UI_TEXT("크기"), LUX_UI_TEXT("타일 붓"), LUX_UI_TEXT("타일 지우개")};
    toolHint_->setText(descriptions[static_cast<size_t>(tool)]);
    tilePalette_->setEnabled(tool == EditorTool::TileBrush);
}

EditorSnapshot EditorWindow::SnapshotScene() {
    std::string serialized;
    if (!document_.GetScene().SerializeToString(serialized))
        return {};
    EditorSnapshot snapshot;
    snapshot.scene = LuxUI::ByteArray(serialized.data(), static_cast<qsizetype>(serialized.size()));
    snapshot.selection = document_.GetSelection();
    for (LuxEngine::Entity entity : document_.GetScene().GetEntities())
        if (entity.HasComponent<LuxEngine::AnimationComponent>())
            snapshot.animationTimes.emplace_back(
                entity.GetId(), entity.GetComponent<LuxEngine::AnimationComponent>().currentTime);
    return snapshot;
}

void EditorWindow::RestoreScene(const EditorSnapshot& snapshot,
                                std::optional<LuxEngine::uint32> selected) {
    if (snapshot.isEmpty())
        return;
    const auto& bytes = snapshot.scene;
    const std::string serialized(bytes.constData(), static_cast<size_t>(bytes.size()));
    restoringHistory_ = true;
    const bool restored = document_.ReplaceSceneFromData(serialized, selected);
    if (restored) {
        document_.SetSelection(snapshot.selection, selected);
        for (const auto& [id, time] : snapshot.animationTimes)
            if (auto* animation =
                    document_.GetScene().TryGetComponent<LuxEngine::AnimationComponent>(id))
                animation->currentTime = time;
        RefreshAll();
    }
    restoringHistory_ = false;
    if (!restored)
        Log(LUX_UI_TEXT("장면 상태 복원에 실패했습니다."), true);
}

void EditorWindow::RecordEdit(const LuxUI::String& name, const std::function<void()>& edit) {
    if (restoringHistory_) {
        edit();
        return;
    }
    const auto tabId = document_.GetActiveTabId();
    CancelViewportEdit();
    const auto before = SnapshotScene();
    if (!document_.IsDirty() && !before.isEmpty())
        cleanScenes_[document_.GetActiveTabId()] = before;
    if (before.isEmpty()) {
        Log(LUX_UI_TEXT("편집 기록 실패 (%1): 편집 전 장면 스냅샷을 생성하지 못했습니다.")
                .arg(name),
            true);
        return;
    }
    const auto previous = SelectedId(document_);
    try {
        edit();
        if (document_.GetActiveTabId() != tabId)
            throw std::runtime_error("편집 도중 다른 씬으로 전환되었습니다.");
    } catch (const std::exception& error) {
        RestoreSceneAt(tabId, before, previous);
        Log(LUX_UI_TEXT("편집 실패 (%1): %2 — 이전 장면 상태로 복원했습니다.")
                .arg(name, LuxUI::String::fromUtf8(error.what())),
            true);
        return;
    } catch (...) {
        RestoreSceneAt(tabId, before, previous);
        Log(LUX_UI_TEXT("편집 실패 (%1): 알 수 없는 예외가 발생하여 이전 장면 상태로 복원했습니다.")
                .arg(name),
            true);
        return;
    }
    const auto after = SnapshotScene();
    if (after.isEmpty()) {
        RestoreSceneAt(tabId, before, previous);
        Log(LUX_UI_TEXT("편집 실패 (%1): 편집 후 장면 직렬화에 실패하여 이전 상태로 복원했습니다.")
                .arg(name),
            true);
        return;
    }
    const auto next = SelectedId(document_);
    if (before != after) {
        const auto tab = document_.GetActiveTabId();
        PushSceneCommand(new SceneEditCommand(
            name, [this, tab, before, previous] { RestoreSceneAt(tab, before, previous); },
            [this, tab, after, next] { RestoreSceneAt(tab, after, next); }, {tab},
            SceneHistoryBytes(before) + SceneHistoryBytes(after)));
    }
    UpdateSceneDirty(document_.GetActiveTabId(), after);
    UpdateChrome();
}

void EditorWindow::CopyEntity() {
    LuxEngine::Entity selected = document_.GetSelected();
    if (!selected)
        return;
    if (selected.HasComponent<LuxEngine::ScriptComponent>() ||
        document_.GetScene().GetParent(selected.GetId()) ||
        !document_.GetScene().GetChildren(selected.GetId()).empty() ||
        selected.HasComponent<LuxEngine::SceneReferenceComponent>() ||
        (selected.HasComponent<LuxEngine::HierarchyComponent>() &&
         selected.GetComponent<LuxEngine::HierarchyComponent>().group)) {
        LuxEngine::Scene subtree;
        const auto ids = subtree.ImportEntities(document_.GetScene(), {selected.GetId()});
        for (auto object : subtree.GetEntities())
            if (auto* mesh = object.TryGetComponent<LuxEngine::MeshRendererComponent>(); mesh)
                for (auto* resource :
                     {&mesh->meshPath, &mesh->material.baseColorTexture,
                      &mesh->material.normalTexture, &mesh->material.metallicTexture,
                      &mesh->material.roughnessTexture, &mesh->material.occlusionTexture})
                    if (!resource->empty())
                        *resource = document_.GetScene().ResolveAssetPath(*resource);
        std::string serialized;
        if (!ids.contains(selected.GetId()) || !subtree.SerializeToString(serialized)) {
            Log(LUX_UI_TEXT("계층을 복사하지 못했습니다."), true);
            return;
        }
        const auto payload =
            LuxUI::ByteArray::number(static_cast<qulonglong>(ids.at(selected.GetId()))) + '\n' +
            LuxUI::ByteArray(serialized.data(), static_cast<qsizetype>(serialized.size()));
        if (payload.size() > 64 * 1024 * 1024) {
            Log(LUX_UI_TEXT("복사할 계층이 허용 크기를 넘었습니다."), true);
            return;
        }
        auto* mime = new LuxUI::MimeData;
        mime->setData(LUX_UI_TEXT("application/x-lux-scene-subtree"), payload);
        LuxUI::Application::clipboard()->setMimeData(mime);
        return;
    }
    LuxUI::JsonObject object;
    object.insert(LUX_UI_TEXT("version"), 2);
    object.insert(LUX_UI_TEXT("hasTag"), selected.HasComponent<LuxEngine::TagComponent>());
    object.insert(LUX_UI_TEXT("hasTransform"),
                  selected.HasComponent<LuxEngine::TransformComponent>());
    if (selected.HasComponent<LuxEngine::TagComponent>())
        object.insert(LUX_UI_TEXT("name"), Q(selected.GetComponent<LuxEngine::TagComponent>().tag));
    if (selected.HasComponent<LuxEngine::TransformComponent>()) {
        const auto& t = selected.GetComponent<LuxEngine::TransformComponent>();
        object.insert(LUX_UI_TEXT("position"),
                      LuxUI::JsonArray{t.position.x, t.position.y, t.position.z});
        object.insert(LUX_UI_TEXT("rotation"),
                      LuxUI::JsonArray{t.rotation.x, t.rotation.y, t.rotation.z});
        object.insert(LUX_UI_TEXT("scale"), LuxUI::JsonArray{t.scale.x, t.scale.y, t.scale.z});
    }
    if (selected.HasComponent<LuxEngine::SpriteRendererComponent>()) {
        const auto& c = selected.GetComponent<LuxEngine::SpriteRendererComponent>().color;
        object.insert(LUX_UI_TEXT("sprite"), LuxUI::JsonArray{c.x, c.y, c.z, c.w});
    }
    if (selected.HasComponent<LuxEngine::MeshRendererComponent>()) {
        const auto& m = selected.GetComponent<LuxEngine::MeshRendererComponent>().material;
        const auto& c = m.albedoColor;
        LuxUI::JsonObject options;
        options.insert(LUX_UI_TEXT("alphaMode"), static_cast<int>(m.alphaMode));
        options.insert(LUX_UI_TEXT("alphaCutoff"), m.alphaCutoff);
        options.insert(LUX_UI_TEXT("doubleSided"), m.doubleSided);
        options.insert(LUX_UI_TEXT("normalScale"), m.normalScale);
        options.insert(LUX_UI_TEXT("occlusionStrength"), m.occlusionStrength);
        options.insert(LUX_UI_TEXT("mipmaps"), m.textureSampling.mipmaps);
        options.insert(LUX_UI_TEXT("addressMode"), static_cast<int>(m.textureSampling.addressMode));
        options.insert(LUX_UI_TEXT("anisotropic"), m.textureSampling.anisotropic);
        const std::array<std::pair<const char*, const std::string*>, 4> maps{
            {{"normalTexture", &m.normalTexture},
             {"metallicTexture", &m.metallicTexture},
             {"roughnessTexture", &m.roughnessTexture},
             {"occlusionTexture", &m.occlusionTexture}}};
        for (const auto& [name, path] : maps)
            options.insert(LuxUI::String::fromLatin1(name),
                           path->empty() ? LuxUI::String{}
                                         : Q(document_.GetScene().ResolveAssetPath(*path)));
        object.insert(LUX_UI_TEXT("materialOptions"), options);
        object.insert(LUX_UI_TEXT("mesh"),
                      LuxUI::JsonArray{m.metallic, m.roughness, c.x, c.y, c.z, c.w});
        const auto& path = selected.GetComponent<LuxEngine::MeshRendererComponent>().meshPath;
        if (!path.empty())
            object.insert(LUX_UI_TEXT("meshPath"), Q(document_.GetScene().ResolveAssetPath(path)));
    }
    if (selected.HasComponent<LuxEngine::MeshRendererComponent>()) {
        const auto& path =
            selected.GetComponent<LuxEngine::MeshRendererComponent>().material.baseColorTexture;
        if (!path.empty())
            object.insert(LUX_UI_TEXT("baseColorTexture"),
                          Q(document_.GetScene().ResolveAssetPath(path)));
    }
    if (selected.HasComponent<LuxEngine::CameraComponent>()) {
        const auto& cam = selected.GetComponent<LuxEngine::CameraComponent>();
        LuxUI::JsonObject camObj;
        camObj.insert(LUX_UI_TEXT("primary"), cam.isPrimary);
        camObj.insert(LUX_UI_TEXT("perspective"), cam.isPerspective);
        camObj.insert(LUX_UI_TEXT("fov"), cam.fov);
        camObj.insert(LUX_UI_TEXT("orthoSize"), cam.orthoSize);
        object.insert(LUX_UI_TEXT("camera"), camObj);
    }
    if (selected.HasComponent<LuxEngine::TilemapComponent>()) {
        const auto& map = selected.GetComponent<LuxEngine::TilemapComponent>();
        LuxUI::JsonObject mapObj;
        mapObj.insert(LUX_UI_TEXT("width"), static_cast<int>(map.width));
        mapObj.insert(LUX_UI_TEXT("height"), static_cast<int>(map.height));
        mapObj.insert(LUX_UI_TEXT("tileSize"), map.tileSize);
        LuxUI::JsonArray tilesArr;
        for (uint8_t tile : map.tiles)
            tilesArr.append(static_cast<int>(tile));
        mapObj.insert(LUX_UI_TEXT("tiles"), tilesArr);
        object.insert(LUX_UI_TEXT("tilemap"), mapObj);
    }
    if (selected.HasComponent<LuxEngine::Rigidbody2DComponent>()) {
        const auto& rb = selected.GetComponent<LuxEngine::Rigidbody2DComponent>();
        LuxUI::JsonObject rbObj;
        rbObj.insert(LUX_UI_TEXT("type"), static_cast<int>(rb.type));
        rbObj.insert(LUX_UI_TEXT("velocity"), LuxUI::JsonArray{rb.velocity.x, rb.velocity.y});
        rbObj.insert(LUX_UI_TEXT("mass"), rb.mass);
        rbObj.insert(LUX_UI_TEXT("gravityScale"), rb.gravityScale);
        object.insert(LUX_UI_TEXT("rigidbody2d"), rbObj);
    }
    if (selected.HasComponent<LuxEngine::BoxCollider2DComponent>()) {
        const auto& col = selected.GetComponent<LuxEngine::BoxCollider2DComponent>();
        LuxUI::JsonObject colObj;
        colObj.insert(LUX_UI_TEXT("size"), LuxUI::JsonArray{col.size.x, col.size.y});
        colObj.insert(LUX_UI_TEXT("offset"), LuxUI::JsonArray{col.offset.x, col.offset.y});
        colObj.insert(LUX_UI_TEXT("trigger"), col.isTrigger);
        object.insert(LUX_UI_TEXT("collider2d"), colObj);
    }
    if (selected.HasComponent<LuxEngine::Rigidbody3DComponent>()) {
        const auto& rb = selected.GetComponent<LuxEngine::Rigidbody3DComponent>();
        LuxUI::JsonObject rbObj;
        rbObj.insert(LUX_UI_TEXT("type"), static_cast<int>(rb.type));
        rbObj.insert(LUX_UI_TEXT("velocity"),
                     LuxUI::JsonArray{rb.velocity.x, rb.velocity.y, rb.velocity.z});
        rbObj.insert(LUX_UI_TEXT("mass"), rb.mass);
        rbObj.insert(LUX_UI_TEXT("gravityScale"), rb.gravityScale);
        object.insert(LUX_UI_TEXT("rigidbody3d"), rbObj);
    }
    if (selected.HasComponent<LuxEngine::BoxCollider3DComponent>()) {
        const auto& col = selected.GetComponent<LuxEngine::BoxCollider3DComponent>();
        LuxUI::JsonObject colObj;
        colObj.insert(LUX_UI_TEXT("size"), LuxUI::JsonArray{col.size.x, col.size.y, col.size.z});
        colObj.insert(LUX_UI_TEXT("offset"),
                      LuxUI::JsonArray{col.offset.x, col.offset.y, col.offset.z});
        colObj.insert(LUX_UI_TEXT("trigger"), col.isTrigger);
        object.insert(LUX_UI_TEXT("collider3d"), colObj);
    }
    if (selected.HasComponent<LuxEngine::AnimationComponent>()) {
        const auto& anim = selected.GetComponent<LuxEngine::AnimationComponent>();
        LuxUI::JsonObject animObj;
        animObj.insert(LUX_UI_TEXT("name"), Q(anim.clip.name));
        animObj.insert(LUX_UI_TEXT("duration"), anim.clip.duration);
        animObj.insert(LUX_UI_TEXT("loop"), anim.clip.loop);
        animObj.insert(LUX_UI_TEXT("playing"), anim.playing);
        LuxUI::JsonArray keysArr;
        for (const auto& k : anim.clip.keyframes) {
            LuxUI::JsonObject kObj;
            kObj.insert(LUX_UI_TEXT("time"), k.time);
            kObj.insert(LUX_UI_TEXT("position"),
                        LuxUI::JsonArray{k.position.x, k.position.y, k.position.z});
            kObj.insert(LUX_UI_TEXT("rotation"),
                        LuxUI::JsonArray{k.rotation.x, k.rotation.y, k.rotation.z});
            kObj.insert(LUX_UI_TEXT("scale"), LuxUI::JsonArray{k.scale.x, k.scale.y, k.scale.z});
            keysArr.append(kObj);
        }
        animObj.insert(LUX_UI_TEXT("keyframes"), keysArr);
        object.insert(LUX_UI_TEXT("animation"), animObj);
    }
    auto* mime = new LuxUI::MimeData;
    mime->setData(LUX_UI_TEXT("application/x-lux-entity"),
                  LuxUI::JsonDocument(object).toJson(LuxUI::JsonDocument::Compact));
    LuxUI::Application::clipboard()->setMimeData(mime);
    Log(LUX_UI_TEXT("엔티티를 복사했습니다."));
}

namespace {
constexpr qsizetype kMaxClipboardJsonBytes = 1024 * 1024;
constexpr float kMaxClipboardCoord = 100000.0f;
constexpr float kMaxClipboardMass = 1000000.0f;
constexpr float kMaxClipboardGravityScale = 10000.0f;
constexpr float kMaxClipboardDuration = 86400.0f;

bool IsStrictValidUtf8(const LuxUI::ByteArray& bytes) {
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

bool ParseFiniteFloat(const LuxUI::JsonValue& val, float& out, float minVal = -kMaxClipboardCoord,
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

bool ParseExactInt(const LuxUI::JsonValue& val, int& out, int minVal, int maxVal) {
    if (!val.isDouble())
        return false;
    const double d = val.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < static_cast<double>(minVal) ||
        d > static_cast<double>(maxVal))
        return false;
    out = static_cast<int>(d);
    return true;
}

bool ParseVec2(const LuxUI::JsonValue& val, LuxEngine::Vec2& out,
               float minVal = -kMaxClipboardCoord, float maxVal = kMaxClipboardCoord) {
    if (!val.isArray())
        return false;
    const auto arr = val.toArray();
    if (arr.size() != 2)
        return false;
    return ParseFiniteFloat(arr[0], out.x, minVal, maxVal) &&
           ParseFiniteFloat(arr[1], out.y, minVal, maxVal);
}

bool ParseVec3(const LuxUI::JsonValue& val, LuxEngine::Vec3& out,
               float minVal = -kMaxClipboardCoord, float maxVal = kMaxClipboardCoord) {
    if (!val.isArray())
        return false;
    const auto arr = val.toArray();
    if (arr.size() != 3)
        return false;
    return ParseFiniteFloat(arr[0], out.x, minVal, maxVal) &&
           ParseFiniteFloat(arr[1], out.y, minVal, maxVal) &&
           ParseFiniteFloat(arr[2], out.z, minVal, maxVal);
}

bool ParseVec4(const LuxUI::JsonValue& val, LuxEngine::Vec4& out,
               float minVal = -kMaxClipboardCoord, float maxVal = kMaxClipboardCoord) {
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
    bool hasTag = true;
    bool hasTransform = true;
    LuxUI::String name;
    LuxEngine::TransformComponent transform;
    std::optional<LuxEngine::SpriteRendererComponent> sprite;
    std::optional<LuxEngine::MeshRendererComponent> mesh;
    std::optional<LuxEngine::CameraComponent> camera;
    std::optional<LuxEngine::TilemapComponent> tilemap;
    std::optional<LuxEngine::Rigidbody2DComponent> rigidbody2d;
    std::optional<LuxEngine::BoxCollider2DComponent> collider2d;
    std::optional<LuxEngine::Rigidbody3DComponent> rigidbody3d;
    std::optional<LuxEngine::BoxCollider3DComponent> collider3d;
    std::optional<LuxEngine::AnimationComponent> animation;
};

bool ValidateClipboardJson(const LuxUI::JsonObject& object, ValidatedClipboardEntity& parsed,
                           LuxUI::String& errorMsg) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const LuxUI::String& key = it.key();
        if (key != LUX_UI_TEXT("name") && key != LUX_UI_TEXT("position") &&
            key != LUX_UI_TEXT("rotation") && key != LUX_UI_TEXT("scale") &&
            key != LUX_UI_TEXT("sprite") && key != LUX_UI_TEXT("mesh") &&
            key != LUX_UI_TEXT("camera") && key != LUX_UI_TEXT("tilemap") &&
            key != LUX_UI_TEXT("rigidbody2d") && key != LUX_UI_TEXT("collider2d") &&
            key != LUX_UI_TEXT("rigidbody3d") && key != LUX_UI_TEXT("collider3d") &&
            key != LUX_UI_TEXT("animation") && key != LUX_UI_TEXT("meshPath") &&
            key != LUX_UI_TEXT("baseColorTexture") && key != LUX_UI_TEXT("materialOptions") &&
            key != LUX_UI_TEXT("version") && key != LUX_UI_TEXT("hasTag") &&
            key != LUX_UI_TEXT("hasTransform")) {
            errorMsg = LUX_UI_TEXT("알 수 없는 클립보드 필드가 포함되어 있습니다: %1").arg(key);
            return false;
        }
    }

    if ((object.contains(LUX_UI_TEXT("meshPath")) ||
         object.contains(LUX_UI_TEXT("baseColorTexture")) ||
         object.contains(LUX_UI_TEXT("materialOptions"))) &&
        !object.contains(LUX_UI_TEXT("mesh"))) {
        errorMsg = LUX_UI_TEXT("메시 컴포넌트가 없는 자산 경로는 붙여넣을 수 없습니다.");
        return false;
    }
    if (object.contains(LUX_UI_TEXT("version"))) {
        const auto version = object.value(LUX_UI_TEXT("version"));
        if (!version.isDouble() || version.toDouble() != 2 ||
            !object.value(LUX_UI_TEXT("hasTag")).isBool() ||
            !object.value(LUX_UI_TEXT("hasTransform")).isBool()) {
            errorMsg = LUX_UI_TEXT("클립보드 버전 또는 컴포넌트 존재 정보가 올바르지 않습니다.");
            return false;
        }
        parsed.hasTag = object.value(LUX_UI_TEXT("hasTag")).toBool();
        parsed.hasTransform = object.value(LUX_UI_TEXT("hasTransform")).toBool();
    } else if (object.contains(LUX_UI_TEXT("hasTag")) ||
               object.contains(LUX_UI_TEXT("hasTransform"))) {
        errorMsg = LUX_UI_TEXT("클립보드 컴포넌트 존재 정보에 버전이 없습니다.");
        return false;
    }
    if ((!parsed.hasTag && object.contains(LUX_UI_TEXT("name"))) ||
        (!parsed.hasTransform &&
         (object.contains(LUX_UI_TEXT("position")) || object.contains(LUX_UI_TEXT("rotation")) ||
          object.contains(LUX_UI_TEXT("scale"))))) {
        errorMsg = LUX_UI_TEXT("클립보드 컴포넌트 존재 정보와 값이 일치하지 않습니다.");
        return false;
    }
    if (parsed.hasTag) {
        const LuxUI::JsonValue nameVal = object.value(LUX_UI_TEXT("name"));
        if (!nameVal.isString()) {
            errorMsg = LUX_UI_TEXT("엔티티 이름(name) 필드가 누락되었거나 문자열이 아닙니다.");
            return false;
        }
        parsed.name = nameVal.toString().trimmed();
        if (parsed.name.isEmpty() || parsed.name.size() > 256 ||
            parsed.name.contains(LuxUI::Char::ReplacementCharacter)) {
            errorMsg = LUX_UI_TEXT("엔티티 이름이 비어 있거나 허용 길이를 초과했습니다.");
            return false;
        }
    }
    if (parsed.hasTransform) {
        if (!object.contains(LUX_UI_TEXT("position")) ||
            !object.contains(LUX_UI_TEXT("rotation")) || !object.contains(LUX_UI_TEXT("scale"))) {
            errorMsg =
                LUX_UI_TEXT("필수 Transform 필드(position, rotation, scale)가 누락되었습니다.");
            return false;
        }
        if (!ParseVec3(object.value(LUX_UI_TEXT("position")), parsed.transform.position) ||
            !ParseVec3(object.value(LUX_UI_TEXT("rotation")), parsed.transform.rotation) ||
            !ParseVec3(object.value(LUX_UI_TEXT("scale")), parsed.transform.scale)) {
            errorMsg = LUX_UI_TEXT("Transform 수치가 유효하지 않거나 허용 범위를 벗어났습니다.");
            return false;
        }
        if (std::abs(parsed.transform.scale.x) < 1e-5f ||
            std::abs(parsed.transform.scale.y) < 1e-5f ||
            std::abs(parsed.transform.scale.z) < 1e-5f) {
            errorMsg = LUX_UI_TEXT("Transform 크기는 0이 될 수 없습니다.");
            return false;
        }
    }
    if (object.contains(LUX_UI_TEXT("sprite"))) {
        LuxEngine::SpriteRendererComponent comp;
        if (!ParseVec4(object.value(LUX_UI_TEXT("sprite")), comp.color, 0.0f, 100.0f)) {
            errorMsg = LUX_UI_TEXT("스프라이트 색상 데이터가 유효하지 않습니다.");
            return false;
        }
        parsed.sprite = comp;
    }

    if (object.contains(LUX_UI_TEXT("mesh"))) {
        const LuxUI::JsonValue meshVal = object.value(LUX_UI_TEXT("mesh"));
        if (!meshVal.isArray()) {
            errorMsg = LUX_UI_TEXT("메시 데이터가 배열 형식이 아닙니다.");
            return false;
        }
        const auto meshArr = meshVal.toArray();
        if (meshArr.size() != 2 && meshArr.size() != 6) {
            errorMsg = LUX_UI_TEXT("메시 배열 길이가 올바르지 않습니다.");
            return false;
        }
        LuxEngine::MeshRendererComponent comp;
        if (!ParseFiniteFloat(meshArr[0], comp.material.metallic, 0.0f, 1.0f) ||
            !ParseFiniteFloat(meshArr[1], comp.material.roughness, 0.0f, 1.0f)) {
            errorMsg = LUX_UI_TEXT("메시 재질 수치가 허용 범위(0~1)를 벗어났습니다.");
            return false;
        }
        if (meshArr.size() == 6) {
            if (!ParseFiniteFloat(meshArr[2], comp.material.albedoColor.x, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[3], comp.material.albedoColor.y, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[4], comp.material.albedoColor.z, 0.0f, 100.0f) ||
                !ParseFiniteFloat(meshArr[5], comp.material.albedoColor.w, 0.0f, 100.0f)) {
                errorMsg = LUX_UI_TEXT("메시 알베도 색상이 유효하지 않습니다.");
                return false;
            }
        }
        if (object.contains(LUX_UI_TEXT("meshPath"))) {
            const auto path = object.value(LUX_UI_TEXT("meshPath"));
            if (!path.isString()) {
                errorMsg = LUX_UI_TEXT("메시 파일 경로가 문자열이 아닙니다.");
                return false;
            }
            comp.meshPath = path.toString().toUtf8().toStdString();
            if (comp.meshPath.size() > 4096 || comp.meshPath.find('\0') != std::string::npos) {
                errorMsg = LUX_UI_TEXT("메시 파일 경로가 올바르지 않습니다.");
                return false;
            }
        }
        if (object.contains(LUX_UI_TEXT("baseColorTexture"))) {
            const auto path = object.value(LUX_UI_TEXT("baseColorTexture"));
            if (!path.isString()) {
                errorMsg = LUX_UI_TEXT("기본 색상 텍스처 경로가 문자열이 아닙니다.");
                return false;
            }
            comp.material.baseColorTexture = path.toString().toUtf8().toStdString();
            if (comp.material.baseColorTexture.size() > 4096 ||
                comp.material.baseColorTexture.find('\0') != std::string::npos) {
                errorMsg = LUX_UI_TEXT("기본 색상 텍스처 경로가 올바르지 않습니다.");
                return false;
            }
        }
        if (object.contains(LUX_UI_TEXT("materialOptions"))) {
            const auto raw = object.value(LUX_UI_TEXT("materialOptions"));
            if (!raw.isObject()) {
                errorMsg = LUX_UI_TEXT("재질 옵션이 객체가 아닙니다.");
                return false;
            }
            const auto options = raw.toObject();
            const auto mode = options.value(LUX_UI_TEXT("alphaMode"));
            const auto address = options.value(LUX_UI_TEXT("addressMode"));
            if (!mode.isDouble() || mode.toDouble() < 0 || mode.toDouble() > 2 ||
                mode.toDouble() != mode.toInt() || !address.isDouble() || address.toDouble() < 0 ||
                address.toDouble() > 1 || address.toDouble() != address.toInt() ||
                !options.value(LUX_UI_TEXT("doubleSided")).isBool() ||
                !options.value(LUX_UI_TEXT("mipmaps")).isBool() ||
                !options.value(LUX_UI_TEXT("anisotropic")).isBool() ||
                !ParseFiniteFloat(options.value(LUX_UI_TEXT("alphaCutoff")),
                                  comp.material.alphaCutoff, 0, 1) ||
                !ParseFiniteFloat(options.value(LUX_UI_TEXT("normalScale")),
                                  comp.material.normalScale, 0, 16) ||
                !ParseFiniteFloat(options.value(LUX_UI_TEXT("occlusionStrength")),
                                  comp.material.occlusionStrength, 0, 1)) {
                errorMsg = LUX_UI_TEXT("재질 옵션의 형식 또는 범위가 올바르지 않습니다.");
                return false;
            }
            comp.material.alphaMode = static_cast<LuxEngine::MaterialAlphaMode>(mode.toInt());
            comp.material.doubleSided = options.value(LUX_UI_TEXT("doubleSided")).toBool();
            comp.material.textureSampling.mipmaps = options.value(LUX_UI_TEXT("mipmaps")).toBool();
            comp.material.textureSampling.addressMode =
                static_cast<LuxEngine::TextureAddressMode>(address.toInt());
            comp.material.textureSampling.anisotropic =
                options.value(LUX_UI_TEXT("anisotropic")).toBool();
            const std::array<std::pair<const char*, std::string*>, 4> maps{
                {{"normalTexture", &comp.material.normalTexture},
                 {"metallicTexture", &comp.material.metallicTexture},
                 {"roughnessTexture", &comp.material.roughnessTexture},
                 {"occlusionTexture", &comp.material.occlusionTexture}}};
            for (const auto& [name, path] : maps) {
                const auto value = options.value(LuxUI::String::fromLatin1(name));
                if (!value.isString()) {
                    errorMsg = LUX_UI_TEXT("재질 텍스처 경로가 문자열이 아닙니다.");
                    return false;
                }
                *path = value.toString().toUtf8().toStdString();
                if (path->size() > 4096 || path->find('\0') != std::string::npos) {
                    errorMsg = LUX_UI_TEXT("재질 텍스처 경로가 올바르지 않습니다.");
                    return false;
                }
            }
        }
        parsed.mesh = comp;
    }

    if (object.contains(LUX_UI_TEXT("camera"))) {
        const LuxUI::JsonValue camVal = object.value(LUX_UI_TEXT("camera"));
        if (!camVal.isObject()) {
            errorMsg = LUX_UI_TEXT("카메라 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto camObj = camVal.toObject();
        const LuxUI::JsonValue primVal = camObj.value(LUX_UI_TEXT("primary"));
        const LuxUI::JsonValue perspVal = camObj.value(LUX_UI_TEXT("perspective"));
        LuxEngine::CameraComponent comp;
        if (!primVal.isBool() || !perspVal.isBool() ||
            !ParseFiniteFloat(camObj.value(LUX_UI_TEXT("fov")), comp.fov, 0.0f, 180.0f) ||
            comp.fov <= 0.0f || comp.fov >= 180.0f ||
            !ParseFiniteFloat(camObj.value(LUX_UI_TEXT("orthoSize")), comp.orthoSize, 0.0f,
                              kMaxClipboardCoord) ||
            comp.orthoSize <= 0.0f) {
            errorMsg = LUX_UI_TEXT("카메라 FOV 또는 직교 크기가 유효 범위를 벗어났습니다.");
            return false;
        }
        comp.isPrimary = primVal.toBool();
        comp.isPerspective = perspVal.toBool();
        parsed.camera = comp;
    }

    if (object.contains(LUX_UI_TEXT("tilemap"))) {
        const LuxUI::JsonValue mapVal = object.value(LUX_UI_TEXT("tilemap"));
        if (!mapVal.isObject()) {
            errorMsg = LUX_UI_TEXT("타일맵 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto mapObj = mapVal.toObject();
        int w = 0, h = 0;
        float tileSize = 0.0f;
        if (!ParseExactInt(mapObj.value(LUX_UI_TEXT("width")), w, 1, 256) ||
            !ParseExactInt(mapObj.value(LUX_UI_TEXT("height")), h, 1, 256) ||
            !ParseFiniteFloat(mapObj.value(LUX_UI_TEXT("tileSize")), tileSize, 0.0f, 64.0f) ||
            tileSize <= 0.0f) {
            errorMsg = LUX_UI_TEXT("타일맵 크기(최대 256x256) 또는 타일 크기가 유효하지 않습니다.");
            return false;
        }
        const LuxUI::JsonValue tilesVal = mapObj.value(LUX_UI_TEXT("tiles"));
        if (!tilesVal.isArray()) {
            errorMsg = LUX_UI_TEXT("타일맵 tiles 배열이 누락되었습니다.");
            return false;
        }
        const auto tilesArr = tilesVal.toArray();
        if (tilesArr.size() != static_cast<qsizetype>(w * h)) {
            errorMsg = LUX_UI_TEXT("타일맵 셀 개수가 가로x세로 크기와 일치하지 않습니다.");
            return false;
        }
        LuxEngine::TilemapComponent comp;
        if (!comp.Resize(static_cast<LuxEngine::uint32>(w), static_cast<LuxEngine::uint32>(h))) {
            errorMsg = LUX_UI_TEXT("타일맵 할당에 실패했습니다.");
            return false;
        }
        comp.tileSize = tileSize;
        for (qsizetype i = 0; i < tilesArr.size(); ++i) {
            int tv = -1;
            if (!ParseExactInt(tilesArr[i], tv, 0, 5)) {
                errorMsg = LUX_UI_TEXT("타일맵 셀 값이 허용 범위(0~5)를 벗어났습니다.");
                return false;
            }
            comp.tiles[static_cast<size_t>(i)] = static_cast<uint8_t>(tv);
        }
        parsed.tilemap = std::move(comp);
    }

    if (object.contains(LUX_UI_TEXT("rigidbody2d"))) {
        const LuxUI::JsonValue rbVal = object.value(LUX_UI_TEXT("rigidbody2d"));
        if (!rbVal.isObject()) {
            errorMsg = LUX_UI_TEXT("2D 강체 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto rbObj = rbVal.toObject();
        int type = -1;
        LuxEngine::Rigidbody2DComponent comp;
        if (!ParseExactInt(rbObj.value(LUX_UI_TEXT("type")), type, 0, 2) ||
            !ParseVec2(rbObj.value(LUX_UI_TEXT("velocity")), comp.velocity) ||
            !ParseFiniteFloat(rbObj.value(LUX_UI_TEXT("mass")), comp.mass, 0.0f,
                              kMaxClipboardMass) ||
            comp.mass <= 0.0f ||
            !ParseFiniteFloat(rbObj.value(LUX_UI_TEXT("gravityScale")), comp.gravityScale,
                              -kMaxClipboardGravityScale, kMaxClipboardGravityScale)) {
            errorMsg = LUX_UI_TEXT("2D 강체 속성(질량, 속도, 중력 배율)이 유효하지 않습니다.");
            return false;
        }
        comp.type = static_cast<LuxEngine::BodyType>(type);
        parsed.rigidbody2d = comp;
    }

    if (object.contains(LUX_UI_TEXT("collider2d"))) {
        const LuxUI::JsonValue colVal = object.value(LUX_UI_TEXT("collider2d"));
        if (!colVal.isObject()) {
            errorMsg = LUX_UI_TEXT("2D 콜라이더 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto colObj = colVal.toObject();
        const LuxUI::JsonValue trigVal = colObj.value(LUX_UI_TEXT("trigger"));
        LuxEngine::BoxCollider2DComponent comp;
        if (!trigVal.isBool() ||
            !ParseVec2(colObj.value(LUX_UI_TEXT("size")), comp.size, 0.0f, kMaxClipboardCoord) ||
            comp.size.x <= 0.0f || comp.size.y <= 0.0f ||
            !ParseVec2(colObj.value(LUX_UI_TEXT("offset")), comp.offset)) {
            errorMsg = LUX_UI_TEXT("2D 콜라이더 크기 또는 오프셋이 유효하지 않습니다.");
            return false;
        }
        comp.isTrigger = trigVal.toBool();
        parsed.collider2d = comp;
    }

    if (object.contains(LUX_UI_TEXT("rigidbody3d"))) {
        const LuxUI::JsonValue rbVal = object.value(LUX_UI_TEXT("rigidbody3d"));
        if (!rbVal.isObject()) {
            errorMsg = LUX_UI_TEXT("3D 강체 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto rbObj = rbVal.toObject();
        int type = -1;
        LuxEngine::Rigidbody3DComponent comp;
        if (!ParseExactInt(rbObj.value(LUX_UI_TEXT("type")), type, 0, 2) ||
            !ParseVec3(rbObj.value(LUX_UI_TEXT("velocity")), comp.velocity) ||
            !ParseFiniteFloat(rbObj.value(LUX_UI_TEXT("mass")), comp.mass, 0.0f,
                              kMaxClipboardMass) ||
            comp.mass <= 0.0f ||
            !ParseFiniteFloat(rbObj.value(LUX_UI_TEXT("gravityScale")), comp.gravityScale,
                              -kMaxClipboardGravityScale, kMaxClipboardGravityScale)) {
            errorMsg = LUX_UI_TEXT("3D 강체 속성(질량, 속도, 중력 배율)이 유효하지 않습니다.");
            return false;
        }
        comp.type = static_cast<LuxEngine::BodyType>(type);
        parsed.rigidbody3d = comp;
    }

    if (object.contains(LUX_UI_TEXT("collider3d"))) {
        const LuxUI::JsonValue colVal = object.value(LUX_UI_TEXT("collider3d"));
        if (!colVal.isObject()) {
            errorMsg = LUX_UI_TEXT("3D 콜라이더 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto colObj = colVal.toObject();
        const LuxUI::JsonValue trigVal = colObj.value(LUX_UI_TEXT("trigger"));
        LuxEngine::BoxCollider3DComponent comp;
        if (!trigVal.isBool() ||
            !ParseVec3(colObj.value(LUX_UI_TEXT("size")), comp.size, 0.0f, kMaxClipboardCoord) ||
            comp.size.x <= 0.0f || comp.size.y <= 0.0f || comp.size.z <= 0.0f ||
            !ParseVec3(colObj.value(LUX_UI_TEXT("offset")), comp.offset)) {
            errorMsg = LUX_UI_TEXT("3D 콜라이더 크기 또는 오프셋이 유효하지 않습니다.");
            return false;
        }
        comp.isTrigger = trigVal.toBool();
        parsed.collider3d = comp;
    }

    if (object.contains(LUX_UI_TEXT("animation"))) {
        const LuxUI::JsonValue animVal = object.value(LUX_UI_TEXT("animation"));
        if (!animVal.isObject()) {
            errorMsg = LUX_UI_TEXT("애니메이션 컴포넌트가 객체 형식이 아닙니다.");
            return false;
        }
        const auto animObj = animVal.toObject();
        const LuxUI::JsonValue clipNameVal = animObj.value(LUX_UI_TEXT("name"));
        const LuxUI::JsonValue loopVal = animObj.value(LUX_UI_TEXT("loop"));
        const LuxUI::JsonValue playingVal = animObj.value(LUX_UI_TEXT("playing"));
        const LuxUI::JsonValue keysVal = animObj.value(LUX_UI_TEXT("keyframes"));
        LuxEngine::AnimationComponent comp;
        if (!clipNameVal.isString() || !loopVal.isBool() || !playingVal.isBool() ||
            !keysVal.isArray() ||
            !ParseFiniteFloat(animObj.value(LUX_UI_TEXT("duration")), comp.clip.duration, 0.0f,
                              kMaxClipboardDuration) ||
            comp.clip.duration <= 0.0f) {
            errorMsg = LUX_UI_TEXT("애니메이션 클립 속성 또는 재생 시간이 유효하지 않습니다.");
            return false;
        }
        const LuxUI::String clipName = clipNameVal.toString().trimmed();
        if (clipName.isEmpty() || clipName.size() > 256 ||
            clipName.contains(LuxUI::Char::ReplacementCharacter)) {
            errorMsg = LUX_UI_TEXT("애니메이션 클립 이름이 유효하지 않습니다.");
            return false;
        }
        const auto keysArr = keysVal.toArray();
        if (keysArr.size() > 4096) {
            errorMsg = LUX_UI_TEXT("애니메이션 키프레임 수가 최대 허용치(4096개)를 초과했습니다.");
            return false;
        }
        comp.clip.name = clipName.toUtf8().toStdString();
        comp.clip.loop = loopVal.toBool();
        comp.playing = playingVal.toBool();
        for (const auto& kv : keysArr) {
            if (!kv.isObject()) {
                errorMsg = LUX_UI_TEXT("애니메이션 키프레임이 객체 형식이 아닙니다.");
                return false;
            }
            const auto kObj = kv.toObject();
            LuxEngine::Keyframe key;
            if (!ParseFiniteFloat(kObj.value(LUX_UI_TEXT("time")), key.time, 0.0f,
                                  kMaxClipboardDuration) ||
                key.time < 0.0f || !ParseVec3(kObj.value(LUX_UI_TEXT("position")), key.position) ||
                !ParseVec3(kObj.value(LUX_UI_TEXT("rotation")), key.rotation) ||
                !ParseVec3(kObj.value(LUX_UI_TEXT("scale")), key.scale) ||
                std::abs(key.scale.x) < 1e-5f || std::abs(key.scale.y) < 1e-5f ||
                std::abs(key.scale.z) < 1e-5f) {
                errorMsg = LUX_UI_TEXT("애니메이션 키프레임 수치가 유효하지 않습니다.");
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
    const auto* mime = LuxUI::Application::clipboard()->mimeData();
    if (mime && mime->hasFormat(LUX_UI_TEXT("application/x-lux-scene-subtree"))) {
        const auto payload = mime->data(LUX_UI_TEXT("application/x-lux-scene-subtree"));
        const auto newline = payload.indexOf('\n');
        bool valid = false;
        const auto root = payload.left(newline).toUInt(&valid);
        auto subtree = std::make_shared<LuxEngine::Scene>();
        if (payload.size() > 64 * 1024 * 1024 || newline <= 0 || newline > 10 || !valid ||
            root == 0 ||
            !subtree->DeserializeFromString(
                std::string(payload.constData() + newline + 1,
                            static_cast<size_t>(payload.size() - newline - 1))) ||
            !subtree->IsAlive(root)) {
            Log(LUX_UI_TEXT("붙여넣을 계층 데이터가 유효하지 않습니다."), true);
            return;
        }
        RecordEdit(LUX_UI_TEXT("계층 붙여넣기"), [this, subtree, root] {
            const auto ids = document_.GetScene().ImportEntities(*subtree, {root});
            if (!ids.contains(root))
                throw std::runtime_error("계층을 붙여넣지 못했습니다.");
            const auto id = ids.at(root);
            if (auto* tag = document_.GetScene().TryGetComponent<LuxEngine::TagComponent>(id)) {
                const auto name = tag->tag + " 복사본";
                if (name.size() <= 256)
                    tag->tag = name;
            }
            document_.Select(id);
        });
        RefreshAll();
        return;
    }
    if (!mime || !mime->hasFormat(LUX_UI_TEXT("application/x-lux-entity")))
        return;
    const LuxUI::ByteArray rawBytes = mime->data(LUX_UI_TEXT("application/x-lux-entity"));
    if (rawBytes.isEmpty() || rawBytes.size() > kMaxClipboardJsonBytes) {
        Log(LUX_UI_TEXT(
                "붙여넣기 실패: 클립보드 데이터가 비어 있거나 허용 크기(1MB)를 초과했습니다."),
            true);
        return;
    }
    if (!IsStrictValidUtf8(rawBytes)) {
        Log(LUX_UI_TEXT(
                "붙여넣기 실패: 클립보드 데이터에 잘못된 UTF-8 인코딩이 포함되어 있습니다."),
            true);
        return;
    }
    LuxUI::JsonParseError parse;
    const auto doc = LuxUI::JsonDocument::fromJson(rawBytes, &parse);
    if (parse.error != LuxUI::JsonParseError::NoError || !doc.isObject()) {
        Log(LUX_UI_TEXT("붙여넣기 실패: 클립보드 JSON 구문이 손상되었습니다."), true);
        return;
    }
    ValidatedClipboardEntity validated;
    LuxUI::String validationError;
    if (!ValidateClipboardJson(doc.object(), validated, validationError)) {
        Log(LUX_UI_TEXT("붙여넣기 실패: %1").arg(validationError), true);
        return;
    }

    RecordEdit(LUX_UI_TEXT("엔티티 붙여넣기"), [this, validated = std::move(validated)] {
        LuxUI::String name = validated.name + LUX_UI_TEXT(" 복사본");
        if (name.toUtf8().size() > 256)
            name = validated.name;
        LuxEngine::Entity entity = document_.CreateEntity(Utf8(name));
        try {
            if (!validated.hasTag)
                entity.RemoveComponent<LuxEngine::TagComponent>();
            if (validated.hasTransform)
                entity.GetComponent<LuxEngine::TransformComponent>() = validated.transform;
            else
                entity.RemoveComponent<LuxEngine::TransformComponent>();
            if (validated.sprite)
                entity.AddComponent<LuxEngine::SpriteRendererComponent>(*validated.sprite);
            if (validated.mesh)
                entity.AddComponent<LuxEngine::MeshRendererComponent>(*validated.mesh);
            if (validated.camera)
                entity.AddComponent<LuxEngine::CameraComponent>(*validated.camera);
            if (validated.tilemap)
                entity.AddComponent<LuxEngine::TilemapComponent>(*validated.tilemap);
            if (validated.rigidbody2d)
                entity.AddComponent<LuxEngine::Rigidbody2DComponent>(*validated.rigidbody2d);
            if (validated.collider2d)
                entity.AddComponent<LuxEngine::BoxCollider2DComponent>(*validated.collider2d);
            if (validated.rigidbody3d)
                entity.AddComponent<LuxEngine::Rigidbody3DComponent>(*validated.rigidbody3d);
            if (validated.collider3d)
                entity.AddComponent<LuxEngine::BoxCollider3DComponent>(*validated.collider3d);
            if (validated.animation)
                entity.AddComponent<LuxEngine::AnimationComponent>(*validated.animation);

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

EditorDocument& EditorWindow::GetDocument() noexcept {
    return document_;
}

const EditorDocument& EditorWindow::GetDocument() const noexcept {
    return document_;
}

LuxEngine::Scene* EditorWindow::GetRuntimeScene() noexcept {
    return runtimeScene_.get();
}

const LuxEngine::Scene* EditorWindow::GetRuntimeScene() const noexcept {
    return runtimeScene_.get();
}

const LuxUI::String& EditorWindow::LastLogMessage() const noexcept {
    return lastLogMessage_;
}

void EditorWindow::ExecuteRecordedEdit(const LuxUI::String& name,
                                       const std::function<void()>& edit) {
    RecordEdit(name, edit);
    RefreshAll();
}

size_t EditorWindow::PendingSceneLoadCount() const noexcept {
    return pendingSceneLoads_.size();
}
} // namespace LuxEditor
