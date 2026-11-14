#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEngine/Scene/PrefabLibrary.hpp>
#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <stdexcept>

namespace LuxEditor {
namespace {
bool InProject(const std::filesystem::path& path, const std::filesystem::path& directory) {
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(directory, error);
    if (error)
        return false;
    const auto file = std::filesystem::weakly_canonical(path, error);
    if (error)
        return false;
    const auto relative = file.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
} // namespace

void EditorWindow::BuildPrefabMenus(QMenu* parent) {
    auto* menu = parent->addMenu(QStringLiteral("프리팹"));
    menu->setObjectName(QStringLiteral("prefabMenu"));
    const auto add = [this, menu](const QString& text, const char* name, auto callback) {
        auto* action = menu->addAction(text);
        action->setObjectName(QString::fromLatin1(name));
        connect(action, &QAction::triggered, this, callback);
    };
    add(QStringLiteral("선택 계층을 프리팹으로 저장..."), "savePrefab",
        [this] { SaveSelectedPrefab(); });
    add(QStringLiteral("프리팹 배치..."), "instantiatePrefab", [this] {
        if (previewActive_ || !project_)
            return;
        const auto file = QFileDialog::getOpenFileName(
            this, QStringLiteral("프리팹 배치"),
            QString::fromUtf8(PathUtf8(project_->GetFile().parent_path())),
            QStringLiteral("Lux 프리팹 (*.luxprefab *.LUXPREFAB)"));
        if (!file.isEmpty())
            AddPrefabAsset(PathFromUtf8(file.toUtf8().toStdString()));
    });
    add(QStringLiteral("선택 프리팹 갱신 (로컬 변경 유지)"), "refreshPrefab",
        [this] { EditSelectedPrefab(0); });
    add(QStringLiteral("선택 프리팹의 로컬 변경 되돌리기"), "revertPrefab",
        [this] { EditSelectedPrefab(1); });
    add(QStringLiteral("선택 프리팹 연결 해제"), "unpackPrefab", [this] { EditSelectedPrefab(2); });
}

void EditorWindow::SaveSelectedPrefab() {
    const auto selected = document_.GetSelected();
    if (previewActive_ || !selected || !project_)
        return;
    const auto tab = document_.GetActiveTabId();
    const auto root = selected.GetId();
    const auto directory = project_->GetFile().parent_path() / "Assets" / "Prefabs";
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        ShowError(QStringLiteral("프리팹 저장"), QStringLiteral("프리팹 폴더를 만들 수 없습니다."));
        return;
    }
    auto file =
        QFileDialog::getSaveFileName(this, QStringLiteral("프리팹 저장"),
                                     QString::fromUtf8(PathUtf8(directory / "NewPrefab.luxprefab")),
                                     QStringLiteral("Lux 프리팹 (*.luxprefab)"));
    if (file.isEmpty())
        return;
    if (document_.GetActiveTabId() != tab || document_.GetSelectedId(tab) != root)
        return;
    auto path = PathFromUtf8(file.toUtf8().toStdString());
    if (!LuxEngine::PrefabLibrary::IsPrefabPath(path))
        path += ".luxprefab";
    if (!InProject(path, project_->GetFile().parent_path())) {
        ShowError(QStringLiteral("프리팹 저장"), QStringLiteral("프로젝트 내부에 저장해 주세요."));
        return;
    }
    std::string error;
    if (!LuxEngine::PrefabLibrary::Save(document_.GetScene(), root, path, error)) {
        ShowError(QStringLiteral("프리팹 저장"), QString::fromUtf8(error));
        return;
    }
    RefreshProjectExplorer();
    Log(QStringLiteral("프리팹 저장: %1").arg(QString::fromUtf8(PathUtf8(path))));
}

void EditorWindow::AddPrefabAsset(const std::filesystem::path& path) {
    if (previewActive_ || !project_)
        return;
    if (!InProject(path, project_->GetFile().parent_path())) {
        ShowError(QStringLiteral("프리팹 배치"),
                  QStringLiteral("프로젝트 내부의 프리팹을 선택해 주세요."));
        return;
    }
    RecordEdit(QStringLiteral("프리팹 배치"), [this, path] {
        std::string error;
        const auto root = LuxEngine::PrefabLibrary::Instantiate(document_.GetScene(), path, error);
        if (!root)
            throw std::runtime_error(error);
        document_.Select(root);
        document_.MarkDirty();
    });
    RefreshAll();
}

void EditorWindow::EditSelectedPrefab(int operation) {
    const auto selected = document_.GetSelected();
    if (previewActive_ || !selected)
        return;
    const auto root =
        LuxEngine::PrefabLibrary::FindInstanceRoot(document_.GetScene(), selected.GetId());
    if (!root) {
        ShowError(QStringLiteral("프리팹 편집"),
                  QStringLiteral("프리팹 인스턴스를 선택해 주세요."));
        return;
    }
    const auto name = operation == 0   ? QStringLiteral("프리팹 갱신")
                      : operation == 1 ? QStringLiteral("프리팹 원본 복원")
                                       : QStringLiteral("프리팹 연결 해제");
    RecordEdit(name, [this, root, operation] {
        std::string error;
        auto& scene = document_.GetScene();
        const bool accepted = operation == 0 ? LuxEngine::PrefabLibrary::Refresh(scene, root, error)
                              : operation == 1
                                  ? LuxEngine::PrefabLibrary::Revert(scene, root, error)
                                  : LuxEngine::PrefabLibrary::Unpack(scene, root, error);
        if (!accepted)
            throw std::runtime_error(error);
        document_.Select(root);
        document_.MarkDirty();
    });
    RefreshAll();
}
} // namespace LuxEditor
