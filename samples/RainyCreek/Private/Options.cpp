#include <RainyCreek/Catalog.hpp>
#include <RainyCreek/Options.hpp>
#include <SDL3/SDL_filesystem.h>
#include <charconv>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace RainyCreek {
namespace {
unsigned Unsigned(std::string_view text, unsigned minimum, unsigned maximum) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || result < minimum ||
        result > maximum)
        throw std::runtime_error("숫자 옵션이 허용 범위를 벗어났습니다.");
    return result;
}

double Time(std::string_view text) {
    double result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(result) ||
        result < 0 || result > 86400)
        throw std::runtime_error("캡처 시간은 0~86400초 사이여야 합니다.");
    return result;
}
} // namespace

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--help")
            options.help = true;
        else if (option == "--validate-assets")
            options.validate = true;
        else if (option == "--smoke-test") {
            options.smoke = true;
            options.frames = 72;
        } else if ((option == "--assets" || option == "--capture" || option == "--frames" ||
                    option == "--time" || option == "--preset" || option == "--size") &&
                   i + 1 < argc) {
            const std::string_view value(argv[++i]);
            if (option == "--assets")
                options.assets = FromUtf8(value);
            else if (option == "--capture")
                options.capture = FromUtf8(value);
            else if (option == "--frames")
                options.frames = Unsigned(value, 1, 100000);
            else if (option == "--time")
                options.fixedTime = Time(value);
            else if (option == "--preset") {
                if (value == "night")
                    options.lighting = Lighting::Night;
                else if (value == "dusk")
                    options.lighting = Lighting::Dusk;
                else if (value == "day")
                    options.lighting = Lighting::Day;
                else
                    throw std::runtime_error(
                        "조명 프리셋은 night, dusk, day 중 하나를 사용하세요.");
            } else {
                const auto split = value.find('x');
                if (split == std::string_view::npos)
                    throw std::runtime_error("창 크기는 WIDTHxHEIGHT 형식으로 지정하세요.");
                options.width = Unsigned(value.substr(0, split), 640, 3840);
                options.height = Unsigned(value.substr(split + 1), 480, 2160);
            }
        } else
            throw std::runtime_error(
                "지원하지 않거나 값이 누락된 옵션입니다. --help를 확인하세요.");
    }
    if (options.assets.empty()) {
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("실행 파일 경로를 확인하지 못했습니다.");
        options.assets = FromUtf8(base) / "assets" / "rainy-creek";
    }
    if (!options.capture.empty()) {
        if (options.capture.extension() != ".png" && options.capture.extension() != ".ppm")
            throw std::runtime_error("캡처 파일은 .png 또는 .ppm 확장자를 사용하세요.");
        if (!options.frames)
            options.frames = 48;
    }
    return options;
}

void PrintHelp() {
    std::cout << "Rainy Creek | LuxEngine 3D 숲과 개울\n"
                 "실행 즉시 장면을 표시합니다. 키보드와 마우스로 조작할 수 있습니다.\n"
                 "왼쪽 드래그: 회전 | 가운데 드래그: 화면 이동 | 휠: 확대/축소\n"
                 "Space: 일시정지 | R/Esc: 시점과 설정 초기화 | L: 조명 | P: 가로등\n"
                 "S: 그림자 | W: 비 | H: 수면 | G: 바람 | J: 거리별 LOD\n"
                 "+/-: 노출 | 창 닫기: 종료\n"
                 "--validate-assets  GPU 없이 모델·텍스처·HDR 형식을 검사합니다.\n"
                 "--smoke-test       장면 토글·카메라·창 크기 변경을 72프레임 검사합니다.\n"
                 "--capture PATH     마지막 프레임을 PNG/PPM으로 저장합니다.\n"
                 "--frames N         지정 프레임 뒤 종료합니다.\n"
                 "--time SECONDS     시뮬레이션 시간을 고정합니다.\n"
                 "--preset night|dusk|day  시작 조명을 지정합니다.\n"
                 "--size WIDTHxHEIGHT  창 크기를 지정합니다.\n"
                 "--assets PATH      변환된 에셋 디렉터리를 지정합니다.\n";
}

} // namespace RainyCreek
