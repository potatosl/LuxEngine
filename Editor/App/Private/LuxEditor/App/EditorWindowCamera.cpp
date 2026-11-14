#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
void EditorWindow::BuildCameraDetails(LuxUI::VBoxLayout* layout) {
    auto* group = new LuxUI::GroupBox(LUX_UI_TEXT("Camera"), inspectorBody_);
    auto* form = new LuxUI::FormLayout(group);
    form->setRowWrapPolicy(LuxUI::FormLayout::WrapLongRows);
    cameraEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("카메라 사용"), group);
    cameraEnabled_->setObjectName(LUX_UI_TEXT("cameraEnabled"));
    form->addRow(cameraEnabled_);
    cameraPrimary_ = new LuxUI::CheckBox(LUX_UI_TEXT("게임 기본 카메라"), group);
    cameraPrimary_->setObjectName(LUX_UI_TEXT("cameraPrimary"));
    form->addRow(cameraPrimary_);
    cameraProjection_ = new LuxUI::ComboBox(group);
    cameraProjection_->setObjectName(LUX_UI_TEXT("cameraProjection"));
    cameraProjection_->addItems({LUX_UI_TEXT("직교"), LUX_UI_TEXT("원근")});
    form->addRow(LUX_UI_TEXT("투영"), cameraProjection_);
    cameraFov_ = new LuxUI::DoubleSpinBox(group);
    cameraFov_->setObjectName(LUX_UI_TEXT("cameraFov"));
    cameraFov_->setRange(1, 179);
    cameraFov_->setSuffix(LUX_UI_TEXT("°"));
    cameraFov_->setKeyboardTracking(false);
    form->addRow(LUX_UI_TEXT("시야각"), cameraFov_);
    cameraOrthoSize_ = new LuxUI::DoubleSpinBox(group);
    cameraOrthoSize_->setObjectName(LUX_UI_TEXT("cameraOrthoSize"));
    cameraOrthoSize_->setRange(.01, 100000);
    cameraOrthoSize_->setKeyboardTracking(false);
    form->addRow(LUX_UI_TEXT("직교 크기"), cameraOrthoSize_);
    layout->addWidget(group);
    connect(cameraEnabled_, &LuxUI::CheckBox::toggled, this, [this] { OnCameraChanged(); });
    connect(cameraPrimary_, &LuxUI::CheckBox::toggled, this, [this] { OnCameraChanged(); });
    connect(cameraProjection_, &LuxUI::ComboBox::currentIndexChanged, this,
            [this] { OnCameraChanged(); });
    for (auto* field : {cameraFov_, cameraOrthoSize_})
        connect(field, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                [this] { OnCameraChanged(); });
}

void EditorWindow::RefreshCameraDetails() {
    const auto entity = document_.GetSelected();
    const auto* camera = entity.TryGetComponent<LuxEngine::CameraComponent>();
    const LuxUI::SignalBlocker enabled(cameraEnabled_), primary(cameraPrimary_),
        projection(cameraProjection_), fov(cameraFov_), ortho(cameraOrthoSize_);
    cameraEnabled_->setChecked(camera != nullptr);
    cameraPrimary_->setChecked(camera && camera->isPrimary);
    cameraProjection_->setCurrentIndex(camera ? (camera->isPerspective ? 1 : 0)
                                              : (viewportTabs_->currentIndex() == 0 ? 1 : 0));
    cameraFov_->setValue(camera ? camera->fov : 45);
    cameraOrthoSize_->setValue(camera ? camera->orthoSize : 10);
    cameraPrimary_->setEnabled(camera != nullptr);
    cameraProjection_->setEnabled(camera != nullptr);
    cameraFov_->setEnabled(camera && camera->isPerspective);
    cameraOrthoSize_->setEnabled(camera && !camera->isPerspective);
}

void EditorWindow::OnCameraChanged() {
    if (refreshingInspector_ || !document_.GetSelected())
        return;
    const bool enabled = cameraEnabled_->isChecked();
    const LuxEngine::CameraComponent value{
        cameraPrimary_->isChecked(), cameraProjection_->currentIndex() == 1,
        static_cast<float>(cameraFov_->value()), static_cast<float>(cameraOrthoSize_->value())};
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

void EditorWindow::AddCameraEntity() {
    RecordEdit(LUX_UI_TEXT("카메라 추가"), [this] {
        bool primary = true;
        for (auto entity : document_.GetScene().GetEntities())
            if (const auto* camera = entity.TryGetComponent<LuxEngine::CameraComponent>())
                primary &= !camera->isPrimary;
        auto entity = document_.CreateEntity("Camera");
        auto& camera = entity.AddComponent<LuxEngine::CameraComponent>();
        camera.isPerspective = true;
        camera.isPrimary = primary;
        auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
        transform.position = {0, 3, 9};
        transform.rotation.x = -.3f;
    });
    RefreshAll();
}
} // namespace LuxEditor
