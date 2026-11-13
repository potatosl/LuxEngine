#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Scene/SceneHistory.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace LuxEditor {
namespace {
std::optional<LuxEngine::uint32> SelectedId(EditorDocument& doc) {
    return doc.GetSelectedId(doc.GetActiveTabId());
}
class KeyList final : public QListWidget {
public:
    KeyList(QWidget* parent, std::function<void()> remove)
        : QListWidget(parent), remove_(std::move(remove)) {}

private:
    std::function<void()> remove_;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::ShortcutOverride &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Delete) {
            event->accept();
            return true;
        }
        return QListWidget::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Delete) {
            remove_();
            event->accept();
        } else
            QListWidget::keyPressEvent(event);
    }
};
class KeyTimeline final : public QSlider {
public:
    KeyTimeline(QWidget* parent, std::function<LuxEngine::Entity()> selected)
        : QSlider(Qt::Horizontal, parent), selected_(std::move(selected)) {
        setMinimumHeight(64);
    }

private:
    std::function<LuxEngine::Entity()> selected_;
    void paintEvent(QPaintEvent* event) override {
        QSlider::paintEvent(event);
        QPainter painter(this);
        painter.setPen(palette().text().color());
        const auto entity = selected_();
        const auto* animation =
            entity ? entity.TryGetComponent<LuxEngine::AnimationComponent>() : nullptr;
        const float duration = animation ? animation->clip.duration : 1;
        for (int tick = 0; tick <= 10; ++tick) {
            const int x = 10 + (width() - 20) * tick / 10;
            painter.drawText(QRect(x - 20, 0, 40, 18), Qt::AlignCenter,
                             QString::number(duration * tick / 10, 'f', 1));
        }
        if (!animation || duration <= 0)
            return;
        painter.setBrush(QColor(235, 176, 75));
        for (const auto& key : animation->clip.keyframes) {
            const double x = 10 + (width() - 20) * key.time / duration;
            painter.drawPolygon(
                QPolygonF{QPointF(x, 46), QPointF(x + 5, 51), QPointF(x, 56), QPointF(x - 5, 51)});
        }
    }
};
} // namespace
void EditorWindow::StopAnimationPlayback() {
    if (!animationTimer_.isActive())
        return;
    animationTimer_.stop();
    EndTimelineEdit();
}
void EditorWindow::BuildAnimationPanel() {
    animationDock_ = new QDockWidget(QStringLiteral("애니메이션"), this);
    animationDock_->setObjectName(QStringLiteral("AnimationWorkspace"));
    // 개발 중: 스프라이트 시트·본 애니메이션은 엔진 구현 후 타임라인에 연결합니다.
    auto* animationGroup = new QGroupBox(QStringLiteral("클립"), animationDock_);
    auto* animationLayout = new QVBoxLayout(animationGroup);
    animationEnabled_ = new QCheckBox(QStringLiteral("애니메이션 사용"), animationGroup);
    animationEnabled_->setObjectName(QStringLiteral("editorAnimation"));
    animationLoop_ = new QCheckBox(QStringLiteral("반복 재생"), animationGroup);
    animationDuration_ = new QDoubleSpinBox(animationGroup);
    animationDuration_->setRange(.01, 3600);
    animationDuration_->setValue(1);
    animationDuration_->setKeyboardTracking(false);
    auto* durationRow = new QFormLayout;
    durationRow->addRow(QStringLiteral("길이 (초)"), animationDuration_);
    animationTimeline_ =
        new KeyTimeline(animationGroup, [this] { return document_.GetSelected(); });
    animationTimeline_->setObjectName(QStringLiteral("animationTimeline"));
    animationTimeline_->setRange(0, 1000);
    animationTimeline_->setToolTip(QStringLiteral("시간을 이동해 변환 애니메이션을 확인합니다."));
    keyframeList_ = new KeyList(animationGroup, [this] { RemoveKeyframe(); });
    keyframeList_->setMaximumHeight(60);
    keyframeList_->setObjectName(QStringLiteral("animationKeys"));
    auto* keyButtons = new QHBoxLayout;
    auto* addKey = new QPushButton(QStringLiteral("현재 변환 키 추가"), animationGroup);
    auto* removeKey = new QPushButton(QStringLiteral("선택 키 삭제"), animationGroup);
    keyButtons->addWidget(addKey);
    keyButtons->addWidget(removeKey);
    animationPanel_ = animationGroup;
    animationGroup->setObjectName(QStringLiteral("animationWorkspace"));
    animationTarget_ = new QLabel(animationGroup);
    animationName_ = new QLineEdit(animationGroup);
    animationName_->setObjectName(QStringLiteral("animationClipName"));
    animationLayout->addWidget(animationTarget_);
    auto* clipRow = new QFormLayout;
    clipRow->addRow(QStringLiteral("클립 이름"), animationName_);
    animationLayout->addLayout(clipRow);
    connect(animationName_, &QLineEdit::editingFinished, this,
            [this] { OnAnimationChanged(animationName_); });
    auto* transport = new QHBoxLayout;
    animationTime_ = new QDoubleSpinBox(animationGroup);
    animationTime_->setObjectName(QStringLiteral("animationTime"));
    animationTime_->setDecimals(3);
    animationTime_->setRange(0, 3600);
    animationTime_->setSuffix(QStringLiteral(" s"));
    animationTime_->setKeyboardTracking(false);
    animationFps_ = new QSpinBox(animationGroup);
    animationFps_->setRange(1, 240);
    animationFps_->setValue(30);
    animationFps_->setSuffix(QStringLiteral(" FPS"));
    const auto button = [&](const QString& text, const char* name, auto callback) {
        auto* control = new QPushButton(text, animationGroup);
        control->setObjectName(QString::fromLatin1(name));
        connect(control, &QPushButton::clicked, this, callback);
        transport->addWidget(control);
        return control;
    };
    button(QStringLiteral("◀|"), "animationPreviousFrame", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        const auto entity = document_.GetSelected();
        if (entity && entity.HasComponent<LuxEngine::AnimationComponent>())
            SeekAnimationTime(entity.GetComponent<LuxEngine::AnimationComponent>().currentTime -
                              1.0 / animationFps_->value());
        EndTimelineEdit();
    });
    button(QStringLiteral("▶ / Ⅱ"), "animationPlay", [this] {
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
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        connect(&animationTimer_, &QTimer::timeout, this, [this, tab, id, clock] {
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
    });
    button(QStringLiteral("■"), "animationStop", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        ScrubAnimation(0);
        EndTimelineEdit();
    });
    button(QStringLiteral("|▶"), "animationNextFrame", [this] {
        StopAnimationPlayback();
        timelineBefore_ = SnapshotScene();
        const auto entity = document_.GetSelected();
        if (entity && entity.HasComponent<LuxEngine::AnimationComponent>())
            SeekAnimationTime(entity.GetComponent<LuxEngine::AnimationComponent>().currentTime +
                              1.0 / animationFps_->value());
        EndTimelineEdit();
    });
    transport->addWidget(animationTime_);
    transport->addWidget(animationFps_);
    animationLayout->addLayout(transport);
    connect(animationTime_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double time) {
                if (refreshingInspector_)
                    return;
                StopAnimationPlayback();
                timelineBefore_ = SnapshotScene();
                SeekAnimationTime(time);
                EndTimelineEdit();
            });
    animationLayout->addWidget(
        new QLabel(QStringLiteral("변환 트랙 · 위치 / 회전 / 크기"), animationGroup));
    animationLayout->addWidget(animationEnabled_);
    animationLayout->addWidget(animationLoop_);
    animationLayout->addLayout(durationRow);
    animationLayout->addWidget(animationTimeline_);
    animationLayout->addWidget(keyframeList_);
    animationLayout->addLayout(keyButtons);
    animationDock_->setWidget(animationGroup);
    connect(animationEnabled_, &QCheckBox::toggled, this,
            [this] { OnAnimationChanged(animationEnabled_); });
    connect(animationLoop_, &QCheckBox::toggled, this,
            [this] { OnAnimationChanged(animationLoop_); });
    connect(animationDuration_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnAnimationChanged(animationDuration_); });
    connect(addKey, &QPushButton::clicked, this, [this] { AddKeyframe(); });
    connect(removeKey, &QPushButton::clicked, this, [this] { RemoveKeyframe(); });
    connect(animationTimeline_, &QSlider::sliderPressed, this,
            [this] { timelineBefore_ = SnapshotScene(); });
    connect(animationTimeline_, &QSlider::actionTriggered, this, [this] {
        if (animationTimeline_->isSliderDown() || previewActive_)
            return;
        timelineBefore_ = SnapshotScene();
        QTimer::singleShot(0, this, [this] { EndTimelineEdit(); });
    });
    connect(animationTimeline_, &QSlider::valueChanged, this,
            [this](int value) { ScrubAnimation(value); });
    connect(animationTimeline_, &QSlider::sliderReleased, this, [this] { EndTimelineEdit(); });
    addDockWidget(Qt::BottomDockWidgetArea, animationDock_);
    tabifyDockWidget(docks_[7], animationDock_);
    animationDock_->raise();
    connect(animationDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible)
            StopAnimationPlayback();
    });
}
void EditorWindow::OnAnimationChanged(const QWidget* field) {
    if (refreshingInspector_ || previewActive_ || !document_.GetSelected() ||
        !document_.GetSelected().HasComponent<LuxEngine::TransformComponent>())
        return;
    StopAnimationPlayback();
    RecordEdit(QStringLiteral("애니메이션 변경"), [this, field] {
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
    RecordEdit(QStringLiteral("애니메이션 키 추가"), [this] {
        LuxEngine::Entity entity = document_.GetSelected();
        auto& animation = entity.GetComponent<LuxEngine::AnimationComponent>();
        const auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
        const float time = animation.clip.duration * animationTimeline_->value() / 1000.0f;
        animation.clip.AddKeyframe({time, transform.position, transform.rotation, transform.scale});
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
    RecordEdit(QStringLiteral("애니메이션 키 삭제"), [this, row] {
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
    const QSignalBlocker timelineBlock(animationTimeline_), timeBlock(animationTime_);
    animationTimeline_->setValue(value);
    animationTime_->setValue(animation.currentTime);
    if (!animation.clip.keyframes.empty())
        document_.GetSelected().GetComponent<LuxEngine::TransformComponent>() =
            animation.clip.Sample(animation.currentTime);
    document_.MarkDirty();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
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
            QStringLiteral("애니메이션 시간 이동"),
            [this, tab, before, id] { RestoreSceneAt(tab, before, id); },
            [this, tab, after, id] { RestoreSceneAt(tab, after, id); }, {tab},
            SceneHistoryBytes(before) + SceneHistoryBytes(after)));
        UpdateSceneDirty(tab, after);
    }
    timelineBefore_.clear();
}

} // namespace LuxEditor
