#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace LuxEditor {
void EditorWindow::BuildCameraDetails(QVBoxLayout* layout) {
    auto* group = new QGroupBox(QStringLiteral("Camera"), inspectorBody_);
    auto* form = new QFormLayout(group);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    cameraEnabled_ = new QCheckBox(QStringLiteral("카메라 사용"), group);
    cameraEnabled_->setObjectName(QStringLiteral("cameraEnabled"));
    form->addRow(cameraEnabled_);
    cameraPrimary_ = new QCheckBox(QStringLiteral("게임 기본 카메라"), group);
    cameraPrimary_->setObjectName(QStringLiteral("cameraPrimary"));
    form->addRow(cameraPrimary_);
    cameraProjection_ = new QComboBox(group);
    cameraProjection_->setObjectName(QStringLiteral("cameraProjection"));
    cameraProjection_->addItems({QStringLiteral("직교"), QStringLiteral("원근")});
    form->addRow(QStringLiteral("투영"), cameraProjection_);
    cameraFov_ = new QDoubleSpinBox(group);
    cameraFov_->setObjectName(QStringLiteral("cameraFov"));
    cameraFov_->setRange(1, 179);
    cameraFov_->setSuffix(QStringLiteral("°"));
    cameraFov_->setKeyboardTracking(false);
    form->addRow(QStringLiteral("시야각"), cameraFov_);
    cameraOrthoSize_ = new QDoubleSpinBox(group);
    cameraOrthoSize_->setObjectName(QStringLiteral("cameraOrthoSize"));
    cameraOrthoSize_->setRange(.01, 100000);
    cameraOrthoSize_->setKeyboardTracking(false);
    form->addRow(QStringLiteral("직교 크기"), cameraOrthoSize_);
    layout->addWidget(group);
    connect(cameraEnabled_, &QCheckBox::toggled, this, [this] { OnCameraChanged(); });
    connect(cameraPrimary_, &QCheckBox::toggled, this, [this] { OnCameraChanged(); });
    connect(cameraProjection_, &QComboBox::currentIndexChanged, this,
            [this] { OnCameraChanged(); });
    for (auto* field : {cameraFov_, cameraOrthoSize_})
        connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this] { OnCameraChanged(); });
}

void EditorWindow::RefreshCameraDetails() {
    const auto entity = document_.GetSelected();
    const auto* camera = entity.TryGetComponent<LuxEngine::CameraComponent>();
    const QSignalBlocker enabled(cameraEnabled_), primary(cameraPrimary_),
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
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected())
        return;
    const bool enabled = cameraEnabled_->isChecked();
    const LuxEngine::CameraComponent value{
        cameraPrimary_->isChecked(), cameraProjection_->currentIndex() == 1,
        static_cast<float>(cameraFov_->value()), static_cast<float>(cameraOrthoSize_->value())};
    RecordEdit(QStringLiteral("카메라 속성 변경"), [this, enabled, value] {
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
    if (previewActive_)
        return;
    RecordEdit(QStringLiteral("카메라 추가"), [this] {
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
