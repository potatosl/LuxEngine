#include "ForestSurvivorApp.hpp"
#include <LuxEngine/EntryPoint.hpp>
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
            std::cout << "MOSSBOUND - 마지막 숲의 수호자\n"
                      << "WASD / 방향키 / 왼쪽 스틱: 이동 | Space / X: 돌진\n"
                      << "시작하기 버튼으로 게임 시작 | 1 / 2 / 3 또는 마우스: 강화 선택\n"
                      << "P / Esc / Start: 일시정지 | 메뉴 버튼으로 계속하기·재시작·종료\n"
                      << "F11: 전체 화면\n"
                      << "실행 옵션:\n"
                      << "  --smoke-test            초기화·실행 점검\n"
                      << "  --autoplay              자동 플레이\n"
                      << "  --frames N              실행할 프레임 수 제한\n"
                      << "  --seed N                게임 난수 시드 지정\n"
                      << "  --showcase SECONDS      지정한 시간의 게임 상태 미리보기\n"
                      << "  --showcase-upgrade      강화 선택 화면 미리보기\n"
                      << "  --capture FILE.bmp      화면을 BMP 파일로 저장\n"
                      << "  --assets PATH           게임 리소스 폴더 지정\n";
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
