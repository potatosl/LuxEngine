#include <LuxUI/Widgets.hpp>
#include <QPointer>
#include <stdexcept>
#include <utility>

namespace LuxUI {
namespace {
String Text(std::string_view value) {
    return String::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {}

Application::~Application() = default;

int Application::Run() {
    return exec();
}

void Application::SetIdentity(std::string_view organization, std::string_view name,
                              std::string_view version) {
    setOrganizationName(Text(organization));
    setApplicationName(Text(name));
    setApplicationVersion(Text(version));
}

MainWindow::MainWindow(Widget* parent) : QMainWindow(parent) {
    setDockNestingEnabled(true);
}

MainWindow::~MainWindow() = default;

DockWidget* MainWindow::AddPanel(std::string_view id, std::string_view title, Widget* content,
                                 Flags::DockWidgetArea area) {
    if (id.empty() || !content || content == this || content->isAncestorOf(this) ||
        findChild<DockWidget*>(Text(id)))
        throw std::invalid_argument("패널 ID와 콘텐츠를 확인해 주세요.");
    auto* panel = new DockWidget(Text(title), this);
    panel->setObjectName(Text(id));
    panel->setWidget(content);
    addDockWidget(area, panel);
    return panel;
}

Action* MainWindow::AddCommand(Menu* menu, std::string_view id, std::string_view title,
                               std::function<void()> execute) {
    if (!menu || id.empty() || !execute || findChild<Action*>(Text(id)))
        throw std::invalid_argument("명령 ID와 실행 동작을 확인해 주세요.");
    auto* action = new Action(Text(title), this);
    action->setObjectName(Text(id));
    menu->addAction(action);
    connect(action, &Action::triggered, this, [this, callback = std::move(execute)] {
        QPointer<MainWindow> guard(this);
        try {
            callback();
        } catch (const std::exception& error) {
            if (guard)
                Q_EMIT guard->CommandFailed(String::fromUtf8(error.what()));
        } catch (...) {
            if (guard)
                Q_EMIT guard->CommandFailed(
                    LUX_UI_TEXT("도구 실행 중 알 수 없는 오류가 발생했습니다."));
        }
    });
    return action;
}
} // namespace LuxUI
