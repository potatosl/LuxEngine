#include "Engine/EntryPoint.hpp"
#include "ForestSurvivorApp.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
unsigned Number(std::string_view text) {
    unsigned number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("숫자 옵션이 올바르지 않습니다.");
    return number;
}
} // namespace
int RunForestSurvivor(int argc, char** argv) {
    ForestGame::RunOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const auto value = [&]() -> std::string_view {
            if (++index >= argc)
                throw std::invalid_argument("옵션 값이 필요합니다.");
            return argv[index];
        };
        if (argument == "--help" || argument == "-h") {
            std::cout << "MOSSBOUND - Keeper of the Last Grove\n"
                      << "WASD / arrows / left stick: move | Space / X: dash\n"
                      << "Click PLAY to begin | 1/2/3 or mouse: choose upgrade\n"
                      << "P / Esc / Start: pause | Use menu buttons to resume/retry/quit | F11: "
                         "fullscreen\n"
                      << "Options: --smoke-test --autoplay --frames N --seed N\n"
                      << "         --showcase SECONDS --showcase-upgrade --capture FILE.bmp "
                         "--assets PATH\n";
            return 0;
        } else if (argument == "--smoke-test") {
            options.smokeTest = true;
            options.maxFrames = 10;
        } else if (argument == "--autoplay") {
            options.autoplay = true;
        } else if (argument == "--frames") {
            options.maxFrames = Number(value());
            if (options.maxFrames == 0 || options.maxFrames > 1000000)
                throw std::invalid_argument("프레임 제한은 1에서 1000000 사이여야 합니다.");
        } else if (argument == "--seed") {
            options.seed = Number(value());
        } else if (argument == "--showcase-upgrade") {
            options.stopAtUpgrade = true;
            options.showcaseSeconds = 60;
        } else if (argument == "--showcase") {
            const unsigned seconds = Number(value());
            if (seconds > 300)
                throw std::invalid_argument("미리보기 시간은 300초 이하여야 합니다.");
            options.showcaseSeconds = static_cast<float>(seconds);
        } else if (argument == "--capture") {
            options.capture = value();
        } else if (argument == "--assets") {
            options.assets = value();
        } else {
            throw std::invalid_argument("알 수 없는 실행 옵션입니다.");
        }
    }
    if (!options.capture.empty() && options.maxFrames == 0)
        options.maxFrames = 10;
    ForestGame::ForestSurvivorApp app(std::move(options));
    if (!app.Initialize())
        return 1;
    return app.Run();
}

LUX_ENGINE_MAIN(RunForestSurvivor)
