#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Scene/ModelImport.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <algorithm>
#include <cmath>

namespace LuxEditor {
namespace {
LuxEngine::uint32 ModelRoot(EditorDocument& document) {
    const auto selected = document.GetSelected();
    return selected ? FindImportedModelRoot(document.GetScene(), selected.GetId()) : 0;
}

std::vector<LuxEngine::uint32> ModelEntities(LuxEngine::Scene& scene, LuxEngine::uint32 root) {
    std::vector<LuxEngine::uint32> result;
    for (const auto id : scene.GetComponentEntityIds<LuxEngine::ModelAnimationComponent>())
        if (FindImportedModelRoot(scene, id) == root)
            result.push_back(id);
    return result;
}

} // namespace

void EditorWindow::BuildModelAnimationWorkspace(LuxUI::VBoxLayout* workspace) {
    auto* panel = new LuxUI::Widget(animationPanel_);
    panel->setObjectName(LUX_UI_TEXT("modelAnimationWorkspace"));
    auto* layout = new LuxUI::VBoxLayout(panel);
    layout->setContentsMargins(10, 6, 10, 8);
    auto* controls = new LuxUI::HBoxLayout;
    auto* clip = new LuxUI::ComboBox(panel);
    clip->setObjectName(LUX_UI_TEXT("modelAnimationClip"));
    clip->setMinimumWidth(180);
    controls->addWidget(new LuxUI::Label(LUX_UI_TEXT("클립"), panel));
    controls->addWidget(clip, 1);
    auto* loop = new LuxUI::CheckBox(LUX_UI_TEXT("반복"), panel);
    loop->setObjectName(LUX_UI_TEXT("modelAnimationLoop"));
    controls->addWidget(loop);
    auto* autoPlay = new LuxUI::CheckBox(LUX_UI_TEXT("실행 시 재생"), panel);
    autoPlay->setObjectName(LUX_UI_TEXT("modelAnimationAutoPlay"));
    controls->addWidget(autoPlay);
    auto* speed = new LuxUI::DoubleSpinBox(panel);
    speed->setObjectName(LUX_UI_TEXT("modelAnimationSpeed"));
    speed->setRange(-10, 10);
    speed->setDecimals(2);
    speed->setSingleStep(.1);
    speed->setSuffix(LUX_UI_TEXT(" ×"));
    speed->setKeyboardTracking(false);
    controls->addWidget(new LuxUI::Label(LUX_UI_TEXT("속도"), panel));
    controls->addWidget(speed);
    layout->addLayout(controls);
    auto* source = new LuxUI::Label(panel);
    source->setObjectName(LUX_UI_TEXT("modelAnimationSource"));
    source->setTextInteractionFlags(LuxUI::Flags::TextSelectableByMouse);
    source->setMinimumWidth(0);
    source->setSizePolicy(LuxUI::SizePolicy::Ignored, LuxUI::SizePolicy::Fixed);
    layout->addWidget(source);

    const auto select = [this](std::optional<LuxEngine::uint32> id) { SelectEntity(id); };
    const auto transform = [this](LuxEngine::uint32 id,
                                  const LuxEngine::TransformComponent& value) {
        ApplyViewportTransform(id, value);
    };
    modelAnimationPreview_ = new SceneViewport3D(
        panel, document_, select, transform, [this] { BeginViewportEdit(); },
        [this] { EndViewportEdit(); }, std::make_shared<EngineSceneRenderer>());
    modelAnimationPreview_->setObjectName(LUX_UI_TEXT("modelAnimationPreview3D"));
    modelAnimationPreview_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    layout->addWidget(modelAnimationPreview_, 1);
    auto* transport = new LuxUI::HBoxLayout;
    auto* play = new LuxUI::PushButton(panel);
    play->setObjectName(LUX_UI_TEXT("modelAnimationPlay"));
    play->setIcon(MakeEditorIcon(EditorIcon::Play));
    play->setToolTip(LUX_UI_TEXT("클립 미리보기 재생 / 일시정지"));
    auto* stop = new LuxUI::PushButton(panel);
    stop->setObjectName(LUX_UI_TEXT("modelAnimationStop"));
    stop->setIcon(MakeEditorIcon(EditorIcon::Stop));
    stop->setToolTip(LUX_UI_TEXT("클립 미리보기 정지"));
    transport->addWidget(play);
    transport->addWidget(stop);
    auto* slider = new LuxUI::Slider(LuxUI::Flags::Horizontal, panel);
    slider->setObjectName(LUX_UI_TEXT("modelAnimationTimeline"));
    slider->setRange(0, 10000);
    transport->addWidget(slider, 1);
    auto* time = new LuxUI::DoubleSpinBox(panel);
    time->setObjectName(LUX_UI_TEXT("modelAnimationTime"));
    time->setRange(0, 3600);
    time->setDecimals(3);
    time->setSuffix(LUX_UI_TEXT(" s"));
    time->setKeyboardTracking(false);
    transport->addWidget(time);
    layout->addLayout(transport);
    workspace->addWidget(panel, 1);
    panel->hide();

    connect(clip, &LuxUI::ComboBox::currentIndexChanged, this, [this, clip](int index) {
        if (refreshingInspector_ || index < 0)
            return;
        StopAnimationPlayback();
        const auto root = ModelRoot(document_);
        if (!root)
            return;
        RecordEdit(LUX_UI_TEXT("모델 애니메이션 클립 변경"), [this, root, clip] {
            for (const auto id : ModelEntities(document_.GetScene(), root)) {
                auto& animation =
                    document_.GetScene().GetComponent<LuxEngine::ModelAnimationComponent>(id);
                animation.clip = clip->currentData().toInt();
                animation.time = 0;
            }
            SeekModelAnimationTime(0);
        });
        RefreshInspector();
    });
    const auto settingsChanged = [this, loop, autoPlay, speed](const LuxUI::Widget* field) {
        if (refreshingInspector_)
            return;
        StopAnimationPlayback();
        const auto root = ModelRoot(document_);
        if (!root)
            return;
        RecordEdit(
            LUX_UI_TEXT("모델 애니메이션 설정 변경"), [this, root, field, loop, autoPlay, speed] {
                for (const auto id : ModelEntities(document_.GetScene(), root)) {
                    auto& animation =
                        document_.GetScene().GetComponent<LuxEngine::ModelAnimationComponent>(id);
                    if (field == loop)
                        animation.loop = loop->isChecked();
                    else if (field == autoPlay)
                        animation.playing = autoPlay->isChecked();
                    else if (field == speed)
                        animation.speed = speed->value();
                }
                document_.MarkDirty();
            });
        RefreshInspector();
    };
    connect(loop, &LuxUI::CheckBox::toggled, this,
            [settingsChanged, loop] { settingsChanged(loop); });
    connect(autoPlay, &LuxUI::CheckBox::toggled, this,
            [settingsChanged, autoPlay] { settingsChanged(autoPlay); });
    connect(speed, &LuxUI::DoubleSpinBox::editingFinished, this,
            [settingsChanged, speed] { settingsChanged(speed); });
    connect(time, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this](double value) {
                if (refreshingInspector_)
                    return;
                StopAnimationPlayback();
                timelineBefore_ = SnapshotScene();
                SeekModelAnimationTime(value);
                EndTimelineEdit();
            });
    connect(slider, &LuxUI::Slider::sliderPressed, this, [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
    });
    connect(slider, &LuxUI::Slider::valueChanged, this, [this, slider](int value) {
        if (refreshingInspector_ || !modelAnimationAsset_)
            return;
        const auto root = ModelRoot(document_);
        const auto* animation =
            document_.GetScene().TryGetComponent<LuxEngine::ModelAnimationComponent>(root);
        if (!animation || animation->clip < 0 ||
            static_cast<size_t>(animation->clip) >= modelAnimationAsset_->animations.size())
            return;
        const bool discrete = !slider->isSliderDown();
        if (discrete) {
            StopAnimationPlayback();
            timelineBefore_ = SnapshotScene();
        }
        SeekModelAnimationTime(modelAnimationAsset_->animations[animation->clip].duration * value /
                               10000.0);
        if (discrete)
            EndTimelineEdit();
    });
    connect(slider, &LuxUI::Slider::sliderReleased, this, [this] { EndTimelineEdit(); });
    connect(stop, &LuxUI::PushButton::clicked, this, [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        SeekModelAnimationTime(0);
        EndTimelineEdit();
    });
    connect(play, &LuxUI::PushButton::clicked, this, [this, play] {
        if (animationTimer_.isActive()) {
            StopAnimationPlayback();
            return;
        }
        const auto root = ModelRoot(document_);
        const auto* animation =
            document_.GetScene().TryGetComponent<LuxEngine::ModelAnimationComponent>(root);
        if (!animation || !modelAnimationAsset_ || animation->clip < 0 ||
            static_cast<size_t>(animation->clip) >= modelAnimationAsset_->animations.size() ||
            modelAnimationAsset_->animations[animation->clip].duration <= 0)
            return;
        StopPreview();
        timelineBefore_ = SnapshotScene();
        const auto tab = document_.GetActiveTabId();
        auto clock = std::make_shared<LuxUI::ElapsedTimer>();
        clock->start();
        animationTimer_.disconnect(this);
        connect(&animationTimer_, &LuxUI::Timer::timeout, this, [this, root, tab, clock] {
            const auto* current =
                document_.GetScene().TryGetComponent<LuxEngine::ModelAnimationComponent>(root);
            if (tab != document_.GetActiveTabId() || ModelRoot(document_) != root || !current ||
                !modelAnimationAsset_ || current->clip < 0 ||
                static_cast<size_t>(current->clip) >= modelAnimationAsset_->animations.size()) {
                StopAnimationPlayback();
                return;
            }
            const double duration = modelAnimationAsset_->animations[current->clip].duration;
            double next = current->time + clock->restart() / 1000.0 * current->speed;
            const bool ended = next >= duration || next < 0;
            const bool looping = current->loop;
            if (looping && duration > 0) {
                next = std::fmod(next, duration);
                if (next < 0)
                    next += duration;
            }
            SeekModelAnimationTime(next);
            if (ended && !looping)
                StopAnimationPlayback();
        });
        animationTimer_.start(16);
        play->setIcon(MakeEditorIcon(EditorIcon::Pause));
    });
}

void EditorWindow::SetModelAnimationMode(bool enabled) {
    for (const auto name : {"animationMainSplitter", "animationSequence", "animationTransport"})
        if (auto* widget =
                animationPanel_->findChild<LuxUI::Widget*>(LuxUI::String::fromLatin1(name)))
            widget->setVisible(!enabled);
    for (const auto name : {"animationAutoPlay", "animationRemoveClip", "animationCreateClip",
                            "animationFocusTarget"})
        if (auto* widget =
                animationPanel_->findChild<LuxUI::Widget*>(LuxUI::String::fromLatin1(name)))
            widget->setVisible(!enabled);
    for (int index = 0; index < 4; ++index)
        if (auto* widget = animationPanel_->findChild<LuxUI::Widget*>(
                LUX_UI_TEXT("animationTool%1").arg(index)))
            widget->setVisible(!enabled);
    if (auto* widget =
            animationPanel_->findChild<LuxUI::Widget*>(LUX_UI_TEXT("modelAnimationWorkspace")))
        widget->setVisible(enabled);
}

void EditorWindow::RefreshModelAnimationWorkspace() {
    if (!modelAnimationPreview_)
        return;
    const auto root = ModelRoot(document_);
    const auto* animation =
        document_.GetScene().TryGetComponent<LuxEngine::ModelAnimationComponent>(root);
    std::string error;
    auto model = animation ? LuxEngine::ModelAsset::LoadFbx(
                                 document_.GetScene().ResolveAssetPath(animation->modelPath), error)
                           : nullptr;
    auto* sourceMode =
        animationPanel_->findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("animationSourceMode"));
    const auto selected = document_.GetSelected();
    const auto target =
        LUX_UI_TEXT("%1:%2").arg(document_.GetActiveTabId()).arg(selected ? selected.GetId() : 0);
    if (sourceMode->property("target").toString() != target) {
        sourceMode->setProperty("target", target);
        const LuxUI::SignalBlocker block(sourceMode);
        sourceMode->setCurrentIndex(animation ? 1 : 0);
        if (animation)
            modelAnimationPreview_->FocusSelection();
    }
    SetModelAnimationMode(sourceMode->currentIndex() == 1);
    auto* clip = animationPanel_->findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("modelAnimationClip"));
    if (model != modelAnimationAsset_) {
        const LuxUI::SignalBlocker block(clip);
        clip->clear();
        clip->addItem(LUX_UI_TEXT("바인드 포즈"), -1);
        if (model)
            for (size_t index = 0; index < model->animations.size(); ++index)
                clip->addItem(LuxUI::String::fromUtf8(model->animations[index].name),
                              static_cast<int>(index));
        modelAnimationAsset_ = std::move(model);
    }
    const bool available = animation && modelAnimationAsset_;
    clip->setEnabled(available);
    const LuxUI::SignalBlocker blockClip(clip);
    clip->setCurrentIndex(available ? clip->findData(animation->clip) : 0);
    const double duration =
        available && animation->clip >= 0 &&
                static_cast<size_t>(animation->clip) < modelAnimationAsset_->animations.size()
            ? modelAnimationAsset_->animations[animation->clip].duration
            : 0;
    for (const auto name : {"modelAnimationLoop", "modelAnimationAutoPlay"}) {
        auto* field = animationPanel_->findChild<LuxUI::CheckBox*>(LuxUI::String::fromLatin1(name));
        const LuxUI::SignalBlocker block(field);
        field->setEnabled(available);
        field->setChecked(available &&
                          (name == std::string_view("modelAnimationLoop") ? animation->loop
                                                                          : animation->playing));
    }
    auto* speed =
        animationPanel_->findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("modelAnimationSpeed"));
    const LuxUI::SignalBlocker blockSpeed(speed);
    speed->setEnabled(available);
    speed->setValue(available ? animation->speed : 1);
    auto* time =
        animationPanel_->findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("modelAnimationTime"));
    const LuxUI::SignalBlocker blockTime(time);
    time->setEnabled(duration > 0);
    time->setRange(0, duration);
    time->setValue(available ? animation->time : 0);
    auto* slider =
        animationPanel_->findChild<LuxUI::Slider*>(LUX_UI_TEXT("modelAnimationTimeline"));
    const LuxUI::SignalBlocker blockSlider(slider);
    slider->setEnabled(duration > 0);
    slider->setValue(duration > 0 ? static_cast<int>(animation->time / duration * 10000) : 0);
    animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelAnimationPlay"))
        ->setEnabled(duration > 0);
    animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelAnimationStop"))
        ->setEnabled(available);
    auto* path = animationPanel_->findChild<LuxUI::Label*>(LUX_UI_TEXT("modelAnimationSource"));
    const auto sourceText = available ? LuxUI::String::fromUtf8(modelAnimationAsset_->sourcePath)
                            : error.empty() ? LUX_UI_TEXT("FBX 모델을 선택해 주세요.")
                                            : LuxUI::String::fromUtf8(error);
    path->setText(path->fontMetrics().elidedText(sourceText, LuxUI::Flags::ElideMiddle,
                                                 std::max(100, animationPanel_->width() - 32)));
    path->setToolTip(sourceText);
    if (available && sourceMode->currentIndex() == 1)
        animationTarget_->setText(
            LUX_UI_TEXT("FBX · %1개 클립").arg(modelAnimationAsset_->animations.size()));
    modelAnimationPreview_->RefreshScene();
}

void EditorWindow::SeekModelAnimationTime(double time) {
    const auto root = ModelRoot(document_);
    const auto* selected =
        document_.GetScene().TryGetComponent<LuxEngine::ModelAnimationComponent>(root);
    if (!selected || !modelAnimationAsset_ || !std::isfinite(time))
        return;
    const auto clip = selected->clip;
    const double duration =
        clip >= 0 && static_cast<size_t>(clip) < modelAnimationAsset_->animations.size()
            ? modelAnimationAsset_->animations[clip].duration
            : 0;
    time = std::clamp(time, 0.0, duration);
    LuxEngine::ModelPose pose;
    std::string error;
    if (!modelAnimationAsset_->Evaluate(time, clip, pose, error)) {
        Log(LuxUI::String::fromUtf8(error), true);
        return;
    }
    for (const auto id : ModelEntities(document_.GetScene(), root)) {
        auto& animation = document_.GetScene().GetComponent<LuxEngine::ModelAnimationComponent>(id);
        animation.time = time;
        animation.clip = clip;
        if (animation.applyNodeTransform && animation.nodeIndex < pose.nodeLocalTransforms.size()) {
            const auto& node = pose.nodeLocalTransforms[animation.nodeIndex];
            document_.GetScene().GetComponent<LuxEngine::TransformComponent>(id) = {
                node.position, node.rotation, node.scale};
        }
    }
    document_.MarkDirty();
    const bool refreshing = refreshingInspector_;
    refreshingInspector_ = true;
    RefreshModelAnimationWorkspace();
    refreshingInspector_ = refreshing;
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    if (animationPreview3D_)
        animationPreview3D_->RefreshScene();
    UpdateChrome();
}
} // namespace LuxEditor
