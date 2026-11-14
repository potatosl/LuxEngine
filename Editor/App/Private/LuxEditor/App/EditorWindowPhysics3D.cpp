#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>

namespace LuxEditor {
void EditorWindow::BuildPhysics3DDetails(LuxUI::VBoxLayout* layout) {
    auto* group = new LuxUI::GroupBox(LUX_UI_TEXT("Jolt Physics 3D"), inspectorBody_);
    auto* form = new LuxUI::FormLayout(group);
    body3DEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("Rigidbody 3D"), group);
    body3DEnabled_->setObjectName(LUX_UI_TEXT("body3DEnabled"));
    collider3DEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("Box Collider 3D"), group);
    collider3DEnabled_->setObjectName(LUX_UI_TEXT("collider3DEnabled"));
    body3DType_ = new LuxUI::ComboBox(group);
    body3DType_->setObjectName(LUX_UI_TEXT("body3DType"));
    body3DType_->addItems({LUX_UI_TEXT("고정"), LUX_UI_TEXT("동적"), LUX_UI_TEXT("키네마틱")});
    const auto number = [group](double low, double high, double value) {
        auto* field = new LuxUI::DoubleSpinBox(group);
        field->setRange(low, high);
        field->setDecimals(3);
        field->setValue(value);
        field->setKeyboardTracking(false);
        return field;
    };
    body3DMass_ = number(.001, 1000000, 1);
    body3DMass_->setObjectName(LUX_UI_TEXT("body3DMass"));
    body3DGravity_ = number(-10000, 10000, 1);
    body3DGravity_->setObjectName(LUX_UI_TEXT("body3DGravity"));
    trigger3D_ = new LuxUI::CheckBox(LUX_UI_TEXT("트리거"), group);
    trigger3D_->setObjectName(LUX_UI_TEXT("trigger3D"));
    form->addRow(body3DEnabled_);
    form->addRow(collider3DEnabled_);
    form->addRow(LUX_UI_TEXT("유형"), body3DType_);
    form->addRow(LUX_UI_TEXT("질량"), body3DMass_);
    form->addRow(LUX_UI_TEXT("중력 배율"), body3DGravity_);
    const char* axes[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < 3; ++i) {
        collider3DSize_[i] = number(.02, 10000, 1);
        collider3DOffset_[i] = number(-10000, 10000, 0);
        collider3DSize_[i]->setObjectName(LUX_UI_TEXT("collider3DSize%1").arg(i));
        collider3DOffset_[i]->setObjectName(LUX_UI_TEXT("collider3DOffset%1").arg(i));
        form->addRow(LUX_UI_TEXT("크기 %1").arg(axes[i]), collider3DSize_[i]);
        form->addRow(LUX_UI_TEXT("오프셋 %1").arg(axes[i]), collider3DOffset_[i]);
    }
    form->addRow(trigger3D_);
    layout->addWidget(group);
    const auto update = [this](const LuxUI::Widget* field) {
        if (refreshingInspector_)
            return;
        RecordEdit(LUX_UI_TEXT("Jolt 3D 물리 설정"), [this, field] {
            auto entity = document_.GetSelected();
            if (!entity)
                return;
            if (field == body3DEnabled_) {
                if (body3DEnabled_->isChecked() &&
                    !entity.HasComponent<LuxEngine::Rigidbody3DComponent>())
                    entity.AddComponent<LuxEngine::Rigidbody3DComponent>();
                else if (!body3DEnabled_->isChecked() &&
                         entity.HasComponent<LuxEngine::Rigidbody3DComponent>())
                    entity.RemoveComponent<LuxEngine::Rigidbody3DComponent>();
            }
            if (field == collider3DEnabled_) {
                if (collider3DEnabled_->isChecked() &&
                    !entity.HasComponent<LuxEngine::BoxCollider3DComponent>())
                    entity.AddComponent<LuxEngine::BoxCollider3DComponent>();
                else if (!collider3DEnabled_->isChecked() &&
                         entity.HasComponent<LuxEngine::BoxCollider3DComponent>())
                    entity.RemoveComponent<LuxEngine::BoxCollider3DComponent>();
            }
            if (auto* body = entity.TryGetComponent<LuxEngine::Rigidbody3DComponent>()) {
                if (field == body3DType_)
                    body->type = static_cast<LuxEngine::BodyType>(body3DType_->currentIndex());
                if (field == body3DMass_)
                    body->mass = static_cast<float>(body3DMass_->value());
                if (field == body3DGravity_)
                    body->gravityScale = static_cast<float>(body3DGravity_->value());
            }
            if (auto* collider = entity.TryGetComponent<LuxEngine::BoxCollider3DComponent>()) {
                if (field == trigger3D_)
                    collider->isTrigger = trigger3D_->isChecked();
                float* sizes[] = {&collider->size.x, &collider->size.y, &collider->size.z};
                float* offsets[] = {&collider->offset.x, &collider->offset.y, &collider->offset.z};
                for (size_t i = 0; i < 3; ++i) {
                    if (field == collider3DSize_[i])
                        *sizes[i] = static_cast<float>(collider3DSize_[i]->value());
                    if (field == collider3DOffset_[i])
                        *offsets[i] = static_cast<float>(collider3DOffset_[i]->value());
                }
            }
            document_.MarkDirty();
        });
        RefreshInspector();
        canvas3D_->update();
    };
    for (auto* field : {body3DEnabled_, collider3DEnabled_, trigger3D_})
        connect(field, &LuxUI::CheckBox::toggled, this, [update, field] { update(field); });
    connect(body3DType_, &LuxUI::ComboBox::currentIndexChanged, this,
            [update, this] { update(body3DType_); });
    for (auto* field :
         {body3DMass_, body3DGravity_, collider3DSize_[0], collider3DSize_[1], collider3DSize_[2],
          collider3DOffset_[0], collider3DOffset_[1], collider3DOffset_[2]})
        connect(field, &LuxUI::DoubleSpinBox::editingFinished, this,
                [update, field] { update(field); });
}

void EditorWindow::RefreshPhysics3DDetails() {
    if (!body3DEnabled_)
        return;
    const auto entity = document_.GetSelected();
    const auto* body = entity.TryGetComponent<LuxEngine::Rigidbody3DComponent>();
    const auto* collider = entity.TryGetComponent<LuxEngine::BoxCollider3DComponent>();
    const LuxUI::SignalBlocker b(body3DEnabled_), c(collider3DEnabled_), t(body3DType_),
        trigger(trigger3D_);
    body3DEnabled_->setChecked(body != nullptr);
    collider3DEnabled_->setChecked(collider != nullptr);
    body3DType_->setCurrentIndex(body ? static_cast<int>(body->type) : 1);
    body3DMass_->setValue(body ? body->mass : 1);
    body3DGravity_->setValue(body ? body->gravityScale : 1);
    trigger3D_->setChecked(collider && collider->isTrigger);
    body3DType_->setEnabled(body != nullptr);
    body3DMass_->setEnabled(body != nullptr);
    body3DGravity_->setEnabled(body != nullptr);
    trigger3D_->setEnabled(collider != nullptr);
    for (size_t i = 0; i < 3; ++i) {
        collider3DSize_[i]->setEnabled(collider != nullptr);
        collider3DOffset_[i]->setEnabled(collider != nullptr);
    }
    const auto size = collider ? collider->size : LuxEngine::Vec3{1, 1, 1};
    const auto offset = collider ? collider->offset : LuxEngine::Vec3{};
    const float sizes[] = {size.x, size.y, size.z}, offsets[] = {offset.x, offset.y, offset.z};
    for (size_t i = 0; i < 3; ++i) {
        collider3DSize_[i]->setValue(sizes[i]);
        collider3DOffset_[i]->setValue(offsets[i]);
    }
}
} // namespace LuxEditor
