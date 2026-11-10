#include "EditorWindow.hpp"
#include "Project/PathEncoding.hpp"
#include "Scene/SceneAuthoringIO.hpp"
#include "Scene/SceneDependencies.hpp"
#include "Scene/SceneHistory.hpp"
#include "Scene/SceneSettingsDialog.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include "Viewport/SceneViewport3D.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>

namespace LuxEditor {
namespace {
QString Q(const std::filesystem::path& path) {
    return QString::fromUtf8(PathUtf8(path));
}
QString Q(const std::string& text) {
    return QString::fromUtf8(text);
}
bool SameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
    if (left.empty() || right.empty())
        return false;
    std::error_code ec;
    const auto a = std::filesystem::weakly_canonical(left, ec);
    if (ec)
        return false;
    const auto b = std::filesystem::weakly_canonical(right, ec);
    return !ec && a == b;
}
QString SceneName(const EditorDocument::TabInfo& tab) {
    return tab.path.empty() ? QStringLiteral("새 씬 %1").arg(tab.id)
                            : QFileInfo(Q(tab.path)).fileName();
}
} // namespace

void EditorWindow::BuildSceneMenus() {
    auto* menu = menuBar()->addMenu(QStringLiteral("씬"));
    menu->setObjectName(QStringLiteral("sceneMenu"));
    const auto add = [&](const QString& label, const char* name, auto callback) {
        auto* action = menu->addAction(label);
        action->setObjectName(QString::fromLatin1(name));
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add(QStringLiteral("모든 씬 저장"), "saveAllScenes", [this] { SaveAllScenes(); });
    add(QStringLiteral("씬 닫기 / 언로드"), "closeScene",
        [this] { RequestCloseScene(document_.GetActiveTabId()); });
    add(QStringLiteral("씬 복제..."), "duplicateScene", [this] { DuplicateScene(); });
    add(QStringLiteral("씬 파일 삭제..."), "deleteSceneFile", [this] { DeleteSceneFile(); });
    add(QStringLiteral("시작 씬으로 지정"), "setStartupScene", [this] { SetStartupScene(); });
    add(QStringLiteral("시작 씬 실행"), "runStartupScene", [this] {
        if (project_ && OpenSceneDocument(project_->GetSceneFile()))
            RunSceneInWindow();
    });
    menu->addSeparator();
    add(QStringLiteral("비동기 씬 열기..."), "openSceneAsync", [this] {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("씬 열기"), {},
                                                       QStringLiteral("Lux 씬 (*.lux)"));
        if (!file.isEmpty())
            LoadSceneAsync(PathFromUtf8(file.toUtf8().toStdString()), false);
    });
    add(QStringLiteral("씬 프리로드..."), "preloadScene", [this] {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("씬 프리로드"), {},
                                                       QStringLiteral("Lux 씬 (*.lux)"));
        if (!file.isEmpty())
            LoadSceneAsync(PathFromUtf8(file.toUtf8().toStdString()), true);
    });
    add(QStringLiteral("프리로드 캐시 비우기"), "unloadSceneCache",
        [this] { sceneManager_.ClearCache(); });
    menu->addSeparator();
    add(QStringLiteral("씬 중첩 / 인스턴스 추가..."), "instanceScene",
        [this] { AddSceneReference(LuxEngine::SceneReferenceMode::Nested); });
    add(QStringLiteral("씬 상속..."), "inheritScene",
        [this] { AddSceneReference(LuxEngine::SceneReferenceMode::Inherited); });
    add(QStringLiteral("Additive 씬 로드..."), "additiveScene",
        [this] { AddSceneReference(LuxEngine::SceneReferenceMode::Additive); });
    add(QStringLiteral("참조 씬 갱신"), "refreshSceneReferences",
        [this] { RefreshSceneReferences(); });
    add(QStringLiteral("선택 참조 씬 경로 변경..."), "relinkSceneReference",
        [this] { RelinkSceneReference(); });
    add(QStringLiteral("선택 참조 씬 다시 로드"), "loadSceneReference", [this] {
        const auto selected = document_.GetSelected();
        if (!selected || !selected.HasComponent<LuxEngine::SceneReferenceComponent>())
            return;
        const auto root = selected.GetId();
        RecordEdit(QStringLiteral("참조 씬 다시 로드"), [this, root] {
            std::string error;
            if (!sceneManager_.RefreshInstance(document_.GetScene(), root, error))
                throw std::runtime_error(error);
        });
        RefreshAll();
    });
    add(QStringLiteral("선택 참조 씬 언로드"), "unloadSceneReference", [this] {
        const auto selected = document_.GetSelected();
        if (!selected || !selected.HasComponent<LuxEngine::SceneReferenceComponent>())
            return;
        const auto root = selected.GetId();
        RecordEdit(QStringLiteral("참조 씬 언로드"), [this, root] {
            std::string error;
            if (!sceneManager_.UnloadInstance(document_.GetScene(), root, error))
                throw std::runtime_error(error);
        });
        RefreshAll();
    });
    add(QStringLiteral("선택 씬 스트리밍..."), "configureSceneStreaming",
        [this] { ConfigureSceneStreaming(); });
    menu->addSeparator();
    add(QStringLiteral("선택 항목 그룹화"), "groupEntities", [this] { GroupSelection(); });
    add(QStringLiteral("그룹 해제"), "ungroupEntities", [this] { UngroupSelection(); });
    add(QStringLiteral("부모 연결 해제"), "detachEntity", [this] {
        const auto selected = document_.GetSelected();
        if (selected)
            ReparentSelection({selected.GetId()}, 0);
    });
    add(QStringLiteral("다른 씬으로 이동..."), "moveEntityToScene",
        [this] { ChooseMoveDestination(); });
    menu->addSeparator();
    add(QStringLiteral("씬 설정..."), "sceneSettings", [this] { EditSceneSettings(); });
    auto* transitions = add(QStringLiteral("씬 전환 효과"), "sceneTransitions", [] {});
    transitions->setCheckable(true);
    sceneTransitions_ = QSettings().value(QStringLiteral("sceneTransitions"), true).toBool();
    transitions->setChecked(sceneTransitions_);
    connect(transitions, &QAction::toggled, this, [this](bool enabled) {
        sceneTransitions_ = enabled;
        QSettings().setValue(QStringLiteral("sceneTransitions"), enabled);
    });
    for (const auto& [name, next] : std::array<std::pair<const char*, bool>, 2>{
             {{"nextScene", true}, {"previousScene", false}}}) {
        auto* action =
            add(next ? QStringLiteral("다음 씬") : QStringLiteral("이전 씬"), name, [this, next] {
                const auto tabs = document_.ListTabs();
                const auto current =
                    std::find_if(tabs.begin(), tabs.end(), [this](const auto& tab) {
                        return tab.id == document_.GetActiveTabId();
                    });
                const auto index = static_cast<size_t>(std::distance(tabs.begin(), current));
                SwitchSceneDocument(tabs[(index + (next ? 1 : tabs.size() - 1)) % tabs.size()].id);
            });
        action->setShortcut(next ? QKeySequence(Qt::CTRL | Qt::Key_Tab)
                                 : QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
    }
}

void EditorWindow::RefreshSceneTabs() {
    if (!sceneTabs_)
        return;
    refreshingSceneTabs_ = true;
    const QSignalBlocker block(sceneTabs_);
    const auto tabs = document_.ListTabs();
    while (sceneTabs_->count() > static_cast<int>(tabs.size()))
        sceneTabs_->removeTab(sceneTabs_->count() - 1);
    for (size_t index = 0; index < tabs.size(); ++index) {
        const auto& tab = tabs[index];
        const auto label = SceneName(tab) + (tab.dirty ? QStringLiteral(" *") : QString());
        if (static_cast<int>(index) >= sceneTabs_->count())
            sceneTabs_->addTab(label);
        else
            sceneTabs_->setTabText(static_cast<int>(index), label);
        sceneTabs_->setTabData(static_cast<int>(index), QVariant::fromValue<qulonglong>(tab.id));
        sceneTabs_->setTabToolTip(static_cast<int>(index), Q(tab.path));
        if (tab.id == document_.GetActiveTabId())
            sceneTabs_->setCurrentIndex(static_cast<int>(index));
    }
    refreshingSceneTabs_ = false;
}

EditorDocument::TabId EditorWindow::NewSceneDocument() {
    StopPreview();
    EndTimelineEdit();
    EndViewportEdit();
    sceneProjects_[document_.GetActiveTabId()] = project_;
    const auto tab = document_.NewTab();
    sceneProjects_[tab] = project_;
    RememberSceneBaseline();
    RefreshAll();
    return tab;
}

bool EditorWindow::SwitchSceneDocument(EditorDocument::TabId id, bool animate) {
    if (!document_.GetScene(id))
        return false;
    if (id == document_.GetActiveTabId())
        return true;
    if (!restoringHistory_) {
        StopPreview();
        EndTimelineEdit();
        EndViewportEdit();
    } else {
        viewportEditBefore_.clear();
        timelineBefore_.clear();
    }
    canvas_->CancelInteraction();
    overviewCanvas_->CancelInteraction();
    canvas3D_->CancelInteraction();
    const QPixmap previous =
        animate && sceneTransitions_ && isVisible() ? viewportTabs_->grab() : QPixmap();
    sceneProjects_[document_.GetActiveTabId()] = project_;
    if (!document_.ActivateTab(id))
        return false;
    if (const auto context = sceneProjects_.find(id); context != sceneProjects_.end())
        project_ = context->second;
    ApplyProjectPreferences();
    RefreshAll();
    if (!previous.isNull())
        AnimateSceneTransition(previous);
    return true;
}

bool EditorWindow::OpenSceneDocument(const std::filesystem::path& path, bool asynchronous) {
    if (asynchronous) {
        LoadSceneAsync(path, false);
        return true;
    }
    return OpenProjectScene(path);
}

bool EditorWindow::CloseSceneDocument(EditorDocument::TabId id, bool discardDirty) {
    if (!document_.GetScene(id) || (document_.IsDirty(id) && !discardDirty))
        return false;
    StopPreview();
    EndTimelineEdit();
    EndViewportEdit();
    const auto path = document_.GetPath(id);
    if (!document_.CloseTab(id, discardDirty))
        return false;
    InvalidateSceneHistory(id);
    cleanScenes_.erase(id);
    autosavedAt_.erase(id);
    sceneProjects_.erase(id);
    if (!path.empty())
        sceneManager_.Unload(PathFromUtf8(path));
    const auto active = document_.GetActiveTabId();
    if (const auto context = sceneProjects_.find(active); context != sceneProjects_.end())
        project_ = context->second;
    else {
        sceneProjects_[active] = project_;
        RememberSceneBaseline();
    }
    ApplyProjectPreferences();
    RefreshAll();
    return true;
}

bool EditorWindow::RequestCloseScene(EditorDocument::TabId id) {
    if (!document_.GetScene(id))
        return false;
    if (document_.IsDirty(id)) {
        if (!SwitchSceneDocument(id, false))
            return false;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("씬 닫기"), QStringLiteral("변경 사항을 저장하시겠습니까?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !SaveScene(false)))
            return false;
    }
    return CloseSceneDocument(id, true);
}

void EditorWindow::ResetSceneWorkspace() {
    ++sceneLoadGeneration_;
    undo_->clear();
    cleanScenes_.clear();
    autosavedAt_.clear();
    sceneProjects_.clear();
    const auto active = document_.GetActiveTabId();
    for (const auto& tab : document_.ListTabs())
        if (tab.id != active)
            document_.CloseTab(tab.id, true);
    viewportEditBefore_.clear();
    timelineBefore_.clear();
    canvas_->CancelInteraction();
    overviewCanvas_->CancelInteraction();
    canvas3D_->CancelInteraction();
}

void EditorWindow::LoadSceneAsync(const std::filesystem::path& path, bool preload) {
    if (!preload) {
        if (const auto tab = document_.FindTabByPath(PathUtf8(path))) {
            SwitchSceneDocument(tab);
            return;
        }
    }
    for (const auto& pending : pendingSceneLoads_)
        if (pending.generation == sceneLoadGeneration_ && pending.preload == preload &&
            SameFile(path, pending.path))
            return;
    if (pendingSceneLoads_.size() >= 8) {
        Log(QStringLiteral("진행 중인 씬 로드가 끝난 후 다시 시도해 주세요."), true);
        return;
    }
    PendingSceneLoad request;
    request.path = path;
    request.project = project_ && project_->ContainsScene(path) ? project_ : std::nullopt;
    request.generation = sceneLoadGeneration_;
    request.origin = document_.GetActiveTabId();
    request.preload = preload;
    try {
        request.result = preload ? sceneManager_.PreloadAsync(path) : sceneManager_.LoadAsync(path);
    } catch (const std::exception& error) {
        Log(QStringLiteral("씬 로드를 시작하지 못했습니다: %1")
                .arg(QString::fromUtf8(error.what())),
            true);
        return;
    }
    pendingSceneLoads_.push_back(std::move(request));
    sceneLoadProgress_->setToolTip(
        QStringLiteral("씬 로드 %1개 진행 중").arg(pendingSceneLoads_.size()));
    sceneLoadProgress_->show();
    sceneLoadTimer_.start();
}

void EditorWindow::PollSceneLoads() {
    for (size_t index = 0; index < pendingSceneLoads_.size();) {
        if (pendingSceneLoads_[index].result.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            ++index;
            continue;
        }
        auto request = std::move(pendingSceneLoads_[index]);
        pendingSceneLoads_.erase(pendingSceneLoads_.begin() + static_cast<std::ptrdiff_t>(index));
        LuxEngine::SceneLoadResult result;
        try {
            result = request.result.get();
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        if (request.generation != sceneLoadGeneration_)
            continue;
        std::string warning;
        if (!result && !request.preload) {
            const auto originalError = result.error;
            result.scene = ReadAuthoredScene(request.path, result.error);
            if (result.scene)
                warning = originalError;
        }
        if (!result) {
            Log(QStringLiteral("씬 로드 실패: %1").arg(Q(result.error)), true);
            continue;
        }
        if (request.preload)
            continue;
        if (const auto existing = document_.FindTabByPath(PathUtf8(request.path))) {
            if (document_.GetActiveTabId() == request.origin && !QApplication::activeModalWidget())
                SwitchSceneDocument(existing);
            continue;
        }
        StopPreview();
        EndTimelineEdit();
        EndViewportEdit();
        sceneProjects_[document_.GetActiveTabId()] = project_;
        const auto previous = document_.GetActiveTabId();
        const bool foreground = previous == request.origin && !QApplication::activeModalWidget();
        const auto id = document_.AdoptTab(PathUtf8(request.path), std::move(result.scene), false,
                                           std::move(warning));
        if (!id) {
            Log(Q(document_.GetLastError()), true);
            continue;
        }
        project_ = request.project;
        sceneProjects_[id] = project_;
        if (!document_.GetLoadWarning().empty())
            Log(Q(document_.GetLoadWarning()), true);
        RememberSceneBaseline();
        if (!foreground) {
            document_.ActivateTab(previous);
            project_ = sceneProjects_[previous];
        }
        ApplyProjectPreferences();
        RefreshAll();
    }
    if (pendingSceneLoads_.empty()) {
        sceneLoadTimer_.stop();
        sceneLoadProgress_->hide();
    }
}

void EditorWindow::RememberSceneBaseline(bool clean) {
    const auto tab = document_.GetActiveTabId();
    if (clean) {
        const auto snapshot = SnapshotScene();
        if (!snapshot.isEmpty())
            cleanScenes_[tab] = snapshot;
        autosavedAt_[tab] = std::chrono::steady_clock::now();
    } else
        cleanScenes_.erase(tab);
}

void EditorWindow::UpdateSceneDirty(EditorDocument::TabId id, const EditorSnapshot& snapshot) {
    const auto baseline = cleanScenes_.find(id);
    document_.SetDirty(id,
                       baseline == cleanScenes_.end() || baseline->second.scene != snapshot.scene);
}

EditorSnapshot EditorWindow::SnapshotScene(EditorDocument::TabId id) {
    auto* scene = document_.GetScene(id);
    std::string serialized;
    if (!scene || !scene->SerializeToString(serialized))
        return {};
    EditorSnapshot snapshot;
    snapshot.scene = QByteArray(serialized.data(), static_cast<qsizetype>(serialized.size()));
    for (const auto& entity : scene->GetEntities())
        if (const auto* animation = entity.TryGetComponent<LuxEngine::AnimationComponent>())
            snapshot.animationTimes.emplace_back(entity.GetId(), animation->currentTime);
    return snapshot;
}

void EditorWindow::RestoreSceneAt(EditorDocument::TabId id, const EditorSnapshot& snapshot,
                                  std::optional<LuxEngine::uint32> selected, bool activate) {
    if (snapshot.isEmpty() || !document_.GetScene(id))
        return;
    const QScopedValueRollback<bool> restoring(restoringHistory_, true);
    const std::string data(snapshot.scene.constData(), static_cast<size_t>(snapshot.scene.size()));
    if (!document_.ReplaceSceneFromData(id, data, selected)) {
        Log(QStringLiteral("씬 편집 이력을 복원하지 못했습니다."), true);
        return;
    }
    auto* scene = document_.GetScene(id);
    for (const auto& [entity, time] : snapshot.animationTimes)
        if (auto* animation = scene->TryGetComponent<LuxEngine::AnimationComponent>(entity))
            animation->currentTime = time;
    UpdateSceneDirty(id, snapshot);
    if (activate)
        SwitchSceneDocument(id, false);
    RefreshAll();
}

void EditorWindow::InvalidateSceneHistory(EditorDocument::TabId id) {
    for (int index = 0; index < undo_->count(); ++index)
        if (auto* command =
                dynamic_cast<SceneEditCommand*>(const_cast<QUndoCommand*>(undo_->command(index)));
            command && command->Touches(id))
            command->Discard();
}

void EditorWindow::PushSceneCommand(SceneEditCommand* command) {
    constexpr size_t Budget = 256 * 1024 * 1024;
    size_t bytes = command->Bytes();
    for (int index = 0; index < undo_->index(); ++index)
        if (const auto* previous = dynamic_cast<const SceneEditCommand*>(undo_->command(index)))
            bytes += previous->Bytes();
    const auto limit = std::max(Budget, command->Bytes());
    for (int index = 0; index < undo_->index() && bytes > limit; ++index)
        if (auto* previous =
                dynamic_cast<SceneEditCommand*>(const_cast<QUndoCommand*>(undo_->command(index)))) {
            bytes -= previous->Bytes();
            previous->Discard();
        }
    undo_->push(command);
}

bool EditorWindow::SaveAllScenes(bool currentProjectOnly) {
    StopPreview();
    const auto original = document_.GetActiveTabId();
    const auto selectedProject = project_;
    for (const auto& tab : document_.ListTabs()) {
        if (!tab.dirty)
            continue;
        if (currentProjectOnly) {
            const auto context = sceneProjects_.find(tab.id);
            if (!selectedProject || context == sceneProjects_.end() || !context->second ||
                context->second->GetFile() != selectedProject->GetFile())
                continue;
        }
        SwitchSceneDocument(tab.id, false);
        if (!SaveScene(false)) {
            SwitchSceneDocument(original, false);
            return false;
        }
    }
    SwitchSceneDocument(original, false);
    return true;
}

void EditorWindow::DuplicateScene() {
    StopPreview();
    EndTimelineEdit();
    EndViewportEdit();
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("씬 복제"), {},
                                             QStringLiteral("Lux 씬 (*.lux)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".lux");
    const auto context = project_;
    const auto id = document_.DuplicateTab(document_.GetActiveTabId(), path.toUtf8().toStdString());
    if (!id) {
        ShowError(QStringLiteral("씬 복제 실패"), Q(document_.GetLastError()));
        return;
    }
    project_ = context && context->ContainsScene(PathFromUtf8(document_.GetPath())) ? context
                                                                                    : std::nullopt;
    sceneProjects_[id] = project_;
    ApplyProjectPreferences();
    RememberSceneBaseline();
    RefreshAll();
}

void EditorWindow::DeleteSceneFile() {
    const auto path = PathFromUtf8(document_.GetPath());
    if (!project_ || path.empty() || !project_->ContainsScene(path)) {
        Log(QStringLiteral("프로젝트에 저장된 씬을 선택해 주세요."), true);
        return;
    }
    if (SameFile(path, project_->GetSceneFile())) {
        Log(QStringLiteral("다른 시작 씬을 지정한 후 삭제해 주세요."), true);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("씬 파일 삭제"),
                              QStringLiteral("%1 파일을 삭제하시겠습니까?").arg(Q(path.filename())),
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    std::error_code error;
    if (!std::filesystem::remove(path, error)) {
        ShowError(QStringLiteral("씬 삭제 실패"),
                  Q(error ? error.message() : "씬 파일을 찾을 수 없습니다."));
        return;
    }
    project_->ClearAutosave(path);
    CloseSceneDocument(document_.GetActiveTabId(), true);
}

void EditorWindow::SetStartupScene() {
    if (!project_)
        return;
    if ((document_.GetPath().empty() || document_.IsDirty()) && !SaveScene(false))
        return;
    if (!project_) {
        Log(QStringLiteral("시작 씬은 프로젝트 안에 저장해 주세요."), true);
        return;
    }
    std::string error;
    if (!project_->SetStartScene(PathFromUtf8(document_.GetPath()), error)) {
        ShowError(QStringLiteral("시작 씬 지정 실패"), Q(error));
        return;
    }
    for (auto& [id, context] : sceneProjects_)
        if (context && context->GetFile() == project_->GetFile())
            context = project_;
    for (auto& pending : pendingSceneLoads_)
        if (pending.project && pending.project->GetFile() == project_->GetFile())
            pending.project = project_;
    RefreshAll();
}

void EditorWindow::EditSceneSettings() {
    if (previewActive_)
        return;
    const auto settings = ShowSceneSettingsDialog(this, document_.GetScene().GetSettings());
    if (!settings)
        return;
    RecordEdit(QStringLiteral("씬 설정"), [this, &settings] {
        if (!document_.GetScene().SetSettings(*settings))
            throw std::runtime_error("씬 설정 값이 유효하지 않습니다.");
    });
    RefreshAll();
}

void EditorWindow::AddSceneReference(LuxEngine::SceneReferenceMode mode) {
    if (previewActive_)
        return;
    const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("참조 씬 선택"), {},
                                                   QStringLiteral("Lux 씬 (*.lux)"));
    if (file.isEmpty())
        return;
    const auto path = PathFromUtf8(file.toUtf8().toStdString());
    if (SameFile(path, PathFromUtf8(document_.GetPath()))) {
        Log(QStringLiteral("씬 자체를 참조할 수 없습니다."), true);
        return;
    }
    if (project_) {
        std::string error;
        auto source = sceneManager_.Load(path, error);
        if (!project_->ContainsScene(path) || !source ||
            !ValidateProjectSceneReferences(*project_, *source, error)) {
            Log(error.empty() ? QStringLiteral("프로젝트 안의 씬을 선택해 주세요.") : Q(error),
                true);
            return;
        }
    }
    RecordEdit(QStringLiteral("씬 참조 추가"), [this, path, mode] {
        std::string error;
        const auto root = sceneManager_.AddSceneInstance(document_.GetScene(), path, mode, error);
        if (!root)
            throw std::runtime_error(error);
        document_.Select(root);
    });
    RefreshAll();
}

void EditorWindow::RefreshSceneReferences() {
    RecordEdit(QStringLiteral("참조 씬 갱신"), [this] {
        std::string error;
        if (!sceneManager_.RefreshInstances(document_.GetScene(), error))
            throw std::runtime_error(error);
    });
    RefreshAll();
}

void EditorWindow::RelinkSceneReference() {
    const auto selected = document_.GetSelected();
    if (previewActive_ || !selected || !selected.HasComponent<LuxEngine::SceneReferenceComponent>())
        return;
    const auto file = QFileDialog::getOpenFileName(
        this, QStringLiteral("참조 씬 경로 변경"),
        Q(selected.GetComponent<LuxEngine::SceneReferenceComponent>().path),
        QStringLiteral("Lux 씬 (*.lux)"));
    if (file.isEmpty())
        return;
    const auto path = PathFromUtf8(file.toUtf8().toStdString());
    if (SameFile(path, PathFromUtf8(document_.GetPath())) ||
        (project_ && !project_->ContainsScene(path))) {
        Log(QStringLiteral("참조할 다른 프로젝트 씬을 선택해 주세요."), true);
        return;
    }
    std::string error;
    auto source = sceneManager_.Load(path, error);
    if (!source || (project_ && !ValidateProjectSceneReferences(*project_, *source, error))) {
        Log(Q(error), true);
        return;
    }
    const auto root = selected.GetId();
    RecordEdit(QStringLiteral("참조 씬 경로 변경"), [this, root, path] {
        auto& scene = document_.GetScene();
        scene.GetComponent<LuxEngine::SceneReferenceComponent>(root).path =
            PathUtf8(std::filesystem::weakly_canonical(path));
        std::string error;
        if (!sceneManager_.RefreshInstance(scene, root, error))
            throw std::runtime_error(error);
    });
    RefreshAll();
}

void EditorWindow::ConfigureSceneStreaming() {
    const auto selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::SceneReferenceComponent>() || previewActive_)
        return;
    const auto reference = selected.GetComponent<LuxEngine::SceneReferenceComponent>();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("씬 스트리밍"));
    auto* layout = new QFormLayout(&dialog);
    auto number = [&](const QString& label, float value, float minimum, float maximum) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setDecimals(2);
        field->setRange(minimum, maximum);
        field->setValue(value);
        layout->addRow(label, field);
        return field;
    };
    auto* enabled = new QCheckBox(QStringLiteral("거리 기반 스트리밍"), &dialog);
    enabled->setChecked(reference.streamLoadRadius > 0);
    layout->addRow(enabled);
    const auto position = document_.GetScene().GetWorldTransform(selected.GetId()).position;
    auto* x = number(QStringLiteral("중심 X"),
                     reference.streamLoadRadius > 0 ? reference.streamCenter.x : position.x,
                     -100000, 100000);
    auto* y = number(QStringLiteral("중심 Y"),
                     reference.streamLoadRadius > 0 ? reference.streamCenter.y : position.y,
                     -100000, 100000);
    auto* load =
        number(QStringLiteral("로드 반경"),
               reference.streamLoadRadius > 0 ? reference.streamLoadRadius : 30, .1f, 100000);
    auto* unload =
        number(QStringLiteral("언로드 반경"),
               reference.streamUnloadRadius > 0 ? reference.streamUnloadRadius : 40, .1f, 100000);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto id = selected.GetId();
    const LuxEngine::Vec2 center{static_cast<float>(x->value()), static_cast<float>(y->value())};
    const float radius = enabled->isChecked() ? static_cast<float>(load->value()) : 0;
    const float outer = enabled->isChecked() ? static_cast<float>(unload->value()) : 0;
    RecordEdit(QStringLiteral("씬 스트리밍 설정"), [this, id, center, radius, outer] {
        std::string error;
        if (!sceneManager_.ConfigureStreaming(document_.GetScene(), id, center, radius, outer,
                                              error))
            throw std::runtime_error(error);
    });
    RefreshAll();
}

void EditorWindow::TickSceneStreaming() {
    if (previewActive_ || refreshingHierarchy_ || restoringHistory_)
        return;
    double x, y, height;
    canvas_->GetView(x, y, height);
    bool changed = false;
    std::string error;
    if (!sceneManager_.UpdateStreaming(document_.GetScene(),
                                       {static_cast<float>(x), static_cast<float>(y)}, error,
                                       &changed)) {
        // A failing source is reported once for its current configuration, never once per frame.
        if (Q(error) != property("streamingError").toString()) {
            setProperty("streamingError", Q(error));
            Log(Q(error), true);
        }
        return;
    }
    if (changed) {
        setProperty("streamingError", QString());
        RefreshAll();
    }
}

void EditorWindow::ReparentSelection(const std::vector<LuxEngine::uint32>& children,
                                     LuxEngine::uint32 parent) {
    RecordEdit(QStringLiteral("부모 변경"), [this, children, parent] {
        for (const auto id : children)
            if (!document_.GetScene().SetParent(id, parent, true))
                throw std::runtime_error("부모 관계 또는 변환을 적용할 수 없습니다.");
    });
    RefreshAll();
}

void EditorWindow::GroupSelection() {
    std::vector<LuxEngine::uint32> selected;
    for (auto* item : hierarchy_->selectedItems()) {
        bool descendant = false;
        for (auto* ancestor = item->parent(); ancestor; ancestor = ancestor->parent())
            if (ancestor->isSelected()) {
                descendant = true;
                break;
            }
        if (!descendant)
            selected.push_back(item->data(0, Qt::UserRole).toUInt());
    }
    if (selected.empty() && document_.GetSelected())
        selected.push_back(document_.GetSelected().GetId());
    if (selected.empty())
        return;
    RecordEdit(QStringLiteral("그룹 만들기"), [this, selected] {
        std::array<double, 3> center{};
        for (const auto id : selected) {
            const auto matrix = document_.GetScene().GetWorldMatrix(id);
            for (size_t axis = 0; axis < center.size(); ++axis)
                center[axis] += matrix.elements[12 + axis] / static_cast<double>(selected.size());
        }
        auto group = document_.GetScene().CreateEntity("그룹");
        group.GetComponent<LuxEngine::TransformComponent>().position = {
            static_cast<float>(center[0]), static_cast<float>(center[1]),
            static_cast<float>(center[2])};
        group.AddComponent<LuxEngine::HierarchyComponent>().group = true;
        for (const auto id : selected)
            if (!document_.GetScene().SetParent(id, group.GetId(), true))
                throw std::runtime_error("그룹으로 이동할 수 없습니다.");
        document_.Select(group.GetId());
    });
    RefreshAll();
}

void EditorWindow::UngroupSelection() {
    const auto selected = document_.GetSelected();
    if (!selected || !selected.HasComponent<LuxEngine::HierarchyComponent>() ||
        !selected.GetComponent<LuxEngine::HierarchyComponent>().group)
        return;
    const auto id = selected.GetId();
    RecordEdit(QStringLiteral("그룹 해제"), [this, id] {
        auto& scene = document_.GetScene();
        const auto parent = scene.GetParent(id);
        for (const auto child : scene.GetChildren(id))
            if (!scene.SetParent(child, parent, true))
                throw std::runtime_error("그룹 해제 변환을 적용할 수 없습니다.");
        scene.DestroyEntity({id, &scene});
        document_.Select(std::nullopt);
    });
    RefreshAll();
}

void EditorWindow::ChooseMoveDestination() {
    QStringList names;
    std::vector<EditorDocument::TabId> ids;
    for (const auto& tab : document_.ListTabs())
        if (tab.id != document_.GetActiveTabId()) {
            names.push_back(SceneName(tab) + QStringLiteral(" (#%1)").arg(tab.id));
            ids.push_back(tab.id);
        }
    if (names.isEmpty() || !document_.GetSelected())
        return;
    bool accepted = false;
    const auto name = QInputDialog::getItem(this, QStringLiteral("씬 간 이동"),
                                            QStringLiteral("대상 씬"), names, 0, false, &accepted);
    if (accepted)
        MoveSelectionToScene(ids[static_cast<size_t>(names.indexOf(name))]);
}

bool EditorWindow::MoveSelectionToScene(EditorDocument::TabId targetId) {
    const auto sourceId = document_.GetActiveTabId();
    const auto entity = document_.GetSelected();
    auto* target = document_.GetScene(targetId);
    if (previewActive_ || !entity || !target || sourceId == targetId)
        return false;
    EndViewportEdit();
    EndTimelineEdit();
    const auto sourceBefore = SnapshotScene(sourceId), targetBefore = SnapshotScene(targetId);
    if (sourceBefore.isEmpty() || targetBefore.isEmpty())
        return false;
    if (!cleanScenes_.contains(targetId) && !document_.IsDirty(targetId))
        cleanScenes_[targetId] = targetBefore;
    const auto sourceSelected = document_.GetSelectedId(sourceId),
               targetSelected = document_.GetSelectedId(targetId);
    const auto restore = [this, sourceId, targetId](const EditorSnapshot& source,
                                                    const EditorSnapshot& destination,
                                                    std::optional<LuxEngine::uint32> sourceEntity,
                                                    std::optional<LuxEngine::uint32> targetEntity) {
        RestoreSceneAt(sourceId, source, sourceEntity, false);
        RestoreSceneAt(targetId, destination, targetEntity, false);
    };
    LuxEngine::uint32 importedId = 0;
    try {
        const auto ids = target->ImportEntities(document_.GetScene(), {entity.GetId()});
        if (!ids.contains(entity.GetId()))
            throw std::runtime_error("씬 간 이동 복사에 실패했습니다.");
        importedId = ids.at(entity.GetId());
        document_.GetScene().DestroyEntity(entity);
        document_.Select(sourceId, std::nullopt);
        document_.Select(targetId, importedId);
    } catch (const std::exception& error) {
        restore(sourceBefore, targetBefore, sourceSelected, targetSelected);
        Log(Q(std::string(error.what())), true);
        return false;
    }
    const auto sourceAfter = SnapshotScene(sourceId), targetAfter = SnapshotScene(targetId);
    if (sourceAfter.isEmpty() || targetAfter.isEmpty()) {
        restore(sourceBefore, targetBefore, sourceSelected, targetSelected);
        return false;
    }
    PushSceneCommand(new SceneEditCommand(
        QStringLiteral("씬 간 오브젝트 이동"),
        [this, restore, sourceBefore, targetBefore, sourceSelected, targetSelected, sourceId] {
            restore(sourceBefore, targetBefore, sourceSelected, targetSelected);
            SwitchSceneDocument(sourceId, false);
        },
        [this, restore, sourceAfter, targetAfter, targetId, importedId] {
            restore(sourceAfter, targetAfter, std::nullopt, importedId);
            SwitchSceneDocument(targetId, false);
        },
        {sourceId, targetId},
        SceneHistoryBytes(sourceBefore) + SceneHistoryBytes(targetBefore) +
            SceneHistoryBytes(sourceAfter) + SceneHistoryBytes(targetAfter)));
    UpdateSceneDirty(sourceId, sourceAfter);
    UpdateSceneDirty(targetId, targetAfter);
    SwitchSceneDocument(targetId);
    RefreshAll();
    return true;
}

void EditorWindow::AnimateSceneTransition(const QPixmap& previous) {
    if (auto* old = viewportTabs_->findChild<QLabel*>(QStringLiteral("sceneTransition")))
        delete old;
    auto* overlay = new QLabel(viewportTabs_);
    overlay->setObjectName(QStringLiteral("sceneTransition"));
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->setGeometry(viewportTabs_->rect());
    overlay->setPixmap(previous);
    overlay->setScaledContents(true);
    auto* opacity = new QGraphicsOpacityEffect(overlay);
    overlay->setGraphicsEffect(opacity);
    auto* animation = new QPropertyAnimation(opacity, "opacity", overlay);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setDuration(160);
    connect(animation, &QPropertyAnimation::finished, overlay, &QLabel::deleteLater);
    overlay->show();
    overlay->raise();
    animation->start();
}
} // namespace LuxEditor
