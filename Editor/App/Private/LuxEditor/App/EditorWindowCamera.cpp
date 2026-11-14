#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace LuxEditor {
namespace {
LuxEngine::TransformComponent ViewTransform(const LuxEngine::PerspectiveCamera& camera) {
    const auto& matrix = camera.GetViewMatrix().elements;
    const LuxEngine::Vec3 right{matrix[0], matrix[4], matrix[8]};
    const LuxEngine::Vec3 up{matrix[1], matrix[5], matrix[9]};
    const LuxEngine::Vec3 back{matrix[2], matrix[6], matrix[10]};
    LuxEngine::TransformComponent transform;
    transform.position = camera.GetPosition();
    transform.rotation.y = std::asin(std::clamp(-right.z, -1.f, 1.f));
    if (std::abs(std::cos(transform.rotation.y)) > 1e-5f) {
        transform.rotation.x = std::atan2(up.z, back.z);
        transform.rotation.z = std::atan2(right.y, right.x);
    } else {
        transform.rotation.x = 0;
        transform.rotation.z = std::atan2(-up.x, up.y);
    }
    return transform;
}

LuxEngine::PerspectiveCamera EntityCamera(LuxEngine::Scene& scene, LuxEngine::uint32 id,
                                          float aspect) {
    const auto& settings = scene.GetComponent<LuxEngine::CameraComponent>(id);
    const auto matrix = scene.GetWorldMatrix(id);
    const auto* m = matrix.elements;
    const LuxEngine::Vec3 position{m[12], m[13], m[14]};
    auto forward = LuxEngine::Vec3{-m[8], -m[9], -m[10]};
    auto up = LuxEngine::Vec3{m[4], m[5], m[6]};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(forward.Dot(forward)) || forward.Dot(forward) < 1e-12f ||
        !std::isfinite(up.Dot(up)) || up.Dot(up) < 1e-12f)
        throw std::runtime_error("카메라의 월드 축이 축소되었거나 올바르지 않습니다.");
    forward = forward.Normalized();
    auto right = forward.Cross(up.Normalized());
    if (right.Dot(right) < 1e-12f)
        throw std::runtime_error("카메라의 전방과 위쪽 축이 겹쳐 시점을 구성할 수 없습니다.");
    up = right.Normalized().Cross(forward).Normalized();
    const float distance =
        std::max({1.f, std::abs(position.x) * .00001f, std::abs(position.y) * .00001f,
                  std::abs(position.z) * .00001f});
    LuxEngine::PerspectiveCamera camera(LuxEngine::ToRadians(settings.fov), aspect,
                                        settings.nearClip, settings.farClip);
    camera.SetPosition(position);
    camera.SetTarget(position + forward * distance);
    camera.SetUp(up);
    return camera;
}
} // namespace

void EditorWindow::BuildCameraDetails(LuxUI::VBoxLayout* layout) {
    auto* group = new LuxUI::GroupBox(LUX_UI_TEXT("Camera"), inspectorBody_);
    group->setObjectName(LUX_UI_TEXT("cameraDetails"));
    auto* form = new LuxUI::FormLayout(group);
    form->setRowWrapPolicy(LuxUI::FormLayout::WrapLongRows);
    cameraEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("카메라 사용"), group);
    cameraEnabled_->setObjectName(LUX_UI_TEXT("cameraEnabled"));
    form->addRow(cameraEnabled_);
    cameraPrimary_ = new LuxUI::CheckBox(LUX_UI_TEXT("게임 기본 카메라"), group);
    cameraPrimary_->setObjectName(LUX_UI_TEXT("cameraPrimary"));
    cameraPrimary_->setToolTip(
        LUX_UI_TEXT("이 카메라를 기본으로 선택하면 다른 카메라의 기본 설정을 해제합니다."));
    form->addRow(cameraPrimary_);
    cameraProjection_ = new LuxUI::ComboBox(group);
    cameraProjection_->setObjectName(LUX_UI_TEXT("cameraProjection"));
    cameraProjection_->addItems({LUX_UI_TEXT("직교"), LUX_UI_TEXT("원근")});
    form->addRow(LUX_UI_TEXT("투영"), cameraProjection_);
    cameraFov_ = new LuxUI::DoubleSpinBox(group);
    cameraFov_->setObjectName(LUX_UI_TEXT("cameraFov"));
    cameraFov_->setRange(1, 179);
    cameraFov_->setDecimals(3);
    cameraFov_->setSuffix(LUX_UI_TEXT("°"));
    cameraFov_->setKeyboardTracking(false);
    form->addRow(LUX_UI_TEXT("시야각"), cameraFov_);
    cameraOrthoSize_ = new LuxUI::DoubleSpinBox(group);
    cameraOrthoSize_->setObjectName(LUX_UI_TEXT("cameraOrthoSize"));
    cameraOrthoSize_->setRange(.01, 100000);
    cameraOrthoSize_->setDecimals(3);
    cameraOrthoSize_->setKeyboardTracking(false);
    form->addRow(LUX_UI_TEXT("직교 크기"), cameraOrthoSize_);
    cameraNearClip_ = new LuxUI::DoubleSpinBox(group);
    cameraNearClip_->setObjectName(LUX_UI_TEXT("cameraNearClip"));
    cameraNearClip_->setRange(.000001, 1000000);
    cameraNearClip_->setDecimals(6);
    cameraNearClip_->setSingleStep(.01);
    cameraFarClip_ = new LuxUI::DoubleSpinBox(group);
    cameraFarClip_->setObjectName(LUX_UI_TEXT("cameraFarClip"));
    cameraFarClip_->setRange(.000001, 1000000000);
    cameraFarClip_->setDecimals(3);
    for (auto* field : {cameraNearClip_, cameraFarClip_}) {
        field->setKeyboardTracking(false);
        field->setMinimumWidth(100);
        field->setButtonSymbols(LuxUI::AbstractSpinBox::NoButtons);
        field->setToolTip(LUX_UI_TEXT(
            "원근 3D 카메라의 표시 거리입니다. 먼 거리는 가까운 거리보다 커야 합니다."));
    }
    form->addRow(LUX_UI_TEXT("가까운 표시 거리"), cameraNearClip_);
    form->addRow(LUX_UI_TEXT("먼 표시 거리"), cameraFarClip_);
    auto* depth = new LuxUI::Label(
        LUX_UI_TEXT("2D 직교 카메라의 깊이는 엔진 기본 범위를 사용합니다."), group);
    depth->setObjectName(LUX_UI_TEXT("cameraDepthHint"));
    depth->setWordWrap(true);
    form->addRow(depth);
    auto* align = new LuxUI::PushButton(LUX_UI_TEXT("씬 뷰에 카메라 맞추기"), group);
    align->setObjectName(LUX_UI_TEXT("cameraAlignToSceneView"));
    align->setToolTip(
        LUX_UI_TEXT("3D는 시점·시야각·표시 거리, 2D는 중심·직교 크기를 현재 씬 뷰에 맞춥니다."));
    form->addRow(align);
    connect(align, &LuxUI::PushButton::clicked, this, [this] { AlignCameraToSceneView(); });
    auto* view = new LuxUI::PushButton(LUX_UI_TEXT("선택 카메라 시점으로 보기"), group);
    view->setObjectName(LUX_UI_TEXT("cameraViewSelected"));
    view->setToolTip(
        LUX_UI_TEXT("게임을 실행하지 않고 씬 뷰를 선택 카메라의 시점으로 이동합니다."));
    form->addRow(view);
    connect(view, &LuxUI::PushButton::clicked, this, [this] { ViewThroughSelectedCamera(); });
    auto* manipulation = new LuxUI::Widget(group);
    auto* manipulationLayout = new LuxUI::HBoxLayout(manipulation);
    manipulationLayout->setContentsMargins(0, 0, 0, 0);
    for (const auto& [tool, text, name, icon] :
         {std::tuple{EditorTool::Move, "이동", "cameraMoveTool", EditorIcon::Move},
          std::tuple{EditorTool::Rotate, "회전", "cameraRotateTool", EditorIcon::Rotate}}) {
        auto* button = new LuxUI::PushButton(MakeEditorIcon(icon), LuxUI::String::fromUtf8(text),
                                             manipulation);
        button->setObjectName(LuxUI::String::fromLatin1(name));
        button->setToolTip(
            LUX_UI_TEXT("카메라 오브젝트를 화면 중앙에 표시하고 기즈모로 조작합니다."));
        manipulationLayout->addWidget(button);
        connect(button, &LuxUI::PushButton::clicked, this, [this, tool] {
            const auto entity = document_.GetSelected();
            if (!entity || !entity.HasComponent<LuxEngine::CameraComponent>())
                return;
            SetWorkspace(false);
            const bool perspective =
                entity.GetComponent<LuxEngine::CameraComponent>().isPerspective;
            viewportTabs_->setCurrentIndex(perspective ? 0 : 1);
            SetTool(tool);
            if (perspective)
                canvas3D_->FocusSelection();
            else
                canvas_->FocusSelection();
        });
    }
    form->addRow(manipulation);
    for (auto* button : {align, view}) {
        button->setSizePolicy(LuxUI::SizePolicy::Ignored, LuxUI::SizePolicy::Fixed);
        button->setMinimumWidth(100);
    }
    layout->addWidget(group);
    connect(cameraEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnCameraChanged(cameraEnabled_); });
    connect(cameraPrimary_, &LuxUI::CheckBox::toggled, this,
            [this] { OnCameraChanged(cameraPrimary_); });
    connect(cameraProjection_, &LuxUI::ComboBox::currentIndexChanged, this,
            [this] { OnCameraChanged(cameraProjection_); });
    for (auto* field : {cameraFov_, cameraOrthoSize_, cameraNearClip_, cameraFarClip_})
        connect(field, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                [this, field] { OnCameraChanged(field); });
}

void EditorWindow::RefreshCameraDetails() {
    const auto entity = document_.GetSelected();
    const auto* camera = entity.TryGetComponent<LuxEngine::CameraComponent>();
    const LuxUI::SignalBlocker enabled(cameraEnabled_), primary(cameraPrimary_),
        projection(cameraProjection_), fov(cameraFov_), ortho(cameraOrthoSize_),
        nearBlock(cameraNearClip_), farBlock(cameraFarClip_);
    cameraEnabled_->setChecked(camera != nullptr);
    cameraPrimary_->setChecked(camera && camera->isPrimary);
    cameraProjection_->setCurrentIndex(camera ? (camera->isPerspective ? 1 : 0)
                                              : (viewportTabs_->currentIndex() == 0 ? 1 : 0));
    cameraFov_->setValue(camera ? camera->fov : 45);
    cameraOrthoSize_->setValue(camera ? camera->orthoSize : 10);
    cameraNearClip_->setValue(camera ? camera->nearClip : .1);
    cameraFarClip_->setValue(camera ? camera->farClip : 1000);
    cameraPrimary_->setEnabled(camera != nullptr);
    cameraProjection_->setEnabled(camera != nullptr);
    cameraFov_->setEnabled(camera && camera->isPerspective);
    cameraOrthoSize_->setEnabled(camera && !camera->isPerspective);
    cameraNearClip_->setEnabled(camera && camera->isPerspective);
    cameraFarClip_->setEnabled(camera && camera->isPerspective);
    findChild<LuxUI::PushButton*>(LUX_UI_TEXT("cameraViewSelected"))
        ->setToolTip(camera && !camera->isPerspective
                         ? LUX_UI_TEXT("2D 중심·직교 크기를 편집 뷰에 맞춥니다. 실제 회전 화면은 "
                                       "Game View에서 확인할 수 있습니다.")
                         : LUX_UI_TEXT("3D 시점·회전·시야각·표시 거리를 선택 카메라에 맞춥니다. F "
                                       "또는 전체 씬 맞춤으로 편집 시점에 복귀할 수 있습니다."));
    findChild<LuxUI::Label*>(LUX_UI_TEXT("cameraDepthHint"))
        ->setVisible(camera && !camera->isPerspective);
    for (const auto name :
         {"cameraAlignToSceneView", "cameraViewSelected", "cameraMoveTool", "cameraRotateTool"})
        findChild<LuxUI::PushButton*>(LuxUI::String::fromLatin1(name))
            ->setEnabled(camera != nullptr);
    for (const auto name : {"alignCameraToSceneView", "viewThroughSelectedCamera"})
        if (auto* action = findChild<LuxUI::Action*>(LuxUI::String::fromLatin1(name)))
            action->setEnabled(camera != nullptr);
    if (camera) {
        auto* group = findChild<LuxUI::GroupBox*>(LUX_UI_TEXT("cameraDetails"));
        const auto target =
            LUX_UI_TEXT("%1:%2").arg(document_.GetActiveTabId()).arg(entity.GetId());
        if (group->property("cameraTarget").toString() != target) {
            group->setProperty("cameraTarget", target);
            if (auto* header = group->parentWidget()->findChild<LuxUI::ToolButton*>(
                    LUX_UI_TEXT("componentHeader"), LuxUI::Flags::FindDirectChildrenOnly))
                header->setChecked(true);
        }
    }
}

void EditorWindow::OnCameraChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    const auto* previous = document_.GetSelected().TryGetComponent<LuxEngine::CameraComponent>();
    auto value = previous ? *previous : LuxEngine::CameraComponent{};
    const bool enabled = cameraEnabled_->isChecked();
    if (!previous) {
        value.isPerspective = cameraProjection_->currentIndex() == 1;
        for (auto entity : document_.GetScene().GetEntities())
            if (const auto* camera = entity.TryGetComponent<LuxEngine::CameraComponent>();
                camera && camera->isPrimary)
                value.isPrimary = false;
    }
    if (field == cameraPrimary_)
        value.isPrimary = cameraPrimary_->isChecked();
    else if (field == cameraProjection_)
        value.isPerspective = cameraProjection_->currentIndex() == 1;
    else if (field == cameraFov_)
        value.fov = static_cast<float>(cameraFov_->value());
    else if (field == cameraOrthoSize_)
        value.orthoSize = static_cast<float>(cameraOrthoSize_->value());
    else if (field == cameraNearClip_)
        value.nearClip = static_cast<float>(cameraNearClip_->value());
    else if (field == cameraFarClip_)
        value.farClip = static_cast<float>(cameraFarClip_->value());
    if (enabled && (!std::isfinite(value.nearClip) || value.nearClip <= 0 ||
                    !std::isfinite(value.farClip) || value.farClip <= value.nearClip)) {
        ShowError(LUX_UI_TEXT("카메라 설정"),
                  LUX_UI_TEXT("먼 표시 거리는 가까운 표시 거리보다 커야 합니다."));
        RefreshCameraDetails();
        return;
    }
    RecordEdit(LUX_UI_TEXT("카메라 속성 변경"), [this, enabled, value] {
        auto entity = document_.GetSelected();
        if (!enabled) {
            entity.RemoveComponent<LuxEngine::CameraComponent>();
        } else {
            if (value.isPrimary)
                for (auto other : document_.GetScene().GetEntities())
                    if (auto* camera = other.TryGetComponent<LuxEngine::CameraComponent>())
                        camera->isPrimary = false;
            if (!entity.HasComponent<LuxEngine::CameraComponent>())
                entity.AddComponent<LuxEngine::CameraComponent>();
            entity.GetComponent<LuxEngine::CameraComponent>() = value;
        }
        document_.MarkDirty();
    });
    RefreshAll();
}

void EditorWindow::AlignCameraToSceneView() {
    const auto entity = document_.GetSelected();
    if (!entity || !entity.HasComponent<LuxEngine::CameraComponent>())
        return;
    RecordEdit(LUX_UI_TEXT("카메라를 씬 뷰에 맞추기"), [this, id = entity.GetId()] {
        auto& scene = document_.GetScene();
        auto& settings = scene.GetComponent<LuxEngine::CameraComponent>(id);
        LuxEngine::TransformComponent transform;
        try {
            transform = scene.GetWorldTransform(id);
        } catch (const std::exception&) {
            throw std::runtime_error(
                "부모의 크기가 0이거나 변환을 분해할 수 없어 카메라를 맞출 수 없습니다.");
        }
        if (settings.isPerspective) {
            const auto view = ViewTransform(canvas3D_->GetViewCamera());
            transform.position = view.position;
            transform.rotation = view.rotation;
            canvas3D_->GetViewProjection(settings.fov, settings.nearClip, settings.farClip);
        } else {
            double x, y, halfHeight;
            canvas_->GetView(x, y, halfHeight);
            transform.position.x = static_cast<float>(x);
            transform.position.y = static_cast<float>(y);
            transform.rotation = {};
            settings.orthoSize = static_cast<float>(halfHeight);
        }
        if (!scene.SetWorldTransform(id, transform))
            throw std::runtime_error(
                "부모의 크기가 0이거나 현재 변환을 표현할 수 없어 카메라를 맞출 수 없습니다.");
        document_.MarkDirty();
    });
    RefreshAll();
}

void EditorWindow::ViewThroughSelectedCamera() {
    const auto entity = document_.GetSelected();
    if (!entity || !entity.HasComponent<LuxEngine::CameraComponent>())
        return;
    try {
        const auto& settings = entity.GetComponent<LuxEngine::CameraComponent>();
        SetWorkspace(false);
        if (settings.isPerspective) {
            viewportTabs_->setCurrentIndex(0);
            const auto view = EntityCamera(document_.GetScene(), entity.GetId(),
                                           static_cast<float>(std::max(1, canvas3D_->width())) /
                                               std::max(1, canvas3D_->height()));
            if (!canvas3D_->SetViewCamera(view, settings.fov, settings.nearClip, settings.farClip))
                throw std::runtime_error("카메라 시야각 또는 표시 거리가 올바르지 않습니다.");
            if (auto* projection = findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("viewportProjection"))) {
                const LuxUI::SignalBlocker block(projection);
                projection->setCurrentIndex(0);
            }
        } else {
            const auto transform = document_.GetScene().GetWorldTransform(entity.GetId());
            viewportTabs_->setCurrentIndex(1);
            if (!canvas_->SetView(transform.position.x, transform.position.y, settings.orthoSize))
                throw std::runtime_error("2D 카메라 중심 또는 직교 크기가 올바르지 않습니다.");
        }
    } catch (const std::exception& error) {
        ShowError(LUX_UI_TEXT("선택 카메라 시점"), LuxUI::String::fromUtf8(error.what()));
    }
}

void EditorWindow::AddCameraEntity(bool perspective) {
    RecordEdit(
        perspective ? LUX_UI_TEXT("3D 카메라 추가") : LUX_UI_TEXT("2D 카메라 추가"),
        [this, perspective] {
            const auto cameras =
                document_.GetScene().GetComponentEntityIds<LuxEngine::CameraComponent>();
            const bool primary = std::none_of(cameras.begin(), cameras.end(), [this](auto id) {
                const auto& camera =
                    document_.GetScene().GetComponent<LuxEngine::CameraComponent>(id);
                return camera.isPrimary &&
                       document_.GetScene().HasComponent<LuxEngine::TransformComponent>(id) &&
                       (camera.isPerspective
                            ? std::isfinite(camera.fov) && camera.fov > 0 && camera.fov < 180
                            : std::isfinite(camera.orthoSize) && camera.orthoSize > 0);
            });
            if (primary)
                for (const auto id : cameras)
                    document_.GetScene().GetComponent<LuxEngine::CameraComponent>(id).isPrimary =
                        false;
            auto entity = document_.CreateEntity(perspective ? "Camera 3D" : "Camera 2D");
            auto& camera = entity.AddComponent<LuxEngine::CameraComponent>();
            camera.isPerspective = perspective;
            camera.isPrimary = primary;
            if (perspective) {
                auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
                transform.position = {0, 3, 9};
                transform.rotation.x = -.3f;
            }
        });
    SetWorkspace(false);
    viewportTabs_->setCurrentIndex(perspective ? 0 : 1);
    SetTool(EditorTool::Move);
    if (perspective)
        canvas3D_->FocusSelection();
    else
        canvas_->FocusSelection();
    detailsSearch_->clear();
    RefreshAll();
}
} // namespace LuxEditor
