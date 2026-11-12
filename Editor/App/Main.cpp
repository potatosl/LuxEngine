#include "EditorWindow.hpp"
#include "Engine/Core/Log.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef LUX_EDITOR_ENGINE_VERSION
#define LUX_EDITOR_ENGINE_VERSION "0.1.0"
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    // If stdout is not redirected to a pipe or file, attach to caller's console
    const HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD outType = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE)
                              ? GetFileType(hStdOut)
                              : FILE_TYPE_UNKNOWN;
    if (outType != FILE_TYPE_PIPE && outType != FILE_TYPE_DISK) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
    }
#endif

    LuxEngine::Log::ConfigureUtf8Output();
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        const std::string_view arg(argv[i]);
        if (arg == "--version" || arg == "-v") {
            std::cout << "LuxEditor " << LUX_EDITOR_ENGINE_VERSION << std::endl;
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "LuxEditor " << LUX_EDITOR_ENGINE_VERSION
                      << " - 2D/3D Game Editor\nUsage: LuxEditor.exe [options]\n"
                      << "  --version, -v   Display version information\n"
                      << "  --smoke-test    Run offscreen initialization check\n"
                      << "  --help, -h      Display this help\n";
            return 0;
        }
        if (arg == "--smoke-test") {
            qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
            LuxEditor::EngineSceneRenderer::SetForceAllowed(false);
            QTemporaryDir tempSettings;
            if (tempSettings.isValid()) {
                QSettings::setDefaultFormat(QSettings::IniFormat);
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempSettings.path());
            }
            QApplication app(argc, argv);
            QCoreApplication::setOrganizationName(QStringLiteral("LuxEngine"));
            QCoreApplication::setApplicationName(QStringLiteral("LuxEditor"));
            {
                LuxEditor::EditorWindow window;
                window.show();
                app.processEvents();
                window.GetDocument().SetDirty(false);
                window.close();
                app.processEvents();
            }
            std::cout
                << "[LuxEditor] Smoke test passed: LuxEditor initialized successfully (version "
                << LUX_EDITOR_ENGINE_VERSION << ")." << std::endl;
            std::cout.flush();
            return 0;
        }
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("LuxEditor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUX_EDITOR_ENGINE_VERSION));

    LuxEditor::EditorWindow window;
    window.show();
    return app.exec();
}
