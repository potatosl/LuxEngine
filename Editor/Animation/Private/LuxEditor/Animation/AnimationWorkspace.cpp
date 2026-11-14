#include <LuxEditor/Animation/AnimationTimeline.hpp>
#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Scene/SceneHistory.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <cmath>

namespace LuxEditor {
namespace {
std::optional<LuxEngine::uint32> SelectedId(EditorDocument& doc) {
    return doc.GetSelectedId(doc.GetActiveTabId());
}

class KeyList final : public LuxUI::ListWidget {
public:
    KeyList(LuxUI::Widget* parent, std::function<void()> remove)
        : LuxUI::ListWidget(parent), remove_(std::move(remove)) {}

private:
    std::function<void()> remove_;

    bool event(LuxUI::Event* event) override {
        if (event->type() == LuxUI::Event::ShortcutOverride &&
            static_cast<LuxUI::KeyEvent*>(event)->key() == LuxUI::Flags::Key_Delete) {
            event->accept();
            return true;
        }
        return LuxUI::ListWidget::event(event);
    }

    void keyPressEvent(LuxUI::KeyEvent* event) override {
        if (event->key() == LuxUI::Flags::Key_Delete) {
            remove_();
            event->accept();
        } else
            LuxUI::ListWidget::keyPressEvent(event);
    }
};

} // namespace

void EditorWindow::StopAnimationPlayback() {
    if (animationPanel_)
        if (auto* play =
                animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("modelAnimationPlay")))
            play->setIcon(MakeEditorIcon(EditorIcon::Play));
    if (animationPanel_)
        if (auto* play =
                animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("animationPlay")))
            play->setIcon(MakeEditorIcon(EditorIcon::Play));
    if (!animationTimer_.isActive())
        return;
    animationTimer_.stop();
    EndTimelineEdit();
}

void EditorWindow::BuildAnimationPanel() {
    // Transform tracks and imported FBX clips share the workspace, with separate authoring tools.
    animationDock_ = new LuxUI::DockWidget(LUX_UI_TEXT("애니메이션"), this);
    animationDock_->setObjectName(LUX_UI_TEXT("AnimationWorkspace"));
    animationPanel_ = new LuxUI::Widget(animationDock_);
    animationPanel_->setObjectName(LUX_UI_TEXT("animationWorkspace"));
    auto* workspace = new LuxUI::VBoxLayout(animationPanel_);
    workspace->setContentsMargins(0, 0, 0, 0);
    workspace->setSpacing(0);

    auto* header = new LuxUI::Widget(animationPanel_);
    header->setObjectName(LUX_UI_TEXT("animationWorkspaceHeader"));
    auto* heading = new LuxUI::HBoxLayout(header);
    heading->setContentsMargins(12, 6, 12, 6);
    animationTarget_ = new LuxUI::Label(header);
    animationTarget_->setMinimumWidth(100);
    heading->addWidget(animationTarget_);
    auto* sourceMode = new LuxUI::ComboBox(header);
    sourceMode->setObjectName(LUX_UI_TEXT("animationSourceMode"));
    sourceMode->addItems({LUX_UI_TEXT("변환 클립"), LUX_UI_TEXT("FBX 클립")});
    heading->addWidget(sourceMode);
    connect(sourceMode, &LuxUI::ComboBox::currentIndexChanged, this, [this](int index) {
        StopAnimationPlayback();
        SetModelAnimationMode(index == 1);
    });
    heading->addStretch();
    auto* save = new LuxUI::PushButton(header);
    save->setObjectName(LUX_UI_TEXT("animationSave"));
    save->setIcon(MakeEditorIcon(EditorIcon::Save));
    save->setFixedSize(32, 30);
    save->setToolTip(LUX_UI_TEXT("애니메이션을 포함한 씬 저장 (Ctrl+S)"));
    heading->addWidget(save);
    connect(save, &LuxUI::PushButton::clicked, this, [this] { SaveScene(false); });
    auto* previewTools = new LuxUI::ButtonGroup(header);
    previewTools->setExclusive(true);
    const EditorIcon icons[] = {EditorIcon::Select, EditorIcon::Move, EditorIcon::Rotate,
                                EditorIcon::Scale};
    const LuxUI::StringList toolNames{LUX_UI_TEXT("선택 (Q)"), LUX_UI_TEXT("이동 (W)"),
                                      LUX_UI_TEXT("회전 (E)"), LUX_UI_TEXT("크기 (R)")};
    for (int index = 0; index < 4; ++index) {
        auto* tool = new LuxUI::ToolButton(header);
        tool->setObjectName(LUX_UI_TEXT("animationTool%1").arg(index));
        tool->setIcon(MakeEditorIcon(icons[index]));
        tool->setToolTip(toolNames[index]);
        const int keys[] = {LuxUI::Flags::Key_Q, LuxUI::Flags::Key_W, LuxUI::Flags::Key_E,
                            LuxUI::Flags::Key_R};
        tool->setShortcut(LuxUI::KeySequence(keys[index]));
        tool->setCheckable(true);
        tool->setChecked(index == 0);
        previewTools->addButton(tool, index);
        heading->addWidget(tool);
        connect(tool, &LuxUI::ToolButton::clicked, this,
                [this, index] { SetAnimationTool(static_cast<EditorTool>(index)); });
    }
    auto* focusPreview = new LuxUI::ToolButton(header);
    focusPreview->setObjectName(LUX_UI_TEXT("animationFocusTarget"));
    focusPreview->setIcon(MakeEditorIcon(EditorIcon::Focus));
    focusPreview->setToolTip(LUX_UI_TEXT("애니메이션 대상에 초점"));
    heading->addWidget(focusPreview);
    connect(focusPreview, &LuxUI::ToolButton::clicked, this, [this] {
        if (animationPreview2D_)
            animationPreview2D_->FocusSelection();
        if (animationPreview3D_)
            animationPreview3D_->FocusSelection();
    });
    // Keep component attachment separate from runtime playback. Disabling playback must never
    // discard authored keys. The hidden attachment control remains the single creation/removal
    // path.
    animationEnabled_ = new LuxUI::CheckBox(header);
    animationEnabled_->setObjectName(LUX_UI_TEXT("editorAnimation"));
    animationEnabled_->hide();
    auto* autoPlay = new LuxUI::CheckBox(LUX_UI_TEXT("자동 재생"), header);
    autoPlay->setObjectName(LUX_UI_TEXT("animationAutoPlay"));
    autoPlay->setToolTip(
        LUX_UI_TEXT("게임 실행 시 클립을 자동으로 재생합니다. 키프레임은 유지됩니다."));
    heading->addWidget(autoPlay);
    connect(autoPlay, &LuxUI::CheckBox::toggled, this, [this](bool playing) {
        if (refreshingInspector_ || previewActive_)
            return;
        auto entity = document_.GetSelected();
        if (!entity || !entity.HasComponent<LuxEngine::AnimationComponent>())
            return;
        StopAnimationPlayback();
        RecordEdit(LUX_UI_TEXT("애니메이션 자동 재생 변경"), [this, playing] {
            document_.GetSelected().GetComponent<LuxEngine::AnimationComponent>().playing = playing;
            document_.MarkDirty();
        });
        RefreshInspector();
        UpdateChrome();
    });
    auto* removeClip = new LuxUI::ToolButton(header);
    removeClip->setObjectName(LUX_UI_TEXT("animationRemoveClip"));
    removeClip->setIcon(MakeEditorIcon(EditorIcon::Delete));
    removeClip->setToolTip(LUX_UI_TEXT("클립 제거 · 실행 취소로 복원할 수 있습니다."));
    heading->addWidget(removeClip);
    connect(removeClip, &LuxUI::ToolButton::clicked, this,
            [this] { animationEnabled_->setChecked(false); });
    auto* createClip = new LuxUI::PushButton(LUX_UI_TEXT("새 변환 클립"), header);
    createClip->setObjectName(LUX_UI_TEXT("animationCreateClip"));
    heading->addWidget(createClip);
    connect(createClip, &LuxUI::PushButton::clicked, this, [this] {
        if (!animationEnabled_->isChecked())
            animationEnabled_->setChecked(true);
        animationName_->setFocus();
        animationName_->selectAll();
    });
    workspace->addWidget(header);

    auto* mainSplit = new LuxUI::Splitter(LuxUI::Flags::Horizontal, animationPanel_);
    mainSplit->setObjectName(LUX_UI_TEXT("animationMainSplitter"));
    mainSplit->setChildrenCollapsible(false);
    auto* browser = new LuxUI::Widget(mainSplit);
    browser->setMinimumWidth(160);
    auto* browserLayout = new LuxUI::VBoxLayout(browser);
    browserLayout->setContentsMargins(10, 10, 10, 10);
    auto* browserTitle = new LuxUI::Label(LUX_UI_TEXT("클립 · 키프레임"), browser);
    browserTitle->setObjectName(LUX_UI_TEXT("sectionCaption"));
    browserLayout->addWidget(browserTitle);
    animationName_ = new LuxUI::LineEdit(browser);
    animationName_->setObjectName(LUX_UI_TEXT("animationClipName"));
    animationName_->setPlaceholderText(LUX_UI_TEXT("클립 이름"));
    animationName_->setMaxLength(256);
    browserLayout->addWidget(animationName_);
    auto* keyCount = new LuxUI::Label(browser);
    keyCount->setObjectName(LUX_UI_TEXT("animationKeyCount"));
    browserLayout->addWidget(keyCount);
    keyframeList_ = new KeyList(browser, [this] { RemoveKeyframe(); });
    keyframeList_->setObjectName(LUX_UI_TEXT("animationKeys"));
    keyframeList_->setAlternatingRowColors(true);
    browserLayout->addWidget(keyframeList_, 1);
    auto* keyActions = new LuxUI::HBoxLayout;
    auto* addKey = new LuxUI::PushButton(LUX_UI_TEXT("현재 변환 키 추가"), browser);
    addKey->setObjectName(LUX_UI_TEXT("animationAddKey"));
    addKey->setToolTip(LUX_UI_TEXT("현재 시간에 오브젝트의 위치·회전·크기를 기록합니다."));
    auto* removeKey = new LuxUI::PushButton(browser);
    removeKey->setObjectName(LUX_UI_TEXT("animationRemoveKey"));
    removeKey->setIcon(MakeEditorIcon(EditorIcon::Delete));
    removeKey->setToolTip(LUX_UI_TEXT("선택 키 삭제 (Delete)"));
    keyActions->addWidget(addKey, 1);
    keyActions->addWidget(removeKey);
    browserLayout->addLayout(keyActions);

    auto* preview = new LuxUI::TabWidget(mainSplit);
    preview->setObjectName(LUX_UI_TEXT("animationPreviewTabs"));
    preview->setDocumentMode(true);
    auto renderer = std::make_shared<EngineSceneRenderer>();
    const auto select = [this](std::optional<LuxEngine::uint32> id) { SelectEntity(id); };
    const auto transform = [this](LuxEngine::uint32 id,
                                  const LuxEngine::TransformComponent& value) {
        ApplyViewportTransform(id, value);
    };
    const auto begin = [this] { BeginViewportEdit(); };
    const auto end = [this] { EndViewportEdit(); };
    animationPreview3D_ =
        new SceneViewport3D(preview, document_, select, transform, begin, end, renderer);
    animationPreview3D_->setObjectName(LUX_UI_TEXT("animationPreview3D"));
    animationPreview2D_ = new SceneViewport2D(
        preview, document_, select, transform,
        [](LuxEngine::uint32, LuxEngine::uint32, LuxEngine::uint32, uint8_t) {}, begin, end,
        renderer);
    animationPreview2D_->setObjectName(LUX_UI_TEXT("animationPreview2D"));
    animationPreview2D_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    animationPreview3D_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    preview->addTab(animationPreview3D_, LUX_UI_TEXT("3D 미리보기"));
    preview->addTab(animationPreview2D_, LUX_UI_TEXT("2D 미리보기"));

    auto* detailsScroll = new LuxUI::ScrollArea(mainSplit);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setHorizontalScrollBarPolicy(LuxUI::Flags::ScrollBarAlwaysOff);
    detailsScroll->setMinimumWidth(270);
    auto* details = new LuxUI::Widget(detailsScroll);
    auto* detailsLayout = new LuxUI::VBoxLayout(details);
    detailsLayout->setContentsMargins(10, 10, 10, 10);
    auto* clipGroup = new LuxUI::GroupBox(LUX_UI_TEXT("클립 설정"), details);
    clipGroup->setStyleSheet(
        LUX_UI_TEXT("QGroupBox { margin-top:20px; padding-top:8px; } QGroupBox::title { "
                    "subcontrol-origin:margin; left:8px; top:2px; }"));
    auto* clipForm = new LuxUI::FormLayout(clipGroup);
    animationDuration_ = new LuxUI::DoubleSpinBox(clipGroup);
    animationDuration_->setObjectName(LUX_UI_TEXT("animationDuration"));
    animationDuration_->setRange(.01, 3600);
    animationDuration_->setDecimals(3);
    animationDuration_->setValue(1);
    animationDuration_->setSuffix(LUX_UI_TEXT(" s"));
    animationDuration_->setKeyboardTracking(false);
    clipForm->addRow(LUX_UI_TEXT("길이"), animationDuration_);
    animationLoop_ = new LuxUI::CheckBox(LUX_UI_TEXT("반복 재생"), clipGroup);
    clipForm->addRow(animationLoop_);
    auto* interpolation = new LuxUI::Label(LUX_UI_TEXT("선형"), clipGroup);
    interpolation->setToolTip(LUX_UI_TEXT("현재 변환 클립은 키 사이를 선형 보간합니다."));
    clipForm->addRow(LUX_UI_TEXT("보간"), interpolation);
    detailsLayout->addWidget(clipGroup);
    auto* keyGroup = new LuxUI::GroupBox(LUX_UI_TEXT("선택 키"), details);
    keyGroup->setObjectName(LUX_UI_TEXT("animationKeyDetails"));
    keyGroup->setStyleSheet(clipGroup->styleSheet());
    auto* keyForm = new LuxUI::FormLayout(keyGroup);
    keyForm->setFieldGrowthPolicy(LuxUI::FormLayout::AllNonFixedFieldsGrow);
    keyForm->setRowWrapPolicy(LuxUI::FormLayout::WrapLongRows);
    animationKeyTime_ = new LuxUI::DoubleSpinBox(keyGroup);
    animationKeyTime_->setObjectName(LUX_UI_TEXT("animationKeyTime"));
    animationKeyTime_->setRange(0, 3600);
    animationKeyTime_->setDecimals(4);
    animationKeyTime_->setSuffix(LUX_UI_TEXT(" s"));
    animationKeyTime_->setKeyboardTracking(false);
    keyForm->addRow(LUX_UI_TEXT("시간"), animationKeyTime_);
    const LuxUI::StringList rowNames{LUX_UI_TEXT("위치"), LUX_UI_TEXT("회전 °"),
                                     LUX_UI_TEXT("크기")};
    for (int row = 0; row < 3; ++row) {
        auto* vector = new LuxUI::Widget(keyGroup);
        auto* vectorLayout = new LuxUI::HBoxLayout(vector);
        vectorLayout->setContentsMargins(0, 0, 0, 0);
        vectorLayout->setSpacing(3);
        for (int axis = 0; axis < 3; ++axis) {
            auto* field = new LuxUI::DoubleSpinBox(vector);
            field->setObjectName(LUX_UI_TEXT("animationKeyValue%1%2").arg(row).arg(axis));
            field->setRange(-1e6, 1e6);
            field->setDecimals(3);
            field->setButtonSymbols(LuxUI::AbstractSpinBox::NoButtons);
            field->setKeyboardTracking(false);
            field->setMinimumWidth(68);
            field->setPrefix(LuxUI::String::fromLatin1("XYZ" + axis, 1) + LUX_UI_TEXT(" "));
            animationKeyValues_[row][axis] = field;
            vectorLayout->addWidget(field);
            connect(field, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                    [this, field] { OnAnimationKeyChanged(field); });
        }
        keyForm->addRow(rowNames[row], vector);
    }
    detailsLayout->addWidget(keyGroup);
    detailsLayout->addStretch();
    detailsScroll->setWidget(details);
    mainSplit->setSizes({205, 750, 270});
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    workspace->addWidget(mainSplit, 3);

    auto* sequence = new LuxUI::Widget(animationPanel_);
    sequence->setObjectName(LUX_UI_TEXT("animationSequence"));
    auto* sequenceLayout = new LuxUI::VBoxLayout(sequence);
    sequenceLayout->setContentsMargins(0, 0, 0, 0);
    sequenceLayout->setSpacing(0);
    auto* sequenceHeader = new LuxUI::Widget(sequence);
    auto* sequenceHeading = new LuxUI::HBoxLayout(sequenceHeader);
    sequenceHeading->setContentsMargins(12, 5, 12, 5);
    auto* mode = new LuxUI::ComboBox(sequenceHeader);
    mode->setObjectName(LUX_UI_TEXT("animationTimelineMode"));
    mode->addItems({LUX_UI_TEXT("도프 시트"), LUX_UI_TEXT("위치 곡선")});
    sequenceHeading->addWidget(mode);
    sequenceHeading->addStretch();
    sequenceHeading->addWidget(new LuxUI::Label(LUX_UI_TEXT("확대"), sequenceHeader));
    auto* zoom = new LuxUI::SpinBox(sequenceHeader);
    zoom->setObjectName(LUX_UI_TEXT("animationZoom"));
    zoom->setRange(100, 1600);
    zoom->setSingleStep(25);
    zoom->setValue(100);
    zoom->setSuffix(LUX_UI_TEXT(" %"));
    sequenceHeading->addWidget(zoom);
    sequenceLayout->addWidget(sequenceHeader);
    auto* timeline = new AnimationTimeline(
        sequence,
        {[this] { return document_.GetSelected(); }, [this] { return keyframeList_->currentRow(); },
         [this] { return animationFps_ ? animationFps_->value() : 30; },
         [this](int row) { keyframeList_->setCurrentRow(row); },
         [this](int row, float time) { MoveAnimationKey(row, time); }, [this] { RemoveKeyframe(); },
         [this] { AddKeyframe(); }, [this](double time) { SeekAnimationTime(time); },
         [this] {
             if (auto* play =
                     animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("animationPlay")))
                 play->click();
         }});
    animationTimeline_ = timeline;
    timeline->setObjectName(LUX_UI_TEXT("animationTimeline"));
    timeline->setRange(0, 1000);
    sequenceLayout->addWidget(timeline, 1);
    auto* scroll = new LuxUI::ScrollBar(LuxUI::Flags::Horizontal, sequence);
    scroll->setObjectName(LUX_UI_TEXT("animationTimeScroll"));
    scroll->setRange(0, 1000);
    scroll->setPageStep(1000);
    sequenceLayout->addWidget(scroll);
    connect(zoom, qOverload<int>(&LuxUI::SpinBox::valueChanged), timeline,
            [timeline, scroll](int value) {
                timeline->SetZoom(value / 100.0);
                scroll->setPageStep(100000 / value);
            });
    connect(scroll, &LuxUI::ScrollBar::valueChanged, timeline,
            [timeline](int value) { timeline->SetScroll(value / 1000.0); });
    connect(mode, &LuxUI::ComboBox::currentIndexChanged, timeline,
            [timeline](int index) { timeline->SetCurveMode(index == 1); });
    workspace->addWidget(sequence, 2);

    auto* transport = new LuxUI::HBoxLayout;
    animationTime_ = new LuxUI::DoubleSpinBox(animationPanel_);
    animationTime_->setObjectName(LUX_UI_TEXT("animationTime"));
    animationTime_->setDecimals(3);
    animationTime_->setRange(0, 3600);
    animationTime_->setSuffix(LUX_UI_TEXT(" s"));
    animationTime_->setKeyboardTracking(false);
    animationFps_ = new LuxUI::SpinBox(animationPanel_);
    animationFps_->setRange(1, 240);
    animationFps_->setValue(30);
    animationFps_->setSuffix(LUX_UI_TEXT(" FPS"));
    connect(animationFps_, qOverload<int>(&LuxUI::SpinBox::valueChanged), this,
            [this] { RefreshAnimationWorkspace(); });
    const auto button = [&](const LuxUI::String& text, const char* name, auto callback) {
        auto* control = new LuxUI::PushButton(animationPanel_);
        control->setToolTip(text);
        control->setFixedSize(30, 30);
        control->setIconSize(LuxUI::Size(18, 18));
        const LuxUI::String id = LuxUI::String::fromLatin1(name);
        control->setIcon(MakeEditorIcon(id == LUX_UI_TEXT("animationPlay")   ? EditorIcon::Play
                                        : id == LUX_UI_TEXT("animationStop") ? EditorIcon::Stop
                                                                             : EditorIcon::Step));
        if (id == LUX_UI_TEXT("animationPreviousFrame"))
            control->setIcon(
                control->icon().pixmap(20, 20).transformed(LuxUI::Transform().scale(-1, 1)));
        control->setObjectName(LuxUI::String::fromLatin1(name));
        connect(control, &LuxUI::PushButton::clicked, this, callback);
        transport->addWidget(control);
        return control;
    };
    button(LUX_UI_TEXT("이전 프레임"), "animationPreviousFrame", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        const auto entity = document_.GetSelected();
        if (entity && entity.HasComponent<LuxEngine::AnimationComponent>())
            SeekAnimationTime(entity.GetComponent<LuxEngine::AnimationComponent>().currentTime -
                              1.0 / animationFps_->value());
        EndTimelineEdit();
    });
    button(LUX_UI_TEXT("재생 / 일시정지"), "animationPlay", [this] {
        if (animationTimer_.isActive()) {
            StopAnimationPlayback();
            return;
        }
        const auto entity = document_.GetSelected();
        if (!entity || !entity.HasComponent<LuxEngine::AnimationComponent>())
            return;
        if (entity.GetComponent<LuxEngine::AnimationComponent>().clip.duration <= 0)
            return;
        StopPreview();
        timelineBefore_ = SnapshotScene();
        const auto tab = document_.GetActiveTabId();
        const auto id = entity.GetId();
        animationTimer_.disconnect(this);
        auto clock = std::make_shared<LuxUI::ElapsedTimer>();
        clock->start();
        connect(&animationTimer_, &LuxUI::Timer::timeout, this, [this, tab, id, clock] {
            const auto selected = document_.GetSelected();
            if (document_.GetActiveTabId() != tab || !selected || selected.GetId() != id ||
                !selected.HasComponent<LuxEngine::AnimationComponent>()) {
                animationTimer_.stop();
                return;
            }
            auto& animation = selected.GetComponent<LuxEngine::AnimationComponent>();
            const auto next = animation.currentTime + clock->restart() / 1000.0f;
            const bool ended = next >= animation.clip.duration;
            const auto time = ended && animation.clip.loop
                                  ? std::fmod(next, animation.clip.duration)
                                  : std::min(next, animation.clip.duration);
            SeekAnimationTime(time);
            if (ended && !animation.clip.loop)
                StopAnimationPlayback();
        });
        animationTimer_.start(std::max(1, 1000 / animationFps_->value()));
        if (auto* play =
                animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("animationPlay")))
            play->setIcon(MakeEditorIcon(EditorIcon::Pause));
    });
    button(LUX_UI_TEXT("정지"), "animationStop", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        ScrubAnimation(0);
        EndTimelineEdit();
    });
    button(LUX_UI_TEXT("다음 프레임"), "animationNextFrame", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        const auto entity = document_.GetSelected();
        if (entity && entity.HasComponent<LuxEngine::AnimationComponent>())
            SeekAnimationTime(entity.GetComponent<LuxEngine::AnimationComponent>().currentTime +
                              1.0 / animationFps_->value());
        EndTimelineEdit();
    });
    transport->addSpacing(12);
    transport->addWidget(new LuxUI::Label(LUX_UI_TEXT("시간"), animationPanel_));
    transport->addWidget(animationTime_);
    transport->addWidget(animationFps_);
    transport->addStretch();

    auto* transportBar = new LuxUI::Widget(animationPanel_);
    transportBar->setObjectName(LUX_UI_TEXT("animationTransport"));
    transportBar->setLayout(transport);
    transport->setContentsMargins(12, 5, 12, 5);
    workspace->addWidget(transportBar);
    BuildModelAnimationWorkspace(workspace);
    connect(animationTime_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this](double time) {
                if (refreshingInspector_)
                    return;
                StopAnimationPlayback();
                timelineBefore_ = SnapshotScene();
                SeekAnimationTime(time);
                EndTimelineEdit();
            });
    connect(animationName_, &LuxUI::LineEdit::editingFinished, this,
            [this] { OnAnimationChanged(animationName_); });
    connect(animationEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnAnimationChanged(animationEnabled_); });
    connect(animationLoop_, &LuxUI::CheckBox::toggled, this,
            [this] { OnAnimationChanged(animationLoop_); });
    connect(animationDuration_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnAnimationChanged(animationDuration_); });
    connect(animationKeyTime_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnAnimationKeyChanged(animationKeyTime_); });
    connect(keyframeList_, &LuxUI::ListWidget::currentRowChanged, this, [this](int row) {
        if (refreshingInspector_)
            return;
        keyframeList_->setProperty("selectedKeyRow", row);
        RefreshAnimationWorkspace();
    });
    connect(addKey, &LuxUI::PushButton::clicked, this, [this] { AddKeyframe(); });
    connect(removeKey, &LuxUI::PushButton::clicked, this, [this] { RemoveKeyframe(); });
    connect(animationTimeline_, &LuxUI::Slider::sliderPressed, this, [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
    });
    connect(animationTimeline_, &LuxUI::Slider::actionTriggered, this, [this] {
        if (animationTimeline_->isSliderDown() || previewActive_)
            return;
        timelineBefore_ = SnapshotScene();
        LuxUI::Timer::singleShot(0, this, [this] { EndTimelineEdit(); });
    });
    connect(animationTimeline_, &LuxUI::Slider::valueChanged, this,
            [this](int value) { ScrubAnimation(value); });
    connect(animationTimeline_, &LuxUI::Slider::sliderReleased, this,
            [this] { EndTimelineEdit(); });
    animationDock_->setWidget(animationPanel_);
    addDockWidget(LuxUI::Flags::BottomDockWidgetArea, animationDock_);
    animationDock_->hide();
    connect(animationDock_, &LuxUI::DockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible)
            StopAnimationPlayback();
    });
}

void EditorWindow::SetAnimationTool(EditorTool tool) {
    const int index = static_cast<int>(tool);
    if (previewActive_ || index < 0 || index > 3)
        return;
    StopAnimationPlayback();
    EndTimelineEdit();
    CancelViewportEdit();
    if (animationPreview2D_) {
        animationPreview2D_->CancelInteraction();
        animationPreview2D_->SetTool(tool);
    }
    if (animationPreview3D_) {
        animationPreview3D_->CancelInteraction();
        animationPreview3D_->SetTool(tool);
    }
    for (int item = 0; item < 4; ++item)
        if (auto* button = animationPanel_->findChild<LuxUI::ToolButton*>(
                LUX_UI_TEXT("animationTool%1").arg(item)))
            button->setChecked(item == index);
}

void EditorWindow::RefreshAnimationWorkspace() {
    if (!animationPanel_)
        return;
    const bool refreshing = refreshingInspector_;
    refreshingInspector_ = true;
    const auto entity = document_.GetSelected();
    const auto* animation =
        entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    const int count = animation ? static_cast<int>(animation->clip.keyframes.size()) : 0;
    int row = keyframeList_->property("selectedKeyRow").isValid()
                  ? keyframeList_->property("selectedKeyRow").toInt()
                  : -1;
    if (row < 0 || row >= count)
        row = count > 0 ? 0 : -1;
    {
        const LuxUI::SignalBlocker blocked(keyframeList_);
        keyframeList_->setCurrentRow(row);
        keyframeList_->setProperty("selectedKeyRow", row);
        for (int index = 0; index < count; ++index)
            keyframeList_->item(index)->setText(
                LUX_UI_TEXT("◆  %1 s    ·    %2 프레임")
                    .arg(animation->clip.keyframes[index].time, 0, 'f', 3)
                    .arg(std::llround(animation->clip.keyframes[index].time *
                                      animationFps_->value())));
    }
    if (entity && entity.HasComponent<LuxEngine::TagComponent>()) {
        const auto name =
            LuxUI::String::fromUtf8(entity.GetComponent<LuxEngine::TagComponent>().tag);
        animationTarget_->setText(name + LUX_UI_TEXT(" · 변환 애니메이션"));
        animationTarget_->setToolTip(name);
    }
    if (auto* create =
            animationPanel_->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("animationCreateClip")))
        create->setEnabled(!animation && entity &&
                           entity.HasComponent<LuxEngine::TransformComponent>() && !previewActive_);
    if (auto* autoPlay =
            animationPanel_->findChild<LuxUI::CheckBox*>(LUX_UI_TEXT("animationAutoPlay"))) {
        const LuxUI::SignalBlocker blocked(autoPlay);
        autoPlay->setChecked(animation && animation->playing);
        autoPlay->setEnabled(animation && !previewActive_);
    }
    if (auto* remove =
            animationPanel_->findChild<LuxUI::ToolButton*>(LUX_UI_TEXT("animationRemoveClip")))
        remove->setEnabled(animation && !previewActive_);
    if (auto* label = animationPanel_->findChild<LuxUI::Label*>(LUX_UI_TEXT("animationKeyCount")))
        label->setText(LUX_UI_TEXT("Transform Clip · 키 %1개").arg(count));
    if (auto* details =
            animationPanel_->findChild<LuxUI::GroupBox*>(LUX_UI_TEXT("animationKeyDetails")))
        details->setEnabled(row >= 0 && !previewActive_);
    if (row >= 0) {
        const auto& key = animation->clip.keyframes[row];
        const LuxUI::SignalBlocker blockTime(animationKeyTime_);
        animationKeyTime_->setRange(0,
                                    std::max(1.0, static_cast<double>(animation->clip.duration)));
        animationKeyTime_->setValue(key.time);
        const double radiansToDegrees = 180.0 / std::acos(-1.0);
        const double values[3][3] = {{key.position.x, key.position.y, key.position.z},
                                     {key.rotation.x * radiansToDegrees,
                                      key.rotation.y * radiansToDegrees,
                                      key.rotation.z * radiansToDegrees},
                                     {key.scale.x, key.scale.y, key.scale.z}};
        for (int group = 0; group < 3; ++group)
            for (int axis = 0; axis < 3; ++axis) {
                const LuxUI::SignalBlocker blocked(animationKeyValues_[group][axis]);
                animationKeyValues_[group][axis]->setValue(values[group][axis]);
            }
    }
    animationTimeline_->update();
    if (entity) {
        auto* preview =
            animationPanel_->findChild<LuxUI::TabWidget*>(LUX_UI_TEXT("animationPreviewTabs"));
        const auto target =
            LUX_UI_TEXT("%1:%2").arg(document_.GetActiveTabId()).arg(entity.GetId());
        if (preview && preview->property("previewTarget").toString() != target) {
            preview->setProperty("previewTarget", target);
            preview->setCurrentIndex(entity.HasComponent<LuxEngine::MeshRendererComponent>() ? 0
                                                                                             : 1);
            if (animationPreview2D_)
                animationPreview2D_->FocusSelection();
            if (animationPreview3D_)
                animationPreview3D_->FocusSelection();
        }
    }
    if (animationPreview2D_)
        animationPreview2D_->RefreshScene();
    if (animationPreview3D_)
        animationPreview3D_->RefreshScene();
    RefreshModelAnimationWorkspace();
    refreshingInspector_ = refreshing;
}

void EditorWindow::MoveAnimationKey(int index, float time) {
    auto entity = document_.GetSelected();
    auto* animation = entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    if (previewActive_ || !animation || !std::isfinite(time) || index < 0 ||
        static_cast<size_t>(index) >= animation->clip.keyframes.size())
        return;
    time = std::clamp(time, 0.0f, animation->clip.duration);
    for (size_t other = 0; other < animation->clip.keyframes.size(); ++other)
        if (static_cast<int>(other) != index &&
            std::abs(animation->clip.keyframes[other].time - time) < 1e-6f) {
            RefreshAnimationWorkspace();
            return;
        }
    animation->clip.keyframes[index].time = time;
    std::stable_sort(animation->clip.keyframes.begin(), animation->clip.keyframes.end(),
                     [](const auto& left, const auto& right) { return left.time < right.time; });
    const auto selected =
        std::find_if(animation->clip.keyframes.begin(), animation->clip.keyframes.end(),
                     [time](const auto& key) { return key.time == time; });
    keyframeList_->setProperty("selectedKeyRow",
                               static_cast<int>(selected - animation->clip.keyframes.begin()));
    SeekAnimationTime(time);
    document_.MarkDirty();
    RefreshInspector();
}

void EditorWindow::OnAnimationKeyChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || previewActive_)
        return;
    auto entity = document_.GetSelected();
    const auto* animation =
        entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
    const int row = keyframeList_->currentRow();
    if (!animation || row < 0 || static_cast<size_t>(row) >= animation->clip.keyframes.size())
        return;
    StopAnimationPlayback();
    RecordEdit(LUX_UI_TEXT("애니메이션 키 편집"), [this, field, row] {
        if (field == animationKeyTime_) {
            MoveAnimationKey(row, static_cast<float>(animationKeyTime_->value()));
            return;
        }
        auto& animation = document_.GetSelected().GetComponent<LuxEngine::AnimationComponent>();
        auto& key = animation.clip.keyframes[static_cast<size_t>(row)];
        for (int group = 0; group < 3; ++group)
            for (int axis = 0; axis < 3; ++axis) {
                if (field != animationKeyValues_[group][axis])
                    continue;
                const float value = static_cast<float>(animationKeyValues_[group][axis]->value() *
                                                       (group == 1 ? std::acos(-1.0) / 180 : 1));
                auto& vector = group == 0 ? key.position : group == 1 ? key.rotation : key.scale;
                if (axis == 0)
                    vector.x = value;
                else if (axis == 1)
                    vector.y = value;
                else
                    vector.z = value;
            }
        if (document_.GetSelected().HasComponent<LuxEngine::TransformComponent>())
            document_.GetSelected().GetComponent<LuxEngine::TransformComponent>() =
                animation.clip.Sample(animation.currentTime);
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}

void EditorWindow::OnAnimationChanged(const LuxUI::Widget* field) {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::TransformComponent>())
        return;
    StopAnimationPlayback();
    RecordEdit(LUX_UI_TEXT("애니메이션 변경"), [this, field] {
        LuxEngine::Entity entity = document_.GetSelected();
        if (field == animationEnabled_) {
            if (animationEnabled_->isChecked() &&
                !entity.HasComponent<LuxEngine::AnimationComponent>()) {
                auto& animation = entity.AddComponent<LuxEngine::AnimationComponent>();
                animation.clip.name = "Transform";
                const auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
                animation.clip.AddKeyframe(
                    {0, transform.position, transform.rotation, transform.scale});
            } else if (!animationEnabled_->isChecked() &&
                       entity.HasComponent<LuxEngine::AnimationComponent>())
                entity.RemoveComponent<LuxEngine::AnimationComponent>();
        } else if (entity.HasComponent<LuxEngine::AnimationComponent>()) {
            auto& clip = entity.GetComponent<LuxEngine::AnimationComponent>().clip;
            if (field == animationName_)
                clip.name = animationName_->text().toUtf8().toStdString();
            else if (field == animationLoop_)
                clip.loop = animationLoop_->isChecked();
            else if (field == animationDuration_) {
                clip.duration = static_cast<float>(animationDuration_->value());
                if (!clip.keyframes.empty())
                    clip.duration = std::max(clip.duration, clip.keyframes.back().time);
            }
        }
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}

void EditorWindow::AddKeyframe() {
    if (previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::TransformComponent>() ||
        !document_.GetSelected().HasComponent<LuxEngine::AnimationComponent>())
        return;
    StopAnimationPlayback();
    RecordEdit(LUX_UI_TEXT("애니메이션 키 추가"), [this] {
        LuxEngine::Entity entity = document_.GetSelected();
        auto& animation = entity.GetComponent<LuxEngine::AnimationComponent>();
        const auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
        const float time = animation.currentTime;
        animation.clip.AddKeyframe({time, transform.position, transform.rotation, transform.scale});
        const auto inserted =
            std::find_if(animation.clip.keyframes.begin(), animation.clip.keyframes.end(),
                         [time](const auto& key) { return key.time == time; });
        keyframeList_->setProperty("selectedKeyRow",
                                   static_cast<int>(inserted - animation.clip.keyframes.begin()));
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}

void EditorWindow::RemoveKeyframe() {
    if (previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::AnimationComponent>())
        return;
    StopAnimationPlayback();
    const int row = keyframeList_->currentRow();
    if (row < 0)
        return;
    RecordEdit(LUX_UI_TEXT("애니메이션 키 삭제"), [this, row] {
        auto& keys =
            document_.GetSelected().GetComponent<LuxEngine::AnimationComponent>().clip.keyframes;
        if (static_cast<size_t>(row) < keys.size())
            keys.erase(keys.begin() + row);
        document_.MarkDirty();
    });
    RefreshInspector();
    UpdateChrome();
}

void EditorWindow::ScrubAnimation(int value) {
    const auto selected = document_.GetSelected();
    if (selected && selected.HasComponent<LuxEngine::AnimationComponent>())
        SeekAnimationTime(selected.GetComponent<LuxEngine::AnimationComponent>().clip.duration *
                          std::clamp(value, 0, 1000) / 1000.0);
}

void EditorWindow::SeekAnimationTime(double time) {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::TransformComponent>() ||
        !document_.GetSelected().HasComponent<LuxEngine::AnimationComponent>())
        return;
    auto& animation = document_.GetSelected().GetComponent<LuxEngine::AnimationComponent>();
    if (!std::isfinite(time))
        return;
    animation.currentTime =
        static_cast<float>(std::clamp(time, 0.0, static_cast<double>(animation.clip.duration)));
    const int value = animation.clip.duration > 0
                          ? static_cast<int>(1000 * animation.currentTime / animation.clip.duration)
                          : 0;
    const LuxUI::SignalBlocker timelineBlock(animationTimeline_), timeBlock(animationTime_);
    animationTimeline_->setValue(value);
    animationTime_->setValue(animation.currentTime);
    if (!animation.clip.keyframes.empty())
        document_.GetSelected().GetComponent<LuxEngine::TransformComponent>() =
            animation.clip.Sample(animation.currentTime);
    document_.MarkDirty();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    if (animationPreview2D_)
        animationPreview2D_->RefreshScene();
    if (animationPreview3D_)
        animationPreview3D_->RefreshScene();
    animationTimeline_->update();
    UpdateChrome();
}

void EditorWindow::EndTimelineEdit() {
    if (timelineBefore_.isEmpty())
        return;
    const auto after = SnapshotScene();
    if (!timelineBefore_.isEmpty() && !after.isEmpty() && timelineBefore_ != after) {
        const auto before = timelineBefore_;
        const auto id = SelectedId(document_);
        const auto tab = document_.GetActiveTabId();
        PushSceneCommand(new SceneEditCommand(
            LUX_UI_TEXT("애니메이션 편집"),
            [this, tab, before, id] { RestoreSceneAt(tab, before, id); },
            [this, tab, after, id] { RestoreSceneAt(tab, after, id); }, {tab},
            SceneHistoryBytes(before) + SceneHistoryBytes(after)));
        UpdateSceneDirty(tab, after);
    }
    timelineBefore_.clear();
}

} // namespace LuxEditor
