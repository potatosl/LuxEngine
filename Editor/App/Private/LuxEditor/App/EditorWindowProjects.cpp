#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEditor/Project/ProjectDialog.hpp>
#include <LuxEditor/Scene/SceneDependencies.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>

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
} // namespace

void EditorWindow::NewProject() {
    auto request = ShowNewProjectDialog(this);
    if (!request || !ConfirmDiscardChanges())
        return;
    std::string error;
    auto created =
        EditorProject::Create(request->directory, request->name, request->projectTemplate, error);
    if (!created) {
        ShowError(LUX_UI_TEXT("프로젝트 생성 실패"), Q(error));
        return;
    }
    const auto previousTab = document_.GetActiveTabId();
    const auto newTab = document_.NewTab();
    if (!document_.Open(PathUtf8(created->GetSceneFile()))) {
        document_.CloseTab(newTab, true);
        document_.ActivateTab(previousTab);
        ShowError(LUX_UI_TEXT("프로젝트 열기 실패"),
                  LUX_UI_TEXT("생성한 시작 장면을 열 수 없습니다."));
        return;
    }
    ResetSceneWorkspace();
    project_ = std::move(created);
    sceneProjects_[document_.GetActiveTabId()] = project_;
    RememberSceneBaseline();
    ApplyProjectPreferences();
    recentProjects_.Add(project_->GetFile());
    UpdateRecentMenu();
    RefreshAll();
}

void EditorWindow::OpenProjectDialog() {
    const LuxUI::String file = LuxUI::FileDialog::getOpenFileName(
        this, LUX_UI_TEXT("프로젝트 열기"), {},
        LUX_UI_TEXT("Lux 프로젝트 (*.luxproject *.luxproj);;모든 파일 (*)"));
    if (!file.isEmpty())
        OpenProjectFile(PathFromUtf8(Utf8(file)));
}

void EditorWindow::OpenProjectFile(const std::filesystem::path& file,
                                   const std::filesystem::path& preferredScene) {
    std::string error;
    auto opened = EditorProject::OpenForRecovery(file, error);
    if (!opened) {
        ShowError(LUX_UI_TEXT("프로젝트 열기 실패"), Q(error));
        recentProjects_.Load();
        UpdateRecentMenu();
        return;
    }
    if (!ConfirmDiscardChanges())
        return;
    auto sceneFile = opened->GetSceneFile();
    if (!preferredScene.empty() && opened->ContainsScene(preferredScene)) {
        LuxEngine::Scene validation;
        if (validation.Deserialize(PathUtf8(preferredScene)) ||
            validation.Deserialize(PathUtf8(opened->GetAutosaveFile(preferredScene))))
            sceneFile = preferredScene;
    }
    EditorDocument candidate;
    const auto result = LoadSceneDocument(sceneFile, &*opened, &candidate);
    if (result == SceneOpenResult::Cancelled || result == SceneOpenResult::Failed) {
        if (result == SceneOpenResult::Failed)
            ShowError(LUX_UI_TEXT("프로젝트 열기 실패"), LUX_UI_TEXT("시작 씬을 열 수 없습니다."));
        return;
    }
    document_ = std::move(candidate);
    ResetSceneWorkspace();
    project_ = std::move(opened);
    sceneProjects_[document_.GetActiveTabId()] = project_;
    ApplyProjectPreferences();
    if (result == SceneOpenResult::Recovered) {
        undo_->resetClean();
        document_.MarkDirty();
    } else
        RecoverAutosaveIfNeeded();
    RememberSceneBaseline(!document_.IsDirty());
    recentProjects_.Add(project_->GetFile());
    UpdateRecentMenu();
    RefreshAll();
}

void EditorWindow::EditProjectSettings() {
    if (!project_)
        return;
    auto request = ShowProjectSettingsDialog(this, project_->GetSettings());
    if (!request)
        return;
    std::string error;
    if (!request->iconSource.empty() && !project_->SetIcon(request->iconSource, error)) {
        ShowError(LUX_UI_TEXT("프로젝트 설정 저장 실패"), Q(error));
        return;
    }
    if (!request->iconSource.empty())
        request->settings.icon = project_->GetIcon();
    if (!project_->UpdateSettings(request->settings, error)) {
        ShowError(LUX_UI_TEXT("프로젝트 설정 저장 실패"), Q(error));
        return;
    }
    for (auto& [id, context] : sceneProjects_)
        if (context && context->GetFile() == project_->GetFile())
            context = project_;
    for (auto& pending : pendingSceneLoads_)
        if (pending.project && pending.project->GetFile() == project_->GetFile())
            pending.project = project_;
    ApplyProjectPreferences();
    RefreshAll();
}

void EditorWindow::ApplyProjectPreferences() {
    autosaveTimer_.stop();
    uint32_t interval = project_ ? project_->GetSettings().autosaveSeconds : 0;
    for (const auto& [id, context] : sceneProjects_)
        if (context && context->GetSettings().autosaveSeconds > 0)
            interval = interval == 0 ? context->GetSettings().autosaveSeconds
                                     : std::min(interval, context->GetSettings().autosaveSeconds);
    if (interval > 0)
        autosaveTimer_.start(static_cast<int>(interval * 1000));
    if (!project_) {
        setWindowIcon(MakeBrandIcon(theme_ != LuxUI::Latin1String("light")));
        return;
    }
    if (!project_->GetIcon().empty())
        setWindowIcon(LuxUI::Icon(Q(project_->GetFile().parent_path() / project_->GetIcon())));
    else
        setWindowIcon(MakeBrandIcon(theme_ != LuxUI::Latin1String("light")));
}

EditorWindow::SceneOpenResult EditorWindow::LoadSceneDocument(const std::filesystem::path& file,
                                                              const EditorProject* recoveryProject,
                                                              EditorDocument* destination) {
    auto& target = destination ? *destination : document_;
    if (target.Open(PathUtf8(file))) {
        if (!target.GetLoadWarning().empty())
            Log(Q(target.GetLoadWarning()), true);
        return SceneOpenResult::Opened;
    }
    if (!recoveryProject)
        return SceneOpenResult::Failed;
    const auto autosave = recoveryProject->GetAutosaveFile(file);
    LuxEngine::Scene validation;
    if (autosave.empty() || !validation.Deserialize(PathUtf8(autosave)))
        return SceneOpenResult::Failed;
    const auto answer = LuxUI::MessageBox::question(
        this, LUX_UI_TEXT("장면 자동 저장 복구"),
        LUX_UI_TEXT("원본 장면을 읽을 수 없습니다. 자동 저장본으로 복구하시겠습니까?"),
        LuxUI::MessageBox::Yes | LuxUI::MessageBox::Cancel, LuxUI::MessageBox::Cancel);
    if (answer != LuxUI::MessageBox::Yes)
        return SceneOpenResult::Cancelled;
    return target.Recover(PathUtf8(file), PathUtf8(autosave)) ? SceneOpenResult::Recovered
                                                              : SceneOpenResult::Failed;
}

bool EditorWindow::OpenProjectScene(const std::filesystem::path& file) {
    if (const auto existing = document_.FindTabByPath(PathUtf8(file)))
        return SwitchSceneDocument(existing);
    StopPreview();
    EndTimelineEdit();
    CancelViewportEdit();
    const auto previousTab = document_.GetActiveTabId();
    sceneProjects_[previousTab] = project_;
    const bool owned = project_ && project_->ContainsScene(file);
    const auto context = owned ? project_ : std::nullopt;
    const bool placeholder = document_.ListTabs().size() == 1 && document_.GetPath().empty() &&
                             !document_.IsDirty() && document_.GetScene().GetEntityCount() == 0 &&
                             undo_->count() == 0;
    const auto nextTab = placeholder ? previousTab : document_.NewTab();
    const auto result = LoadSceneDocument(file, context ? &*context : nullptr);
    if (result == SceneOpenResult::Cancelled || result == SceneOpenResult::Failed) {
        if (!placeholder)
            document_.CloseTab(nextTab, true);
        document_.ActivateTab(previousTab);
        if (result == SceneOpenResult::Failed)
            ShowError(LUX_UI_TEXT("씬 열기 실패"), Q(document_.GetLastError()));
        return false;
    }
    project_ = context;
    sceneProjects_[nextTab] = context;
    ApplyProjectPreferences();
    if (result == SceneOpenResult::Recovered)
        undo_->resetClean();
    else
        RecoverAutosaveIfNeeded();
    RememberSceneBaseline(!document_.IsDirty());
    RefreshAll();
    return true;
}

void EditorWindow::RecoverAutosaveIfNeeded() {
    if (!project_ || !project_->HasNewerAutosave(PathFromUtf8(document_.GetPath())))
        return;
    const auto path = project_->GetAutosaveFile(PathFromUtf8(document_.GetPath()));
    const auto answer = LuxUI::MessageBox::question(
        this, LUX_UI_TEXT("장면 자동 저장 복구"),
        LUX_UI_TEXT("저장된 장면보다 새 자동 저장본이 있습니다. 복구하시겠습니까?"));
    if (answer == LuxUI::MessageBox::Yes) {
        if (!document_.Recover(document_.GetPath(), PathUtf8(path)))
            ShowError(LUX_UI_TEXT("복구 실패"), LUX_UI_TEXT("자동 저장본을 읽을 수 없습니다."));
        else
            undo_->resetClean();
    } else
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
}

void EditorWindow::AutoSaveScene() {
    if (!viewportEditBefore_.isEmpty())
        return;
    if (previewActive_)
        return;
    for (const auto& tab : document_.ListTabs()) {
        if (!tab.dirty || tab.path.empty())
            continue;
        const auto found = sceneProjects_.find(tab.id);
        const auto context =
            found == sceneProjects_.end() ? std::optional<EditorProject>{} : found->second;
        auto* scene = document_.GetScene(tab.id);
        if (!context || !scene || !context->ContainsScene(PathFromUtf8(tab.path)))
            continue;
        const auto seconds = context->GetSettings().autosaveSeconds;
        const auto now = std::chrono::steady_clock::now();
        const auto foundTime = autosavedAt_.find(tab.id);
        const auto previous = foundTime == autosavedAt_.end() ? openedAt_ : foundTime->second;
        if (seconds == 0 || now - previous < std::chrono::seconds(seconds))
            continue;
        std::string error;
        if (!context->SaveAutosave(PathFromUtf8(tab.path), *scene, error))
            Log(Q(error), true);
        else
            autosavedAt_[tab.id] = now;
    }
}

bool EditorWindow::ConfirmDiscardChanges() {
    StopPreview();
    EndTimelineEdit();
    CancelViewportEdit();
    const auto tabs = document_.ListTabs();
    const auto count =
        std::count_if(tabs.begin(), tabs.end(), [](const auto& tab) { return tab.dirty; });
    if (count == 0)
        return true;
    const auto answer = LuxUI::MessageBox::question(
        this, LUX_UI_TEXT("LuxEditor"),
        LUX_UI_TEXT("저장하지 않은 씬 %1개의 변경 사항을 저장하시겠습니까?").arg(count),
        LuxUI::MessageBox::Yes | LuxUI::MessageBox::No | LuxUI::MessageBox::Cancel,
        LuxUI::MessageBox::Yes);
    if (answer == LuxUI::MessageBox::Cancel)
        return false;
    return answer != LuxUI::MessageBox::Yes || SaveAllScenes();
}

bool EditorWindow::SaveScene(bool choosePath) {
    CancelViewportEdit();
    StopPreview();
    const auto previous = document_.GetPath();
    bool saved = false;
    if (choosePath || previous.empty()) {
        LuxUI::String file = LuxUI::FileDialog::getSaveFileName(
            this, LUX_UI_TEXT("장면 저장"), {}, LUX_UI_TEXT("Lux 장면 (*.lux);;모든 파일 (*)"));
        if (file.isEmpty())
            return false;
        if (LuxUI::FileInfo(file).suffix().isEmpty())
            file += LUX_UI_TEXT(".lux");
        saved = document_.SaveAs(Utf8(file));
    } else
        saved = document_.Save();
    if (!saved) {
        ShowError(LUX_UI_TEXT("저장 실패"), Q(document_.GetLastError()));
        return false;
    }
    RefreshProjectExplorer();
    if (project_) {
        if (!previous.empty())
            project_->ClearAutosave(PathFromUtf8(previous));
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
    }
    undo_->setClean();
    RememberSceneBaseline();
    if (project_ && !project_->ContainsScene(PathFromUtf8(document_.GetPath())))
        project_.reset();
    sceneProjects_[document_.GetActiveTabId()] = project_;
    RefreshSceneTabs();
    UpdateChrome();
    Log(LUX_UI_TEXT("장면을 저장했습니다."));
    return true;
}

void EditorWindow::CloneCurrentProject() {
    if (!project_ || !SaveAllScenes(true) || !project_)
        return;
    bool accepted = false;
    const LuxUI::String name = LuxUI::InputDialog::getText(
        this, LUX_UI_TEXT("프로젝트 복제"), LUX_UI_TEXT("복제할 프로젝트의 이름을 입력해 주세요."),
        LuxUI::LineEdit::Normal, Q(project_->GetName()) + LUX_UI_TEXT(" 복제"), &accepted);
    if (!accepted)
        return;
    auto destination = ShowProjectDestinationDialog(this, "프로젝트 복제", Utf8(name));
    if (!destination)
        return;
    std::string error;
    std::optional<EditorProject> copy;
    RunWithProgress(LUX_UI_TEXT("프로젝트를 복제하고 있습니다..."),
                    [&] { copy = CloneProject(*project_, *destination, Utf8(name), error); });
    if (!copy) {
        ShowError(LUX_UI_TEXT("프로젝트 복제 실패"), Q(error));
        return;
    }
    OpenProjectFile(copy->GetFile());
}

void EditorWindow::ImportProjectDialog() {
    const LuxUI::String file = LuxUI::FileDialog::getOpenFileName(
        this, LUX_UI_TEXT("프로젝트 가져오기"), {},
        LUX_UI_TEXT("Lux 프로젝트 패키지 (*.luxpack *.luxbackup)"));
    if (file.isEmpty())
        return;
    const auto archive = PathFromUtf8(Utf8(file));
    auto destination =
        ShowProjectDestinationDialog(this, "프로젝트 가져오기", PathUtf8(archive.stem()));
    if (!destination || !ConfirmDiscardChanges())
        return;
    std::string error;
    std::optional<EditorProject> imported;
    RunWithProgress(LUX_UI_TEXT("프로젝트를 가져오고 있습니다..."),
                    [&] { imported = ImportProject(archive, *destination, error); });
    if (!imported) {
        ShowError(LUX_UI_TEXT("프로젝트 가져오기 실패"), Q(error));
        return;
    }
    OpenProjectFile(imported->GetFile());
}

void EditorWindow::ExportCurrentProject() {
    if (!project_ || !SaveAllScenes(true) || !project_)
        return;
    std::string validationError;
    if (!ValidateProjectScenes(*project_, validationError)) {
        ShowError(LUX_UI_TEXT("씬 의존성 오류"), Q(validationError));
        return;
    }
    LuxUI::String file = LuxUI::FileDialog::getSaveFileName(
        this, LUX_UI_TEXT("프로젝트 내보내기"), Q(project_->GetName()) + LUX_UI_TEXT(".luxpack"),
        LUX_UI_TEXT("Lux 프로젝트 패키지 (*.luxpack)"));
    if (file.isEmpty())
        return;
    auto archive = PathFromUtf8(Utf8(file));
    if (archive.extension() != ".luxpack")
        archive += ".luxpack";
    std::string error;
    bool success = false;
    RunWithProgress(LUX_UI_TEXT("프로젝트 패키지를 만들고 있습니다..."),
                    [&] { success = ExportProject(*project_, archive, error); });
    if (!success) {
        ShowError(LUX_UI_TEXT("프로젝트 내보내기 실패"), Q(error));
        return;
    }
    Log(LUX_UI_TEXT("프로젝트 패키지를 내보냈습니다."));
}

void EditorWindow::BackupCurrentProject() {
    if (!project_ || !SaveAllScenes(true) || !project_)
        return;
    std::filesystem::path backup;
    std::string error;
    bool success = false;
    RunWithProgress(LUX_UI_TEXT("프로젝트를 백업하고 있습니다..."),
                    [&] { success = CreateProjectBackup(*project_, backup, error); });
    if (!success) {
        ShowError(LUX_UI_TEXT("프로젝트 백업 실패"), Q(error));
        return;
    }
    Log(LUX_UI_TEXT("프로젝트 백업을 만들었습니다."));
}

void EditorWindow::RestoreCurrentProject() {
    if (!project_)
        return;
    const auto directory = project_->GetFile().parent_path() / ".lux" / "backups";
    const LuxUI::String file =
        LuxUI::FileDialog::getOpenFileName(this, LUX_UI_TEXT("프로젝트 백업 복구"), Q(directory),
                                           LUX_UI_TEXT("Lux 백업 (*.luxbackup)"));
    if (file.isEmpty() || !ConfirmDiscardChanges() || !project_)
        return;
    if (LuxUI::MessageBox::question(this, LUX_UI_TEXT("프로젝트 백업 복구"),
                                    LUX_UI_TEXT("현재 프로젝트를 선택한 백업으로 복구하시겠습니까? "
                                                "복구 전 상태도 별도 백업합니다.")) !=
        LuxUI::MessageBox::Yes)
        return;
    const auto projectFile = project_->GetFile();
    std::string error;
    bool success = false;
    RunWithProgress(LUX_UI_TEXT("프로젝트를 복구하고 있습니다..."), [&] {
        success = RestoreProjectBackup(*project_, PathFromUtf8(Utf8(file)), error);
    });
    if (!success) {
        ShowError(LUX_UI_TEXT("프로젝트 백업 복구 실패"), Q(error));
        return;
    }
    auto restored = EditorProject::Open(projectFile, error);
    EditorDocument candidate;
    if (!restored || !candidate.Open(PathUtf8(restored->GetSceneFile()))) {
        ShowError(LUX_UI_TEXT("프로젝트 백업 복구 실패"),
                  error.empty() ? Q(candidate.GetLastError()) : Q(error));
        return;
    }
    document_ = std::move(candidate);
    ResetSceneWorkspace();
    project_ = std::move(restored);
    sceneProjects_[document_.GetActiveTabId()] = project_;
    RememberSceneBaseline();
    ApplyProjectPreferences();
    RefreshAll();
    Log(LUX_UI_TEXT("프로젝트를 백업에서 복구했습니다."));
}

void EditorWindow::RescanProjectResources() {
    if (!project_)
        return;
    std::string error;
    size_t count = 0;
    RunWithProgress(LUX_UI_TEXT("리소스를 재검색하고 있습니다..."),
                    [&] { count = project_->RescanResources(error); });
    if (!error.empty()) {
        ShowError(LUX_UI_TEXT("리소스 재검색 실패"), Q(error));
        return;
    }
    RefreshProjectExplorer();
    Log(LUX_UI_TEXT("리소스 %1개를 재검색했습니다.").arg(count));
}
} // namespace LuxEditor
