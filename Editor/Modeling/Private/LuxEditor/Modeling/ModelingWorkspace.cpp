#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Modeling/MeshModeling.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace LuxEditor {
namespace {
using namespace LuxEngine;

bool SameLifetime(const std::weak_ptr<int>& left, const std::weak_ptr<int>& right) {
    return !left.owner_before(right) && !right.owner_before(left);
}

bool Fbx(const std::string& path) {
    if (path.size() < 4)
        return false;
    auto extension = path.substr(path.size() - 4);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".fbx";
}

bool NeedsBake(Entity entity, const Ref<ModelAsset>& asset = {}) {
    const auto* renderer = entity.TryGetComponent<MeshRendererComponent>();
    if (asset) {
        if (!asset->animations.empty())
            return true;
        for (size_t index = 0; index < asset->submeshes.size(); ++index)
            if ((!renderer || renderer->modelSubmesh < 0 ||
                 static_cast<size_t>(renderer->modelSubmesh) == index) &&
                (asset->submeshes[index].skinned || asset->submeshes[index].morphTargets))
                return true;
    }
    auto& scene = *entity.GetScene();
    for (auto cursor = entity.GetId(); cursor; cursor = scene.GetParent(cursor)) {
        if (const auto* clip = scene.TryGetComponent<ModelAnimationComponent>(cursor);
            clip && clip->clip >= 0)
            return true;
        if (const auto* clip = scene.TryGetComponent<AnimationComponent>(cursor);
            clip && !clip->clip.keyframes.empty())
            return true;
    }
    return false;
}

bool PlainMesh(Entity entity) {
    return entity.HasComponent<MeshRendererComponent>() &&
           !entity.HasComponent<ScriptComponent>() &&
           !entity.HasComponent<Rigidbody2DComponent>() &&
           !entity.HasComponent<BoxCollider2DComponent>() &&
           !entity.HasComponent<Rigidbody3DComponent>() &&
           !entity.HasComponent<BoxCollider3DComponent>() &&
           !entity.HasComponent<AnimationComponent>() &&
           !entity.HasComponent<ModelAnimationComponent>() &&
           !entity.HasComponent<CameraComponent>() &&
           !entity.HasComponent<SpriteRendererComponent>() &&
           !entity.HasComponent<TilemapComponent>() &&
           !entity.HasComponent<SceneReferenceComponent>() &&
           !entity.HasComponent<InstanceSourceComponent>();
}

bool SameMaterial(Scene& scene, const Material3D& left, const Material3D& right) {
    const auto numbers = [](const Material3D& value) {
        return std::tuple{value.albedoColor.x,
                          value.albedoColor.y,
                          value.albedoColor.z,
                          value.albedoColor.w,
                          value.metallic,
                          value.roughness,
                          value.normalScale,
                          value.occlusionStrength,
                          value.textureSampling.mipmaps,
                          value.textureSampling.addressMode,
                          value.textureSampling.anisotropic,
                          value.textureSampling.normalMap,
                          value.alphaMode,
                          value.alphaCutoff,
                          value.doubleSided};
    };
    if (numbers(left) != numbers(right))
        return false;
    for (const auto field :
         {&Material3D::baseColorTexture, &Material3D::normalTexture, &Material3D::metallicTexture,
          &Material3D::roughnessTexture, &Material3D::occlusionTexture}) {
        if ((left.*field).empty() != (right.*field).empty())
            return false;
        if (!(left.*field).empty() &&
            scene.ResolveAssetPath(left.*field) != scene.ResolveAssetPath(right.*field))
            return false;
    }
    return true;
}

bool UsesTextures(const Material3D& material) {
    return !material.baseColorTexture.empty() || !material.normalTexture.empty() ||
           !material.metallicTexture.empty() || !material.roughnessTexture.empty() ||
           !material.occlusionTexture.empty();
}

std::string Label(Entity entity) {
    const auto* tag = entity.TryGetComponent<TagComponent>();
    return tag ? tag->tag : "Mesh";
}

ModelingSelection RemapSelection(const ModelingSelection& selection, const ModelingMesh& source,
                                 const ModelingMesh& destination) {
    ModelingSelection result;
    std::map<std::tuple<float, float, float>, std::vector<uint32>> positions;
    for (uint32 id = 0; id < destination.vertices.size(); ++id) {
        const auto position = destination.vertices[id].position;
        positions[{position.x, position.y, position.z}].push_back(id);
    }
    for (const auto id : selection.vertices) {
        if (id >= source.vertices.size())
            continue;
        const auto position = source.vertices[id].position;
        if (const auto found = positions.find({position.x, position.y, position.z});
            found != positions.end())
            result.vertices.insert(result.vertices.end(), found->second.begin(),
                                   found->second.end());
    }
    std::sort(result.vertices.begin(), result.vertices.end());
    result.vertices.erase(std::unique(result.vertices.begin(), result.vertices.end()),
                          result.vertices.end());
    for (const auto face : selection.faces)
        if (face < destination.indices.size() / 3)
            result.faces.push_back(face);
    return result;
}
} // namespace

void EditorWindow::BuildModelingWorkspace() {
    auto* panel = new LuxUI::Widget(this);
    auto* layout = new LuxUI::VBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* primitives = new LuxUI::ComboBox(panel);
    primitives->setObjectName(LUX_UI_TEXT("modelingPrimitive"));
    for (const auto& model : BasicModelLibrary::GetModels())
        primitives->addItem(LuxUI::String::fromUtf8(model.name), LuxUI::String::fromUtf8(model.id));
    auto* add = new LuxUI::PushButton(MakeEditorIcon(EditorIcon::Mesh),
                                      LUX_UI_TEXT("기본 도형 추가"), panel);
    add->setObjectName(LUX_UI_TEXT("modelingAddPrimitive"));
    layout->addWidget(primitives);
    layout->addWidget(add);
    connect(add, &LuxUI::PushButton::clicked, this, [this, primitives] {
        AddBasicModel(primitives->currentData().toString().toUtf8().toStdString());
    });
    modelingMode_ = new LuxUI::ComboBox(panel);
    modelingMode_->setObjectName(LUX_UI_TEXT("modelingMode"));
    modelingMode_->addItems({LUX_UI_TEXT("오브젝트"), LUX_UI_TEXT("정점"), LUX_UI_TEXT("면")});
    layout->addWidget(new LuxUI::Label(LUX_UI_TEXT("편집 모드"), panel));
    layout->addWidget(modelingMode_);
    auto* selected = new LuxUI::Label(panel);
    selected->setObjectName(LUX_UI_TEXT("modelingSelection"));
    selected->setWordWrap(true);
    layout->addWidget(selected);
    auto* distance = new LuxUI::DoubleSpinBox(panel);
    distance->setObjectName(LUX_UI_TEXT("modelingExtrudeDistance"));
    distance->setRange(-10000, 10000);
    distance->setDecimals(3);
    distance->setSingleStep(.1);
    distance->setValue(.5);
    distance->setKeyboardTracking(false);
    layout->addWidget(new LuxUI::Label(LUX_UI_TEXT("돌출 거리"), panel));
    layout->addWidget(distance);
    auto* extrude = new LuxUI::PushButton(LUX_UI_TEXT("선택 면 돌출"), panel);
    extrude->setObjectName(LUX_UI_TEXT("modelingExtrude"));
    layout->addWidget(extrude);
    connect(extrude, &LuxUI::PushButton::clicked, this, [this] { ExtrudeModelingSelection(); });
    auto* merge = new LuxUI::PushButton(LUX_UI_TEXT("선택 메시 합치기"), panel);
    merge->setObjectName(LUX_UI_TEXT("modelingMerge"));
    merge->setToolTip(LUX_UI_TEXT(
        "같은 재질의 정적 메시를 활성 오브젝트에 합칩니다. 다른 오브젝트의 자식은 유지합니다."));
    layout->addWidget(merge);
    connect(merge, &LuxUI::PushButton::clicked, this, [this] { MergeModelingSelection(); });
    auto* bake = new LuxUI::PushButton(LUX_UI_TEXT("현재 포즈의 정적 복사본 만들기"), panel);
    bake->setObjectName(LUX_UI_TEXT("modelingBake"));
    bake->setToolTip(
        LUX_UI_TEXT("원본 계층과 애니메이션을 유지하고 독립된 정적 메시를 생성합니다."));
    layout->addWidget(bake);
    connect(bake, &LuxUI::PushButton::clicked, this, [this] { BakeModelingSelection(); });
    auto* reload = new LuxUI::PushButton(LUX_UI_TEXT("편집 메시 새로 고침"), panel);
    reload->setObjectName(LUX_UI_TEXT("modelingReload"));
    layout->addWidget(reload);
    connect(reload, &LuxUI::PushButton::clicked, this, [this] {
        modelingEntity_ = 0;
        RefreshModelingWorkspace();
    });
    layout->addStretch();
    modelingDock_ =
        AddPanel("modelingToolsDock", "모델링", panel, LuxUI::Flags::LeftDockWidgetArea);
    modelingDock_->setMinimumWidth(225);
    modelingDock_->hide();
    auto* menu = menuBar()->addMenu(LUX_UI_TEXT("모델링"));
    menu->setObjectName(LUX_UI_TEXT("modelingMenu"));
    auto* show = AddCommand(menu, "showModelingTools", "모델링 도구", [this] {
        SetWorkspace(false);
        viewportTabs_->setCurrentIndex(0);
        modelingDock_->show();
        modelingDock_->raise();
    });
    if (auto* toolbar = findChild<LuxUI::ToolBar*>(LUX_UI_TEXT("lux-main-toolbar")))
        toolbar->addAction(show);
    for (int index = 0; index < 3; ++index)
        AddCommand(menu, "modelingMode." + std::to_string(index),
                   modelingMode_->itemText(index).toUtf8().toStdString(), [this, index] {
                       SetWorkspace(false);
                       viewportTabs_->setCurrentIndex(0);
                       modelingDock_->show();
                       modelingDock_->raise();
                       modelingMode_->setCurrentIndex(index);
                   });
    connect(modelingMode_, &LuxUI::ComboBox::currentIndexChanged, this, [this] {
        if (refreshingModeling_)
            return;
        CancelViewportEdit();
        if (modelingMode_->currentIndex() != 0) {
            SetWorkspace(false);
            viewportTabs_->setCurrentIndex(0);
            SetTool(EditorTool::Move);
        }
        RefreshModelingWorkspace();
    });
    canvas3D_->SetModelingCommitCallback(
        [this](const ModelingMesh& mesh, const ModelingSelection& selection) {
            CommitModelingEdit(mesh, selection);
        });
    canvas3D_->SetModelingSelectionCallback(
        [this](const ModelingSelection&) { RefreshModelingWorkspace(); });
    connect(modelingDock_, &LuxUI::DockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible && modelingMode_)
            modelingMode_->setCurrentIndex(0);
    });
    RefreshModelingWorkspace();
}

void EditorWindow::RefreshModelingWorkspace() {
    if (!modelingDock_ || refreshingModeling_)
        return;
    refreshingModeling_ = true;

    struct Guard {
        bool& value;

        ~Guard() {
            value = false;
        }
    } guard{refreshingModeling_};

    const auto entity = document_.GetSelected();
    const auto* renderer = entity ? entity.TryGetComponent<MeshRendererComponent>() : nullptr;
    const bool restoreElements = restoringHistory_ && renderer &&
                                 canvas3D_->GetModelingEntity() == entity.GetId() &&
                                 modelingTab_ == document_.GetActiveTabId();
    auto retainedElements =
        restoreElements ? canvas3D_->GetModelingSelection() : ModelingSelection{};
    const auto lifetime = document_.GetScene().Lifetime();
    const auto path =
        renderer ? document_.GetScene().ResolveAssetPath(renderer->meshPath) : std::string{};
    const bool changed = modelingEntity_ != (renderer ? entity.GetId() : 0) ||
                         modelingTab_ != document_.GetActiveTabId() ||
                         !SameLifetime(lifetime, modelingLifetime_) ||
                         modelingSourcePath_ != path ||
                         modelingSourcePart_ != (renderer ? renderer->modelSubmesh : -1) ||
                         (!previewActive_ && renderer && !modelingMesh_);
    std::string failure;
    if (changed) {
        modelingMesh_.reset();
        modelingSourceAsset_.reset();
        modelingEntity_ = renderer ? entity.GetId() : 0;
        modelingTab_ = document_.GetActiveTabId();
        modelingLifetime_ = lifetime;
        modelingSourcePath_ = path;
        modelingSourcePart_ = renderer ? renderer->modelSubmesh : -1;
        if (renderer && !previewActive_) {
            auto mesh = std::make_shared<ModelingMesh>();
            if (LoadModelingMesh(document_.GetScene(), entity, *mesh, failure))
                modelingMesh_ = std::move(mesh);
            if (Fbx(path)) {
                std::string ignored;
                modelingSourceAsset_ = ModelAsset::LoadFbx(path, ignored);
            }
        }
    }
    const bool available = renderer && modelingMesh_ && project_ && !previewActive_;
    const bool needsBake = renderer && NeedsBake(entity, modelingSourceAsset_);
    const bool editable = available && !needsBake && document_.GetSelection().size() == 1;
    const auto mode = editable ? static_cast<ModelingEditMode>(modelingMode_->currentIndex())
                               : ModelingEditMode::Object;
    if (editable) {
        if (changed || canvas3D_->GetModelingEntity() != entity.GetId())
            canvas3D_->SetModelingMesh(entity.GetId(), *modelingMesh_);
        if (canvas3D_->GetModelingMode() != mode)
            canvas3D_->SetModelingMode(mode);
        if (restoreElements) {
            if (mode == ModelingEditMode::Face)
                retainedElements.vertices =
                    ModelingFaceVertices(*modelingMesh_, retainedElements.faces);
            canvas3D_->SetModelingSelection(retainedElements);
        }
    } else if (canvas3D_->GetModelingMesh()) {
        canvas3D_->ClearModelingMesh();
    }
    const auto selection = editable ? canvas3D_->GetModelingSelection() : ModelingSelection{};
    auto* info = modelingDock_->findChild<LuxUI::Label*>(LUX_UI_TEXT("modelingSelection"));
    info->setText(!project_   ? LUX_UI_TEXT("프로젝트를 열어 주세요.")
                  : !renderer ? LUX_UI_TEXT("메시 오브젝트를 선택해 주세요.")
                  : previewActive_
                      ? LUX_UI_TEXT("모델 편집은 게임 실행을 중지한 뒤 사용할 수 있습니다.")
                  : !failure.empty() ? LuxUI::String::fromUtf8(failure)
                  : document_.GetSelection().size() > 1
                      ? LUX_UI_TEXT("메시를 합치거나 하나를 선택해 정점·면을 편집해 주세요.")
                  : needsBake ? LUX_UI_TEXT("이 모델은 애니메이션·스킨·모프를 포함합니다. 정적 "
                                            "복사본을 만들어 편집해 주세요.")
                  : editable  ? LUX_UI_TEXT("정점 %1 / %2 · 면 %3 / %4")
                                   .arg(selection.vertices.size())
                                   .arg(modelingMesh_->vertices.size())
                                   .arg(selection.faces.size())
                                   .arg(modelingMesh_->indices.size() / 3)
                             : LUX_UI_TEXT("편집 메시를 읽을 수 없습니다."));
    modelingMode_->setEnabled(editable);
    modelingDock_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingAddPrimitive"))
        ->setEnabled(project_ && !previewActive_);
    modelingDock_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingExtrude"))
        ->setEnabled(editable && mode == ModelingEditMode::Face && !selection.faces.empty());
    modelingDock_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingMerge"))
        ->setEnabled(project_ && !previewActive_ && document_.GetSelection().size() > 1);
    modelingDock_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingBake"))
        ->setEnabled(available);
    modelingDock_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelingReload"))
        ->setEnabled(renderer && !previewActive_);
}

void EditorWindow::CommitModelingEdit(const ModelingMesh& mesh,
                                      const ModelingSelection& selection) {
    const auto entity = document_.GetSelected();
    if (!project_ || previewActive_ || !entity || entity.GetId() != modelingEntity_ ||
        modelingTab_ != document_.GetActiveTabId() ||
        !SameLifetime(modelingLifetime_, document_.GetScene().Lifetime()) ||
        !entity.HasComponent<MeshRendererComponent>() || NeedsBake(entity, modelingSourceAsset_)) {
        modelingEntity_ = 0;
        RefreshModelingWorkspace();
        return;
    }
    if (!mesh.hasTexCoords && UsesTextures(entity.GetComponent<MeshRendererComponent>().material)) {
        ShowError(LUX_UI_TEXT("메시 모델링"),
                  LUX_UI_TEXT("텍스처를 사용하는 재질에는 UV 좌표가 있는 메시가 필요합니다."));
        modelingEntity_ = 0;
        RefreshModelingWorkspace();
        return;
    }
    const auto id = entity.GetId();
    const ModelingMesh committed = mesh;
    const ModelingSelection selectedElements = selection;
    std::string saved;
    RecordEdit(LUX_UI_TEXT("메시 모델링"), [this, id, &committed, &saved] {
        std::string error;
        if (!SaveModelingMesh(committed, project_->GetFile().parent_path(),
                              Label({id, &document_.GetScene()}), saved, error))
            throw std::runtime_error(error);
        auto& renderer = document_.GetScene().GetComponent<MeshRendererComponent>(id);
        renderer.meshPath = saved;
        renderer.modelSubmesh = -1;
        document_.MarkDirty();
    });
    modelingEntity_ = 0;
    RefreshAll();
    if (canvas3D_->GetModelingEntity() == id && modelingMesh_)
        canvas3D_->SetModelingSelection(
            RemapSelection(selectedElements, committed, *modelingMesh_));
    RefreshModelingWorkspace();
}

void EditorWindow::ExtrudeModelingSelection() {
    if (!modelingMesh_ || !project_ || previewActive_)
        return;
    auto selection = canvas3D_->GetModelingSelection();
    auto mesh = *modelingMesh_;
    const auto distance = static_cast<float>(
        modelingDock_->findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("modelingExtrudeDistance"))
            ->value());
    std::string error;
    if (!ExtrudeModelingFaces(mesh, selection.faces, distance, error)) {
        ShowError(LUX_UI_TEXT("면 돌출"), LuxUI::String::fromUtf8(error));
        return;
    }
    selection.vertices = ModelingFaceVertices(mesh, selection.faces);
    CommitModelingEdit(mesh, selection);
}

void EditorWindow::MergeModelingSelection() {
    if (!project_ || previewActive_ || document_.GetSelection().size() < 2)
        return;
    const auto active = document_.GetSelected();
    auto& scene = document_.GetScene();
    const auto ids = document_.GetSelection();
    const std::unordered_set<uint32> selected(ids.begin(), ids.end());
    try {
        if (!active || !PlainMesh(active))
            throw std::runtime_error(
                "스크립트·물리·애니메이션·참조가 없는 정적 메시만 합칠 수 있습니다.");
        const auto material = active.GetComponent<MeshRendererComponent>().material;
        std::vector<ModelingMesh> meshes(ids.size());
        std::vector<ModelingMeshSource> sources;
        sources.reserve(ids.size());
        const auto target = scene.GetWorldMatrix(active.GetId());
        std::string error;
        for (size_t index = 0; index < ids.size(); ++index) {
            Entity entity{ids[index], &scene};
            if (!PlainMesh(entity))
                throw std::runtime_error("스크립트·물리·애니메이션·참조가 있는 오브젝트는 정적 "
                                         "복사본을 만든 뒤 합쳐 주세요.");
            Ref<ModelAsset> asset;
            const auto path =
                scene.ResolveAssetPath(entity.GetComponent<MeshRendererComponent>().meshPath);
            if (Fbx(path))
                asset = ModelAsset::LoadFbx(path, error);
            if (NeedsBake(entity, asset))
                throw std::runtime_error("애니메이션·스킨·모프가 있는 모델은 현재 포즈의 정적 "
                                         "복사본을 만든 뒤 합쳐 주세요.");
            for (auto parent = scene.GetParent(ids[index]); parent;
                 parent = scene.GetParent(parent))
                if (selected.contains(parent))
                    throw std::runtime_error(
                        "서로 부모·자식인 메시들은 계층을 분리한 뒤 합쳐 주세요.");
            if (!SameMaterial(scene, material,
                              entity.GetComponent<MeshRendererComponent>().material))
                throw std::runtime_error("같은 재질을 사용하는 메시만 합칠 수 있습니다.");
            if (!LoadModelingMesh(scene, entity, meshes[index], error))
                throw std::runtime_error(error);
            Mat4 relative;
            if (!RelativeModelingTransform(target, scene.GetWorldMatrix(ids[index]), relative,
                                           error))
                throw std::runtime_error(error);
            sources.push_back({&meshes[index], relative});
        }
        ModelingMesh merged;
        if (!MergeModelingMeshes(sources, merged, error))
            throw std::runtime_error(error);
        if (!merged.hasTexCoords && UsesTextures(material))
            throw std::runtime_error("텍스처를 유지하려면 합칠 모든 메시에 UV 좌표가 필요합니다.");
        const auto id = active.GetId();
        RecordEdit(LUX_UI_TEXT("정적 메시 합치기"), [this, id, &ids, &merged] {
            auto& scene = document_.GetScene();
            std::string saved, error;
            if (!SaveModelingMesh(merged, project_->GetFile().parent_path(), Label({id, &scene}),
                                  saved, error))
                throw std::runtime_error(error);
            for (const auto other : ids) {
                if (other == id)
                    continue;
                const auto parent = scene.GetParent(other);
                for (const auto child : scene.GetChildren(other))
                    if (!scene.SetParent(child, parent, true))
                        throw std::runtime_error(
                            "자식 오브젝트의 월드 변환을 유지할 수 없어 합치기를 취소했습니다.");
                scene.DestroyEntity({other, &scene});
            }
            auto& renderer = scene.GetComponent<MeshRendererComponent>(id);
            renderer.meshPath = saved;
            renderer.modelSubmesh = -1;
            document_.Select(id);
            document_.MarkDirty();
        });
        RefreshAll();
    } catch (const std::exception& error) {
        ShowError(LUX_UI_TEXT("메시 합치기"), LuxUI::String::fromUtf8(error.what()));
    }
}

void EditorWindow::BakeModelingSelection() {
    if (!project_ || previewActive_)
        return;
    const auto entity = document_.GetSelected();
    if (!entity || !entity.HasComponent<MeshRendererComponent>())
        return;
    ModelingMesh mesh, baked;
    std::string error;
    if (!LoadModelingMesh(document_.GetScene(), entity, mesh, error)) {
        ShowError(LUX_UI_TEXT("정적 복사본 만들기"), LuxUI::String::fromUtf8(error));
        return;
    }
    const auto world = document_.GetScene().GetWorldMatrix(entity.GetId());
    const Vec3 position{world.elements[12], world.elements[13], world.elements[14]};
    const ModelingMeshSource source{&mesh, Mat4::Translate(position * -1) * world};
    if (!MergeModelingMeshes(std::span(&source, 1), baked, error)) {
        ShowError(LUX_UI_TEXT("정적 복사본 만들기"), LuxUI::String::fromUtf8(error));
        return;
    }
    const auto material = entity.GetComponent<MeshRendererComponent>().material;
    if (!baked.hasTexCoords && UsesTextures(material)) {
        ShowError(LUX_UI_TEXT("정적 복사본 만들기"),
                  LUX_UI_TEXT("텍스처를 유지하려면 원본 메시에 UV 좌표가 필요합니다."));
        return;
    }
    const auto name = Label(entity) + " Static";
    RecordEdit(
        LUX_UI_TEXT("현재 포즈의 정적 복사본 만들기"), [this, &baked, position, material, name] {
            std::string saved, error;
            if (!SaveModelingMesh(baked, project_->GetFile().parent_path(), name, saved, error))
                throw std::runtime_error(error);
            auto copy = document_.CreateEntity(name);
            copy.GetComponent<TransformComponent>().position = position;
            auto& renderer = copy.AddComponent<MeshRendererComponent>();
            renderer.material = material;
            renderer.meshPath = saved;
            document_.MarkDirty();
        });
    modelingMode_->setCurrentIndex(1);
    RefreshAll();
}
} // namespace LuxEditor
