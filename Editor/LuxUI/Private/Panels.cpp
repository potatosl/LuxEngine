#include <LuxUI/Panels.hpp>
#include <QDateTime>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <algorithm>
#include <cmath>

namespace LuxUI {
namespace {
String Text(std::string_view value) {
    return String::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

String FormatMessage(const Message& message) {
    const char* name = message.severity == Severity::Error     ? "오류"
                       : message.severity == Severity::Warning ? "경고"
                                                               : "정보";
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(message.timestamp.time_since_epoch())
            .count();
    auto text = Text(message.text);
    // One record per block makes bounded retention and incremental removal exact.
    text.replace('\r', ' ');
    text.replace('\n', QChar(0x21b5));
    return LUX_UI_TEXT("[%1] [%2] [%3] %4")
        .arg(QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss.zzz"), String::fromUtf8(name),
             Text(message.category), text);
}
} // namespace

struct ConsolePanel::Impl {
    QPointer<Widget> root;
    ComboBox* level = nullptr;
    LineEdit* search = nullptr;
    PlainTextEdit* output = nullptr;
    LineEdit* input = nullptr;
    MessageStore store;
    std::function<void(std::string)> execute;
    std::deque<unsigned long long> displayed;
    unsigned long long consumed = 0;
    bool queued = false;

    bool Matches(const Message& message) const {
        return (!level->currentIndex() ||
                static_cast<int>(message.severity) == level->currentIndex() - 1) &&
               Text(message.text).contains(search->text(), Flags::CaseInsensitive);
    }

    void Flush() {
        queued = false;
        if (!root)
            return;
        const auto oldest = store.FirstSequence();
        while (!displayed.empty() && displayed.front() < oldest) {
            QTextCursor cursor(output->document()->firstBlock());
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.removeSelectedText();
            cursor.deleteChar();
            displayed.pop_front();
        }
        const auto incoming = store.Snapshot(consumed);
        if (incoming.empty())
            return;
        auto* scrollbar = output->verticalScrollBar();
        const bool following = scrollbar->value() >= scrollbar->maximum();
        const int previousScroll = scrollbar->value();
        output->setUpdatesEnabled(false);
        for (const auto& message : incoming) {
            consumed = message.sequence;
            if (!Matches(message))
                continue;
            output->appendPlainText(FormatMessage(message));
            displayed.push_back(message.sequence);
            if (displayed.size() > 2000)
                displayed.pop_front();
        }
        output->setUpdatesEnabled(true);
        if (following)
            scrollbar->setValue(scrollbar->maximum());
        else
            scrollbar->setValue(previousScroll);
    }

    void Refresh() {
        if (!root)
            return;
        output->clear();
        displayed.clear();
        consumed = 0;
        Flush();
    }

    void Schedule() {
        if (!root || queued)
            return;
        queued = true;
        Timer::singleShot(0, root, [this] { Flush(); });
    }
};

ConsolePanel::ConsolePanel(Widget* parent) : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.root = new Widget(parent);
    p.root->setObjectName(LUX_UI_TEXT("luxConsolePanel"));
    auto* layout = new VBoxLayout(p.root);
    auto* controls = new HBoxLayout;
    p.level = new ComboBox(p.root);
    p.level->setObjectName(LUX_UI_TEXT("luxConsoleLevel"));
    p.level->addItems(
        {LUX_UI_TEXT("전체"), LUX_UI_TEXT("정보"), LUX_UI_TEXT("경고"), LUX_UI_TEXT("오류")});
    p.search = new LineEdit(p.root);
    p.search->setObjectName(LUX_UI_TEXT("luxConsoleSearch"));
    p.search->setPlaceholderText(LUX_UI_TEXT("로그 검색"));
    auto* clear = new PushButton(LUX_UI_TEXT("지우기"), p.root);
    clear->setObjectName(LUX_UI_TEXT("luxConsoleClear"));
    auto* save = new PushButton(LUX_UI_TEXT("저장…"), p.root);
    controls->addWidget(p.level);
    controls->addWidget(p.search, 1);
    controls->addWidget(clear);
    controls->addWidget(save);
    layout->addLayout(controls);
    p.output = new PlainTextEdit(p.root);
    p.output->setObjectName(LUX_UI_TEXT("luxConsoleOutput"));
    p.output->setReadOnly(true);
    p.output->setLineWrapMode(PlainTextEdit::NoWrap);
    p.output->setMaximumBlockCount(2000);
    layout->addWidget(p.output);
    p.input = new LineEdit(p.root);
    p.input->setObjectName(LUX_UI_TEXT("luxConsoleCommand"));
    p.input->setPlaceholderText(
        LUX_UI_TEXT("명령 입력: help, play, pause, step, stop, frame, tools"));
    layout->addWidget(p.input);
    Object::connect(p.level, &ComboBox::currentIndexChanged, p.root, [this] { impl_->Refresh(); });
    Object::connect(p.search, &LineEdit::textChanged, p.root, [this] { impl_->Refresh(); });
    Object::connect(clear, &PushButton::clicked, p.root, [this] {
        impl_->store.Clear();
        impl_->Refresh();
    });
    Object::connect(save, &PushButton::clicked, p.root, [this] {
        QPointer<Widget> guard = impl_->root;
        const auto path =
            FileDialog::getSaveFileName(guard, LUX_UI_TEXT("로그 저장"), "LuxEditor.log");
        if (!guard || path.isEmpty())
            return;
        impl_->Flush();
        SaveFile file(path);
        const auto bytes = impl_->output->toPlainText().toUtf8();
        if (!file.open(IODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            Append(Severity::Error, "콘솔", "로그 파일을 저장하지 못했습니다.");
    });
    Object::connect(p.input, &LineEdit::returnPressed, p.root, [this] {
        const auto command = impl_->input->text().trimmed().toUtf8().toStdString();
        impl_->input->clear();
        const auto execute = impl_->execute;
        if (!command.empty() && execute)
            execute(command);
    });
}

ConsolePanel::~ConsolePanel() {
    delete impl_->root.data();
}

Widget* ConsolePanel::View() const {
    return impl_->root;
}

void ConsolePanel::Append(Severity severity, std::string_view category, std::string_view text) {
    impl_->store.Add(severity, category, text);
    impl_->Schedule();
}

void ConsolePanel::SetCommandHandler(std::function<void(std::string)> callback) {
    impl_->execute = std::move(callback);
}

namespace {
class FrameGraph final : public Widget {
public:
    FrameHistory history;

    explicit FrameGraph(Widget* parent) : Widget(parent) {
        setMinimumHeight(140);
    }

    void paintEvent(QPaintEvent*) override {
        Painter painter(this);
        painter.fillRect(rect(), Color(23, 27, 31));
        const auto samples = history.Samples();
        double maximum = 33.333;
        for (const auto& sample : samples)
            maximum = std::max(maximum, sample.updateMs + sample.renderMs);
        painter.setPen(Color(65, 75, 83));
        const auto budget = height() * (1 - 16.667 / maximum);
        painter.drawLine(PointF(0, budget), PointF(width(), budget));
        painter.setPen(Pen(Color(104, 205, 153), 1.5));
        for (size_t i = 1; i < samples.size(); ++i) {
            const auto y = [&](size_t n) {
                return height() * (1 - (samples[n].updateMs + samples[n].renderMs) / maximum);
            };
            painter.drawLine(PointF((i - 1) * width() / 239., y(i - 1)),
                             PointF(i * width() / 239., y(i)));
        }
    }
};
} // namespace

struct ProfilerPanel::Impl {
    QPointer<Widget> root;
    FrameGraph* graph = nullptr;
    Label* values = nullptr;
    bool paused = false;
};

ProfilerPanel::ProfilerPanel(Widget* parent) : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.root = new Widget(parent);
    p.root->setObjectName(LUX_UI_TEXT("luxProfilerPanel"));
    auto* layout = new VBoxLayout(p.root);
    auto* controls = new HBoxLayout;
    auto* pause = new CheckBox(LUX_UI_TEXT("수집 일시정지"), p.root);
    auto* reset = new PushButton(LUX_UI_TEXT("기록 지우기"), p.root);
    reset->setObjectName(LUX_UI_TEXT("luxProfilerClear"));
    auto* exportFile = new PushButton(LUX_UI_TEXT("CSV 내보내기…"), p.root);
    controls->addWidget(pause);
    controls->addStretch();
    controls->addWidget(reset);
    controls->addWidget(exportFile);
    layout->addLayout(controls);
    p.graph = new FrameGraph(p.root);
    layout->addWidget(p.graph);
    p.values = new Label(LUX_UI_TEXT("프로파일 샘플을 기다리고 있습니다."), p.root);
    p.values->setObjectName(LUX_UI_TEXT("luxProfilerValues"));
    p.values->setWordWrap(true);
    layout->addWidget(p.values);
    Object::connect(pause, &CheckBox::toggled, p.root,
                    [this](bool value) { impl_->paused = value; });
    Object::connect(reset, &PushButton::clicked, p.root, [this] {
        impl_->graph->history.Clear();
        impl_->graph->update();
        impl_->values->setText(LUX_UI_TEXT("프로파일 기록을 지웠습니다."));
    });
    Object::connect(exportFile, &PushButton::clicked, p.root, [this] {
        QPointer<Widget> guard = impl_->root;
        const auto path =
            FileDialog::getSaveFileName(guard, LUX_UI_TEXT("프로파일 저장"), "profile.csv");
        if (!guard || path.isEmpty())
            return;
        SaveFile file(path);
        ByteArray csv("update_ms,render_ms,gpu_ms,draw_calls,gpu_bytes\n");
        for (const auto& sample : impl_->graph->history.Samples())
            csv += String("%1,%2,%3,%4,%5\n")
                       .arg(sample.updateMs, 0, 'g', 16)
                       .arg(sample.renderMs, 0, 'g', 16)
                       .arg(sample.gpuMs, 0, 'g', 16)
                       .arg(sample.drawCalls)
                       .arg(sample.gpuBytes)
                       .toUtf8();
        if (!file.open(IODevice::WriteOnly) || file.write(csv) != csv.size() || !file.commit())
            impl_->values->setText(LUX_UI_TEXT("프로파일 파일을 저장하지 못했습니다."));
    });
}

ProfilerPanel::~ProfilerPanel() {
    delete impl_->root.data();
}

Widget* ProfilerPanel::View() const {
    return impl_->root;
}

void ProfilerPanel::Sample(FrameSample sample) {
    if (impl_->paused || !impl_->root)
        return;
    auto& history = impl_->graph->history;
    history.Push(sample);
    const auto samples = history.Samples();
    if (samples.empty())
        return;
    const auto& current = samples.back();
    impl_->graph->update();
    impl_->values->setText(LUX_UI_TEXT("CPU 업데이트 %1 ms | 렌더 %2 ms | 평균 %3 ms | P95 %4 "
                                       "ms\nGPU %5 | Draw %6 | GPU 할당 %7 MiB")
                               .arg(current.updateMs, 0, 'f', 2)
                               .arg(current.renderMs, 0, 'f', 2)
                               .arg(history.MeanCpuMs(), 0, 'f', 2)
                               .arg(history.Percentile95CpuMs(), 0, 'f', 2)
                               .arg(current.gpuMs >= 0
                                        ? String::number(current.gpuMs, 'f', 2) + " ms"
                                        : LUX_UI_TEXT("측정 불가"))
                               .arg(current.drawCalls)
                               .arg(current.gpuBytes / 1048576., 0, 'f', 2));
}
} // namespace LuxUI
