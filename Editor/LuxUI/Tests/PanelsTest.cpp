#include <LuxUI/AssetList.hpp>
#include <LuxUI/Panels.hpp>
#include <LuxUI/Widgets.hpp>

#include <limits>
#include <memory>

namespace {
void Flush() {
    LuxUI::CoreApplication::processEvents();
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application application(argc, argv);
    application.SetIdentity("LuxUI", "LuxUI Panel Tests", "1");
    LuxUI::MainWindow window;
    auto console = std::make_unique<LuxUI::ConsolePanel>(&window);
    auto* panel =
        window.AddPanel("console", "Console", console->View(), LuxUI::Flags::BottomDockWidgetArea);
    if (!panel || !qobject_cast<LuxUI::MainWindow*>(&window))
        return 1;
    auto* menu = window.menuBar()->addMenu(LUX_UI_TEXT("도구"));
    bool executed = false;
    auto* action = window.AddCommand(menu, "test", "Test", [&] { executed = true; });
    action->trigger();
    if (!executed)
        return 2;
    bool failed = false;
    LuxUI::Object::connect(&window, &LuxUI::MainWindow::CommandFailed, &window,
                           [&](const LuxUI::String&) { failed = true; });
    window.AddCommand(menu, "throw", "Throw", [] { throw std::runtime_error("tool error"); })
        ->trigger();
    if (!failed)
        return 11;

    auto* output =
        console->View()->findChild<LuxUI::PlainTextEdit*>(LUX_UI_TEXT("luxConsoleOutput"));
    auto* filter = console->View()->findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("luxConsoleLevel"));
    auto* search = console->View()->findChild<LuxUI::LineEdit*>(LUX_UI_TEXT("luxConsoleSearch"));
    auto* clear = console->View()->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("luxConsoleClear"));
    if (!output || !filter || !search || !clear)
        return 3;
    console->Append(LuxUI::Severity::Error, "test", "old error");
    Flush();
    filter->setCurrentIndex(3);
    for (int i = 0; i < 2100; ++i)
        console->Append(LuxUI::Severity::Info, "test", "message " + std::to_string(i));
    Flush();
    if (!output->toPlainText().isEmpty())
        return 4;
    filter->setCurrentIndex(0);
    if (output->document()->blockCount() != 2000 ||
        output->toPlainText().contains(LUX_UI_TEXT("old error")))
        return 5;
    search->setText(LUX_UI_TEXT("2099"));
    if (!output->toPlainText().contains(LUX_UI_TEXT("message 2099")) ||
        output->document()->blockCount() != 1)
        return 6;
    clear->click();
    console->Append(LuxUI::Severity::Warning, "test", "after clear 2099\nsecond line");
    Flush();
    if (!output->toPlainText().contains(LUX_UI_TEXT("after clear 2099")) ||
        output->document()->blockCount() != 1)
        return 7;

    bool command = false;
    console->SetCommandHandler([&](std::string text) { command = text == "help"; });
    auto* input = console->View()->findChild<LuxUI::LineEdit*>(LUX_UI_TEXT("luxConsoleCommand"));
    input->setText(LUX_UI_TEXT("help"));
    LuxUI::MetaObject::invokeMethod(input, "returnPressed", LuxUI::Flags::DirectConnection);
    if (!command || !input->text().isEmpty())
        return 8;

    auto profiler = std::make_unique<LuxUI::ProfilerPanel>(&window);
    auto* values = profiler->View()->findChild<LuxUI::Label*>(LUX_UI_TEXT("luxProfilerValues"));
    profiler->Sample({2, 3, -1, 12, 1048576});
    if (!values || !values->text().contains(LUX_UI_TEXT("측정 불가")) ||
        !values->text().contains(LUX_UI_TEXT("P95 5.00")))
        return 9;
    profiler->Sample({std::numeric_limits<double>::quiet_NaN(), 0});
    auto* reset = profiler->View()->findChild<LuxUI::PushButton*>(LUX_UI_TEXT("luxProfilerClear"));
    reset->click();
    if (!values->text().contains(LUX_UI_TEXT("지웠습니다")))
        return 10;

    // Native parents can disappear before their owning panel services.
    delete console->View();
    console->Append(LuxUI::Severity::Info, "test", "after native destruction");
    delete profiler->View();
    profiler->Sample({1, 2});
    console.reset();
    profiler.reset();
    Flush();
    return 0;
}
