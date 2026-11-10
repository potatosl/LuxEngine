#include "DebugPanel.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Ui/IconFactory.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>

namespace LuxEditor {
DebugPanel::DebugPanel(QWidget* parent, std::function<void(uint32_t)> onSelect)
    : QWidget(parent), onSelect_(std::move(onSelect)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new QHBoxLayout;
    filter_ = new QComboBox(this);
    filter_->addItems({QStringLiteral("전체"), QStringLiteral("오류"), QStringLiteral("정보")});
    filter_->setToolTip(QStringLiteral("로그 유형 필터"));
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("로그 검색"));
    auto* clear = new QToolButton(this);
    clear->setIcon(MakeEditorIcon(EditorIcon::Delete));
    clear->setToolTip(QStringLiteral("디버그 로그 지우기"));
    auto* save = new QToolButton(this);
    save->setIcon(MakeEditorIcon(EditorIcon::Save));
    save->setToolTip(QStringLiteral("디버그 로그 저장"));
    controls->addWidget(filter_);
    controls->addWidget(search_, 1);
    controls->addWidget(clear);
    controls->addWidget(save);
    layout->addLayout(controls);
    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("debugLog"));
    output_->setReadOnly(true);
    output_->setMaximumHeight(80);
    layout->addWidget(output_, 2);
    metrics_ = new QLabel(QStringLiteral("실행 대기 중"), this);
    metrics_->setWordWrap(true);
    metrics_->setMinimumHeight(34);
    layout->addWidget(metrics_);
    state_ = new QTreeWidget(this);
    state_->setObjectName(QStringLiteral("debugState"));
    state_->setHeaderLabels({QStringLiteral("상태"), QStringLiteral("값")});
    state_->setRootIsDecorated(false);
    state_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    state_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    state_->header()->resizeSection(0, 90);
    state_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(state_, 1);
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { FilterLogs(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { FilterLogs(); });
    connect(clear, &QToolButton::clicked, this, [this] {
        logs_.clear();
        FilterLogs();
    });
    connect(save, &QToolButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("디버그 로그 저장"), QStringLiteral("lux-debug.log"),
            QStringLiteral("로그 파일 (*.log *.txt)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        for (const auto& entry : logs_) {
            const QByteArray line =
                ((entry.error ? QStringLiteral("[오류] ") : QStringLiteral("[정보] ")) +
                 entry.message + QLatin1Char('\n'))
                    .toUtf8();
            if (file.write(line) != line.size())
                return;
        }
        file.commit();
    });
    connect(state_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const uint32_t id = item->data(0, Qt::UserRole).toUInt();
        if (id)
            onSelect_(id);
    });
}
void DebugPanel::AddLog(const QString& message, bool error) {
    logs_.push_back({message, error});
    if (logs_.size() > 2000)
        logs_.erase(logs_.begin(), logs_.begin() + 500);
    FilterLogs();
}
void DebugPanel::FilterLogs() {
    output_->clear();
    for (const auto& entry : logs_) {
        if ((filter_->currentIndex() == 1 && !entry.error) ||
            (filter_->currentIndex() == 2 && entry.error) ||
            !entry.message.contains(search_->text(), Qt::CaseInsensitive))
            continue;
        output_->appendPlainText(
            (entry.error ? QStringLiteral("[오류] ") : QStringLiteral("[정보] ")) + entry.message);
    }
}
void DebugPanel::UpdateScene(LuxEngine::Scene& scene, uint32_t selectedId, uint64_t frame,
                             double frameMs) {
    metrics_->setText(QStringLiteral("프레임 %1  |  %2 ms  |  엔티티 %3  |  충돌 %4")
                          .arg(frame)
                          .arg(frameMs, 0, 'f', 2)
                          .arg(scene.GetEntityCount())
                          .arg(scene.GetCollisionEvents2D().size()));
    state_->clear();
    auto add = [this](const QString& key, const QString& value, uint32_t entityId = 0) {
        auto* row = new QTreeWidgetItem(state_);
        row->setText(0, key);
        row->setText(1, value);
        row->setData(0, Qt::UserRole, entityId);
        row->setToolTip(1, value);
    };
    if (selectedId && scene.IsAlive(selectedId)) {
        add(QStringLiteral("선택 엔티티"), QString::number(selectedId), selectedId);
        if (const auto* transform =
                scene.TryGetComponent<LuxEngine::TransformComponent>(selectedId))
            add(QStringLiteral("위치"), QStringLiteral("%1, %2, %3")
                                            .arg(transform->position.x, 0, 'f', 2)
                                            .arg(transform->position.y, 0, 'f', 2)
                                            .arg(transform->position.z, 0, 'f', 2));
        if (const auto* body = scene.TryGetComponent<LuxEngine::Rigidbody2DComponent>(selectedId)) {
            add(QStringLiteral("2D 속도"), QStringLiteral("%1, %2")
                                               .arg(body->velocity.x, 0, 'f', 2)
                                               .arg(body->velocity.y, 0, 'f', 2));
            add(QStringLiteral("접지"),
                body->isGrounded ? QStringLiteral("예") : QStringLiteral("아니요"));
        }
        if (const auto* animation =
                scene.TryGetComponent<LuxEngine::AnimationComponent>(selectedId))
            add(QStringLiteral("애니메이션 시간"), QString::number(animation->currentTime, 'f', 2));
    }
    for (const auto& event : scene.GetCollisionEvents2D())
        add(event.isTrigger ? QStringLiteral("트리거") : QStringLiteral("충돌"),
            QStringLiteral("#%1 ↔ #%2  법선 (%3, %4)")
                .arg(event.entityA)
                .arg(event.entityB)
                .arg(event.normal.x, 0, 'f', 2)
                .arg(event.normal.y, 0, 'f', 2),
            event.entityA);
    // 개발 중: 엔진 접촉 이벤트에 접점 좌표가 추가되면 뷰포트에도 접점을 표시합니다.
}
} // namespace LuxEditor
