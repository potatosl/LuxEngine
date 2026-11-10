#include "EditorWindow.hpp"
#include "Project/PathEncoding.hpp"
#include "Project/ProjectArchive.hpp"
#include "Project/ProjectDialog.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>

namespace LuxEditor {
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
} // namespace
void EditorWindow::NewProject() {
    auto request = ShowNewProjectDialog(this);
    if (!request || !ConfirmDiscardChanges())
        return;
    std::string error;
    auto created =
        EditorProject::Create(request->directory, request->name, request->projectTemplate, error);
    if (!created) {
        ShowError(QStringLiteral("프로젝트 생성 실패"), Q(error));
        return;
    }
    if (!document_.Open(PathUtf8(created->GetSceneFile()))) {
        ShowError(QStringLiteral("프로젝트 열기 실패"),
                  QStringLiteral("생성한 시작 장면을 열 수 없습니다."));
        return;
    }
    project_ = std::move(created);
    undo_->clear();
    ApplyProjectPreferences();
    recentProjects_.Add(project_->GetFile());
    UpdateRecentMenu();
    RefreshAll();
}
void EditorWindow::OpenProjectDialog() {
    const QString file =
        QFileDialog::getOpenFileName(this, QStringLiteral("프로젝트 열기"), {},
                                     QStringLiteral("Lux 프로젝트 (*.luxproj);;모든 파일 (*)"));
    if (!file.isEmpty())
        OpenProjectFile(PathFromUtf8(Utf8(file)));
}
void EditorWindow::OpenProjectFile(const std::filesystem::path& file) {
    std::string error;
    auto opened = EditorProject::OpenForRecovery(file, error);
    if (!opened) {
        ShowError(QStringLiteral("프로젝트 열기 실패"), Q(error));
        recentProjects_.Load();
        UpdateRecentMenu();
        return;
    }
    if (!ConfirmDiscardChanges())
        return;
    const auto result = LoadSceneDocument(opened->GetSceneFile(), &*opened);
    if (result == SceneOpenResult::Cancelled)
        return;
    if (result == SceneOpenResult::Failed) {
        ShowError(QStringLiteral("프로젝트 열기 실패"),
                  QStringLiteral("시작 장면을 열 수 없습니다."));
        return;
    }
    project_ = std::move(opened);
    undo_->clear();
    ApplyProjectPreferences();
    if (result == SceneOpenResult::Recovered) {
        undo_->resetClean();
        document_.MarkDirty();
    } else
        RecoverAutosaveIfNeeded();
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
        ShowError(QStringLiteral("프로젝트 설정 저장 실패"), Q(error));
        return;
    }
    if (!request->iconSource.empty())
        request->settings.icon = project_->GetIcon();
    if (!project_->UpdateSettings(request->settings, error)) {
        ShowError(QStringLiteral("프로젝트 설정 저장 실패"), Q(error));
        return;
    }
    ApplyProjectPreferences();
    RefreshAll();
}
void EditorWindow::ApplyProjectPreferences() {
    autosaveTimer_.stop();
    const bool snap = project_ ? project_->GetSettings().snapToGrid : true;
    if (snapAction_)
        snapAction_->setChecked(snap);
    canvas_->SetSnapToGrid(snap);
    if (!project_) {
        setWindowIcon(QIcon());
        return;
    }
    const auto seconds = project_->GetSettings().autosaveSeconds;
    if (seconds > 0)
        autosaveTimer_.start(static_cast<int>(seconds * 1000));
    if (!project_->GetIcon().empty())
        setWindowIcon(QIcon(Q(project_->GetFile().parent_path() / project_->GetIcon())));
    else
        setWindowIcon(QIcon());
}
EditorWindow::SceneOpenResult
EditorWindow::LoadSceneDocument(const std::filesystem::path& file,
                                const EditorProject* recoveryProject) {
    if (document_.Open(PathUtf8(file)))
        return SceneOpenResult::Opened;
    if (!recoveryProject)
        return SceneOpenResult::Failed;
    const auto autosave = recoveryProject->GetAutosaveFile(file);
    LuxEngine::Scene validation;
    if (autosave.empty() || !validation.Deserialize(PathUtf8(autosave)))
        return SceneOpenResult::Failed;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("장면 자동 저장 복구"),
        QStringLiteral("원본 장면을 읽을 수 없습니다. 자동 저장본으로 복구하시겠습니까?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return SceneOpenResult::Cancelled;
    return document_.Recover(PathUtf8(file), PathUtf8(autosave)) ? SceneOpenResult::Recovered
                                                                 : SceneOpenResult::Failed;
}

bool EditorWindow::OpenProjectScene(const std::filesystem::path& file) {
    if (!ConfirmDiscardChanges())
        return false;
    const bool owned = project_ && project_->ContainsScene(file);
    const auto result = LoadSceneDocument(file, owned ? &*project_ : nullptr);
    if (result == SceneOpenResult::Cancelled)
        return false;
    if (result == SceneOpenResult::Failed) {
        ShowError(QStringLiteral("장면 열기 실패"), QStringLiteral("장면 파일을 열 수 없습니다."));
        return false;
    }
    if (project_ && !owned)
        project_.reset();
    undo_->clear();
    ApplyProjectPreferences();
    if (result == SceneOpenResult::Recovered) {
        undo_->resetClean();
        document_.MarkDirty();
    } else
        RecoverAutosaveIfNeeded();
    RefreshAll();
    return true;
}
void EditorWindow::RecoverAutosaveIfNeeded() {
    if (!project_ || !project_->HasNewerAutosave(PathFromUtf8(document_.GetPath())))
        return;
    const auto path = project_->GetAutosaveFile(PathFromUtf8(document_.GetPath()));
    const auto answer = QMessageBox::question(
        this, QStringLiteral("장면 자동 저장 복구"),
        QStringLiteral("저장된 장면보다 새 자동 저장본이 있습니다. 복구하시겠습니까?"));
    if (answer == QMessageBox::Yes) {
        if (!document_.Recover(document_.GetPath(), PathUtf8(path)))
            ShowError(QStringLiteral("복구 실패"),
                      QStringLiteral("자동 저장본을 읽을 수 없습니다."));
        else
            undo_->resetClean();
    } else
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
}
void EditorWindow::AutoSaveScene() {
    if (previewActive_)
        return;
    if (!project_ || !document_.IsDirty() || document_.GetPath().empty())
        return;
    std::string error;
    if (project_->SaveAutosave(PathFromUtf8(document_.GetPath()), document_.GetScene(), error))
        Log(QStringLiteral("장면 자동 저장본을 만들었습니다."));
    else
        Log(Q(error), true);
}
bool EditorWindow::ConfirmDiscardChanges() {
    StopPreview();
    if (!document_.IsDirty())
        return true;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("LuxEditor"),
        QStringLiteral("저장하지 않은 변경 사항이 있습니다. 저장하시겠습니까?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer != QMessageBox::Yes || SaveScene(false);
}
bool EditorWindow::SaveScene(bool choosePath) {
    StopPreview();
    const auto previous = document_.GetPath();
    bool saved = false;
    if (choosePath || previous.empty()) {
        QString file =
            QFileDialog::getSaveFileName(this, QStringLiteral("장면 저장"), {},
                                         QStringLiteral("Lux 장면 (*.lux);;모든 파일 (*)"));
        if (file.isEmpty())
            return false;
        if (QFileInfo(file).suffix().isEmpty())
            file += QStringLiteral(".lux");
        saved = document_.SaveAs(Utf8(file));
    } else
        saved = document_.Save();
    if (!saved) {
        ShowError(
            QStringLiteral("저장 실패"),
            QStringLiteral("장면을 저장하지 못했습니다. 파일 경로와 쓰기 권한을 확인해 주세요."));
        return false;
    }
    RefreshProjectExplorer();
    if (project_) {
        if (!previous.empty())
            project_->ClearAutosave(PathFromUtf8(previous));
        project_->ClearAutosave(PathFromUtf8(document_.GetPath()));
    }
    undo_->setClean();
    UpdateChrome();
    Log(QStringLiteral("장면을 저장했습니다."));
    return true;
}
void EditorWindow::CloneCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("프로젝트 복제"),
        QStringLiteral("복제할 프로젝트의 이름을 입력해 주세요."), QLineEdit::Normal,
        Q(project_->GetName()) + QStringLiteral(" 복제"), &accepted);
    if (!accepted)
        return;
    auto destination = ShowProjectDestinationDialog(this, "프로젝트 복제", Utf8(name));
    if (!destination)
        return;
    std::string error;
    std::optional<EditorProject> copy;
    RunWithProgress(QStringLiteral("프로젝트를 복제하고 있습니다..."),
                    [&] { copy = CloneProject(*project_, *destination, Utf8(name), error); });
    if (!copy) {
        ShowError(QStringLiteral("프로젝트 복제 실패"), Q(error));
        return;
    }
    OpenProjectFile(copy->GetFile());
}
void EditorWindow::ImportProjectDialog() {
    const QString file =
        QFileDialog::getOpenFileName(this, QStringLiteral("프로젝트 가져오기"), {},
                                     QStringLiteral("Lux 프로젝트 패키지 (*.luxpack *.luxbackup)"));
    if (file.isEmpty())
        return;
    const auto archive = PathFromUtf8(Utf8(file));
    auto destination =
        ShowProjectDestinationDialog(this, "프로젝트 가져오기", PathUtf8(archive.stem()));
    if (!destination || !ConfirmDiscardChanges())
        return;
    std::string error;
    std::optional<EditorProject> imported;
    RunWithProgress(QStringLiteral("프로젝트를 가져오고 있습니다..."),
                    [&] { imported = ImportProject(archive, *destination, error); });
    if (!imported) {
        ShowError(QStringLiteral("프로젝트 가져오기 실패"), Q(error));
        return;
    }
    OpenProjectFile(imported->GetFile());
}
void EditorWindow::ExportCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    QString file = QFileDialog::getSaveFileName(this, QStringLiteral("프로젝트 내보내기"),
                                                Q(project_->GetName()) + QStringLiteral(".luxpack"),
                                                QStringLiteral("Lux 프로젝트 패키지 (*.luxpack)"));
    if (file.isEmpty())
        return;
    auto archive = PathFromUtf8(Utf8(file));
    if (archive.extension() != ".luxpack")
        archive += ".luxpack";
    std::string error;
    bool success = false;
    RunWithProgress(QStringLiteral("프로젝트 패키지를 만들고 있습니다..."),
                    [&] { success = ExportProject(*project_, archive, error); });
    if (!success) {
        ShowError(QStringLiteral("프로젝트 내보내기 실패"), Q(error));
        return;
    }
    Log(QStringLiteral("프로젝트 패키지를 내보냈습니다."));
}
void EditorWindow::BackupCurrentProject() {
    if (!project_ || (document_.IsDirty() && !SaveScene(false)))
        return;
    std::filesystem::path backup;
    std::string error;
    bool success = false;
    RunWithProgress(QStringLiteral("프로젝트를 백업하고 있습니다..."),
                    [&] { success = CreateProjectBackup(*project_, backup, error); });
    if (!success) {
        ShowError(QStringLiteral("프로젝트 백업 실패"), Q(error));
        return;
    }
    Log(QStringLiteral("프로젝트 백업을 만들었습니다."));
}
void EditorWindow::RestoreCurrentProject() {
    if (!project_)
        return;
    const auto directory = project_->GetFile().parent_path() / ".lux" / "backups";
    const QString file =
        QFileDialog::getOpenFileName(this, QStringLiteral("프로젝트 백업 복구"), Q(directory),
                                     QStringLiteral("Lux 백업 (*.luxbackup)"));
    if (file.isEmpty() || !ConfirmDiscardChanges())
        return;
    if (QMessageBox::question(this, QStringLiteral("프로젝트 백업 복구"),
                              QStringLiteral("현재 프로젝트를 선택한 백업으로 복구하시겠습니까? "
                                             "복구 전 상태도 별도 백업합니다.")) !=
        QMessageBox::Yes)
        return;
    const auto projectFile = project_->GetFile();
    std::string error;
    bool success = false;
    RunWithProgress(QStringLiteral("프로젝트를 복구하고 있습니다..."), [&] {
        success = RestoreProjectBackup(*project_, PathFromUtf8(Utf8(file)), error);
    });
    if (!success) {
        ShowError(QStringLiteral("프로젝트 백업 복구 실패"), Q(error));
        return;
    }
    auto restored = EditorProject::Open(projectFile, error);
    if (!restored || !document_.Open(PathUtf8(restored->GetSceneFile()))) {
        ShowError(QStringLiteral("프로젝트 백업 복구 실패"),
                  error.empty() ? QStringLiteral("복구한 프로젝트를 열 수 없습니다.") : Q(error));
        return;
    }
    project_ = std::move(restored);
    undo_->clear();
    ApplyProjectPreferences();
    RefreshAll();
    Log(QStringLiteral("프로젝트를 백업에서 복구했습니다."));
}
void EditorWindow::RescanProjectResources() {
    if (!project_)
        return;
    std::string error;
    size_t count = 0;
    RunWithProgress(QStringLiteral("리소스를 재검색하고 있습니다..."),
                    [&] { count = project_->RescanResources(error); });
    if (!error.empty()) {
        ShowError(QStringLiteral("리소스 재검색 실패"), Q(error));
        return;
    }
    RefreshProjectExplorer();
    Log(QStringLiteral("리소스 %1개를 재검색했습니다.").arg(count));
}
} // namespace LuxEditor
