#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEngine/Core/Log.hpp>
#include <LuxUI/Widgets.hpp>
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
            LuxUI::TemporaryDir tempSettings;
            if (tempSettings.isValid()) {
                LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
                LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                                         tempSettings.path());
            }
            LuxUI::Application app(argc, argv);
            app.SetIdentity("LuxEngine", "LuxEditor", LUX_EDITOR_ENGINE_VERSION);
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

    LuxUI::Application app(argc, argv);
    app.SetIdentity("LuxEngine", "LuxEditor", LUX_EDITOR_ENGINE_VERSION);

    LuxEditor::EditorWindow window;
    window.show();
    return app.Run();
}
