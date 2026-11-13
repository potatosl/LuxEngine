#include <LuxEngine/EntryPoint.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <cstdio>
#include <string_view>

namespace {
int Convert(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("LuxSceneConvert <input.lux> <output.lux>\n기존 씬을 JSON 형식으로 변환합니다. "
                  "같은 경로를 지정하면 원자적으로 교체합니다.");
        return 0;
    }
    if (argc != 3) {
        std::fputs("사용법: LuxSceneConvert <input.lux> <output.lux>\n", stderr);
        return 2;
    }
    LuxEngine::Scene scene;
    if (!scene.Deserialize(argv[1])) {
        std::fputs("씬 파일을 읽을 수 없습니다.\n", stderr);
        return 1;
    }
    if (!scene.Serialize(argv[2])) {
        std::fputs("씬 파일을 저장할 수 없습니다.\n", stderr);
        return 1;
    }
    return 0;
}
} // namespace
LUX_ENGINE_MAIN(Convert)
