#pragma once

namespace LuxSamples {
template <class App> int RunSample(int argc, char** argv) {
    auto options = ParseOptions(argc, argv);
    if (options.help) {
        std::cout << App::Help
                  << "\n실행 옵션: --smoke-test, --frames N, --capture image.ppm\n"
                     "P: 일시정지, R: 다시 시작, Esc: 종료\n";
        return 0;
    }
    App app(std::move(options));
    return app.Initialize() ? app.Run() : 1;
}
} // namespace LuxSamples
