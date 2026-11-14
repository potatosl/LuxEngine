#include <LuxEditor/Debug/DebugPanel.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>

namespace LuxEditor {
DebugPanel::DebugPanel(LuxUI::Widget* parent, std::function<void(uint32_t)> onSelect)
    : LuxUI::Widget(parent), onSelect_(std::move(onSelect)) {
    auto* layout = new LuxUI::VBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* controls = new LuxUI::HBoxLayout;
    filter_ = new LuxUI::ComboBox(this);
    filter_->addItems({LUX_UI_TEXT("전체"), LUX_UI_TEXT("오류"), LUX_UI_TEXT("정보")});
    filter_->setToolTip(LUX_UI_TEXT("로그 유형 필터"));
    search_ = new LuxUI::LineEdit(this);
    search_->setPlaceholderText(LUX_UI_TEXT("로그 검색"));
    auto* clear = new LuxUI::ToolButton(this);
    clear->setIcon(MakeEditorIcon(EditorIcon::Delete));
    clear->setToolTip(LUX_UI_TEXT("디버그 로그 지우기"));
    auto* save = new LuxUI::ToolButton(this);
    save->setIcon(MakeEditorIcon(EditorIcon::Save));
    save->setToolTip(LUX_UI_TEXT("디버그 로그 저장"));
    controls->addWidget(filter_);
    controls->addWidget(search_, 1);
    controls->addWidget(clear);
    controls->addWidget(save);
    layout->addLayout(controls);
    output_ = new LuxUI::PlainTextEdit(this);
    output_->setObjectName(LUX_UI_TEXT("debugLog"));
    output_->setReadOnly(true);
    output_->setMaximumHeight(80);
    layout->addWidget(output_, 2);
    metrics_ = new LuxUI::Label(LUX_UI_TEXT("실행 대기 중"), this);
    metrics_->setWordWrap(true);
    metrics_->setMinimumHeight(34);
    layout->addWidget(metrics_);
    state_ = new LuxUI::TreeWidget(this);
    state_->setObjectName(LUX_UI_TEXT("debugState"));
    state_->setHeaderLabels({LUX_UI_TEXT("상태"), LUX_UI_TEXT("값")});
    state_->setRootIsDecorated(false);
    state_->setHorizontalScrollBarPolicy(LuxUI::Flags::ScrollBarAlwaysOff);
    state_->header()->setSectionResizeMode(0, LuxUI::HeaderView::Fixed);
    state_->header()->resizeSection(0, 90);
    state_->header()->setSectionResizeMode(1, LuxUI::HeaderView::Stretch);
    layout->addWidget(state_, 1);
    connect(filter_, &LuxUI::ComboBox::currentIndexChanged, this, [this] { FilterLogs(); });
    connect(search_, &LuxUI::LineEdit::textChanged, this, [this] { FilterLogs(); });
    connect(clear, &LuxUI::ToolButton::clicked, this, [this] {
        logs_.clear();
        FilterLogs();
    });
    connect(save, &LuxUI::ToolButton::clicked, this, [this] {
        const LuxUI::String path = LuxUI::FileDialog::getSaveFileName(
            this, LUX_UI_TEXT("디버그 로그 저장"), LUX_UI_TEXT("lux-debug.log"),
            LUX_UI_TEXT("로그 파일 (*.log *.txt)"));
        if (path.isEmpty())
            return;
        LuxUI::SaveFile file(path);
        if (!file.open(LuxUI::IODevice::WriteOnly))
            return;
        for (const auto& entry : logs_) {
            const LuxUI::ByteArray line =
                ((entry.error ? LUX_UI_TEXT("[오류] ") : LUX_UI_TEXT("[정보] ")) + entry.message +
                 LuxUI::Latin1Char('\n'))
                    .toUtf8();
            if (file.write(line) != line.size())
                return;
        }
        file.commit();
    });
    connect(state_, &LuxUI::TreeWidget::itemDoubleClicked, this,
            [this](LuxUI::TreeWidgetItem* item) {
                const uint32_t id = item->data(0, LuxUI::Flags::UserRole).toUInt();
                if (id)
                    onSelect_(id);
            });
}

void DebugPanel::AddLog(const LuxUI::String& message, bool error) {
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
            !entry.message.contains(search_->text(), LuxUI::Flags::CaseInsensitive))
            continue;
        output_->appendPlainText((entry.error ? LUX_UI_TEXT("[오류] ") : LUX_UI_TEXT("[정보] ")) +
                                 entry.message);
    }
}

void DebugPanel::UpdateScene(LuxEngine::Scene& scene, uint32_t selectedId, uint64_t frame,
                             double frameMs) {
    metrics_->setText(LUX_UI_TEXT("프레임 %1  |  %2 ms  |  엔티티 %3  |  충돌 %4")
                          .arg(frame)
                          .arg(frameMs, 0, 'f', 2)
                          .arg(scene.GetEntityCount())
                          .arg(scene.GetCollisionEvents2D().size()));
    state_->clear();
    auto add = [this](const LuxUI::String& key, const LuxUI::String& value, uint32_t entityId = 0) {
        auto* row = new LuxUI::TreeWidgetItem(state_);
        row->setText(0, key);
        row->setText(1, value);
        row->setData(0, LuxUI::Flags::UserRole, entityId);
        row->setToolTip(1, value);
    };
    if (selectedId && scene.IsAlive(selectedId)) {
        add(LUX_UI_TEXT("선택 엔티티"), LuxUI::String::number(selectedId), selectedId);
        if (const auto* transform =
                scene.TryGetComponent<LuxEngine::TransformComponent>(selectedId))
            add(LUX_UI_TEXT("위치"), LUX_UI_TEXT("%1, %2, %3")
                                         .arg(transform->position.x, 0, 'f', 2)
                                         .arg(transform->position.y, 0, 'f', 2)
                                         .arg(transform->position.z, 0, 'f', 2));
        if (const auto* body = scene.TryGetComponent<LuxEngine::Rigidbody2DComponent>(selectedId)) {
            add(LUX_UI_TEXT("2D 속도"), LUX_UI_TEXT("%1, %2")
                                            .arg(body->velocity.x, 0, 'f', 2)
                                            .arg(body->velocity.y, 0, 'f', 2));
            add(LUX_UI_TEXT("접지"), body->isGrounded ? LUX_UI_TEXT("예") : LUX_UI_TEXT("아니요"));
        }
        if (const auto* animation =
                scene.TryGetComponent<LuxEngine::AnimationComponent>(selectedId))
            add(LUX_UI_TEXT("애니메이션 시간"),
                LuxUI::String::number(animation->currentTime, 'f', 2));
    }
    for (const auto& event : scene.GetCollisionEvents2D())
        add(event.isTrigger ? LUX_UI_TEXT("트리거") : LUX_UI_TEXT("충돌"),
            LUX_UI_TEXT("#%1 ↔ #%2  법선 (%3, %4)")
                .arg(event.entityA)
                .arg(event.entityB)
                .arg(event.normal.x, 0, 'f', 2)
                .arg(event.normal.y, 0, 'f', 2),
            event.entityA);
    // 개발 중: 엔진 접촉 이벤트에 접점 좌표가 추가되면 뷰포트에도 접점을 표시합니다.
}
} // namespace LuxEditor
