#include "EditorWindow.hpp"
#include <QApplication>
#include <QCoreApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("LuxEditor"));
    LuxEngine::EditorWindow window;
    window.show();
    return app.exec();
}
