#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QTabWidget>

namespace LuxEditor {
namespace {
QString MeshFailure(const std::string& path) {
    const auto error = LuxEngine::AssetManager::GetAssetError(path);
    return error.empty() ? QStringLiteral("모델 파일을 읽을 수 없습니다.")
                         : QString::fromUtf8(error);
}

bool IsInsideProject(const std::filesystem::path& file, const std::filesystem::path& root) {
    std::error_code error;
    const auto canonicalRoot = std::filesystem::canonical(root, error);
    if (error)
        return false;
    const auto canonicalFile = std::filesystem::canonical(file, error);
    if (error || !std::filesystem::is_regular_file(canonicalFile, error) || error)
        return false;
    const auto relative = canonicalFile.lexically_relative(canonicalRoot);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
} // namespace

void EditorWindow::AddBasicModelMenu(QMenu* parent) {
    if (basicModelActions_.empty()) {
        for (const auto& model : LuxEngine::BasicModelLibrary::GetModels()) {
            const std::string id = model.id;
            auto* action =
                new QAction(MakeEditorIcon(EditorIcon::Mesh), QString::fromUtf8(model.name), this);
            action->setObjectName(QStringLiteral("addBasicModel.") + QString::fromUtf8(id));
            action->setToolTip(QStringLiteral("CC0 기본 모델을 프로젝트에 추가합니다."));
            connect(action, &QAction::triggered, this, [this, id] { AddBasicModel(id); });
            basicModelActions_.push_back(action);
        }
    }
    auto* menu = parent->addMenu(MakeEditorIcon(EditorIcon::Mesh), QStringLiteral("기본 모델"));
    menu->addActions(QList<QAction*>(basicModelActions_.begin(), basicModelActions_.end()));
}

void EditorWindow::AddBasicModel(const std::string& id) {
    if (previewActive_)
        return;
    if (!project_) {
        ShowError(QStringLiteral("기본 모델 추가"),
                  QStringLiteral("기본 모델을 추가하려면 프로젝트를 먼저 열어 주세요."));
        return;
    }
    LuxEngine::BasicModelInstallation installed;
    std::string error;
    if (!LuxEngine::BasicModelLibrary::InstallIntoProject(id, project_->GetFile().parent_path(),
                                                          installed, error)) {
        ShowError(QStringLiteral("기본 모델 추가"), QString::fromUtf8(error));
        return;
    }
    AddModelAsset(installed.modelPath);
}

void EditorWindow::AddModelAsset(const std::filesystem::path& path) {
    if (previewActive_)
        return;
    if (!project_ || !IsInsideProject(path, project_->GetFile().parent_path())) {
        ShowError(QStringLiteral("모델 배치"),
                  QStringLiteral("프로젝트 내부의 모델 파일을 선택해 주세요."));
        return;
    }
    const auto modelPath = PathUtf8(path);
    LuxEngine::RawMeshData data;
    if (!LuxEngine::AssetManager::LoadMesh(modelPath, data)) {
        ShowError(QStringLiteral("모델 배치"), MeshFailure(modelPath));
        return;
    }
    LuxEngine::BasicModelInstallation installed;
    const bool basic = LuxEngine::BasicModelLibrary::IdentifyInstalledModel(path, installed);
    if (basic && (!data.hasTexCoords || !LuxEngine::AssetManager::ValidateTextureFile(
                                            PathUtf8(installed.baseColorTexture)))) {
        ShowError(QStringLiteral("모델 배치"),
                  QStringLiteral("기본 모델의 UV 또는 색상 텍스처를 확인할 수 없습니다."));
        return;
    }
    const auto* descriptor =
        basic ? LuxEngine::BasicModelLibrary::FindModel(PathUtf8(path.stem())) : nullptr;
    const auto name = descriptor ? std::string(descriptor->name) : PathUtf8(path.stem());
    const auto texture = basic ? PathUtf8(installed.baseColorTexture) : std::string{};
    RecordEdit(QStringLiteral("모델 배치"), [this, modelPath, texture, name] {
        auto entity = document_.CreateEntity(name);
        auto& mesh = entity.AddComponent<LuxEngine::MeshRendererComponent>();
        mesh.meshPath = modelPath;
        mesh.material.baseColorTexture = texture;
    });
    viewportTabs_->setCurrentIndex(0);
    RefreshAll();
    canvas3D_->FocusSelection();
}

void EditorWindow::ChooseModelAsset() {
    if (previewActive_ || !document_.GetSelected())
        return;
    const auto tab = document_.GetActiveTabId();
    const auto entity = document_.GetSelected().GetId();
    const auto file = QFileDialog::getOpenFileName(
        this, QStringLiteral("모델 선택"), meshPath_->text(),
        QStringLiteral(
            "3D 모델 (*.obj *.OBJ *.fbx *.FBX);;Wavefront OBJ (*.obj *.OBJ);;FBX (*.fbx *.FBX)"));
    if (file.isEmpty())
        return;
    if (document_.GetActiveTabId() != tab || document_.GetSelectedId(tab) != entity) {
        Log(QStringLiteral("선택이 변경되어 모델 적용을 취소했습니다."), true);
        return;
    }
    const auto path = file.toUtf8().toStdString();
    LuxEngine::RawMeshData data;
    if (!LuxEngine::AssetManager::LoadMesh(path, data, true)) {
        ShowError(QStringLiteral("모델 불러오기"), MeshFailure(path));
        return;
    }
    const auto selected = document_.GetSelected();
    const auto hasTextures = [&] {
        if (!selected.HasComponent<LuxEngine::MeshRendererComponent>())
            return false;
        const auto& material = selected.GetComponent<LuxEngine::MeshRendererComponent>().material;
        return !material.baseColorTexture.empty() || !material.normalTexture.empty() ||
               !material.metallicTexture.empty() || !material.roughnessTexture.empty() ||
               !material.occlusionTexture.empty();
    };
    if (hasTextures() && !data.hasTexCoords) {
        ShowError(QStringLiteral("모델 불러오기"),
                  QStringLiteral("텍스처를 유지하려면 UV 좌표가 있는 모델이 필요합니다."));
        return;
    }
    RecordEdit(QStringLiteral("모델 파일 변경"), [this, path] {
        auto selected = document_.GetSelected();
        if (!selected.HasComponent<LuxEngine::MeshRendererComponent>())
            selected.AddComponent<LuxEngine::MeshRendererComponent>();
        selected.GetComponent<LuxEngine::MeshRendererComponent>().meshPath = path;
        document_.MarkDirty();
    });
    RefreshAll();
}

} // namespace LuxEditor
