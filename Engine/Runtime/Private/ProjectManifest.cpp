#include <LuxEngine/Runtime/ProjectManifest.hpp>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace LuxEngine {
bool ReadProjectStartScene(const std::filesystem::path& contentRoot,
                           std::filesystem::path& sceneFile, std::string& error) {
    error.clear();
    try {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto manifest = contentRoot / "project.luxproject";
        if (!fs::exists(manifest, ec))
            manifest = contentRoot / "project.luxproj";
        const auto size = fs::file_size(manifest, ec);
        if (ec || size == 0 || size > 65536)
            throw std::runtime_error("프로젝트 실행 정보를 읽을 수 없습니다.");
        std::ifstream input(manifest, std::ios::binary);
        std::string data(static_cast<size_t>(size), '\0');
        if (!input.read(data.data(), static_cast<std::streamsize>(data.size())) ||
            input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("프로젝트 실행 정보를 읽을 수 없습니다.");
        std::string scene;
        const auto first = data.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && data[first] == '{') {
            using Json = nlohmann::json;
            std::vector<std::set<std::string>> keys;
            const auto root = Json::parse(data, [&](int depth, Json::parse_event_t event,
                                                    Json& parsed) {
                if (depth > 16)
                    throw std::runtime_error("프로젝트 JSON 중첩이 너무 깊습니다.");
                if (event == Json::parse_event_t::object_start)
                    keys.emplace_back();
                else if (event == Json::parse_event_t::key &&
                         (keys.empty() || !keys.back().insert(parsed.get<std::string>()).second))
                    throw std::runtime_error("프로젝트 JSON에 중복된 필드가 있습니다.");
                else if (event == Json::parse_event_t::object_end)
                    keys.pop_back();
                return true;
            });
            if (!root.is_object() || root.at("format") != "LuxEngine.Project" ||
                !root.at("version").is_number_integer() || root.at("version") != 1 ||
                !root.at("start_scene").is_string())
                throw std::runtime_error("프로젝트 실행 정보 형식이 올바르지 않습니다.");
            scene = root.at("start_scene").get<std::string>();
        } else {
            std::istringstream legacy(data);
            std::string magic;
            int version = 0;
            if (!(legacy >> magic >> version) || magic != "LUXPROJECT" ||
                (version != 1 && version != 2))
                throw std::runtime_error("프로젝트 실행 정보 형식이 올바르지 않습니다.");
            std::string line;
            bool found = false;
            while (std::getline(legacy, line)) {
                std::istringstream field(line);
                std::string key, trailing;
                if (!(field >> key) || key != "start_scene")
                    continue;
                if (found || !(field >> std::quoted(scene)) || field >> trailing)
                    throw std::runtime_error("시작 씬 설정이 올바르지 않습니다.");
                found = true;
            }
            if (!found || legacy.bad())
                throw std::runtime_error("시작 씬을 지정해 주세요.");
        }
        const fs::path relative(
            std::u8string(reinterpret_cast<const char8_t*>(scene.data()), scene.size()));
        if (relative.empty() || relative.is_absolute() || relative.extension() != ".lux" ||
            scene.find('\\') != std::string::npos || scene.find(':') != std::string::npos ||
            scene.find('\0') != std::string::npos)
            throw std::runtime_error("시작 씬 경로가 올바르지 않습니다.");
        for (const auto& part : relative)
            if (part == ".." || part == ".")
                throw std::runtime_error("시작 씬 경로가 콘텐츠 폴더를 벗어났습니다.");
        const auto root = fs::canonical(contentRoot);
        const auto target = fs::canonical(root / relative);
        const auto resolved = target.lexically_relative(root);
        if (resolved.empty() || !fs::is_regular_file(target))
            throw std::runtime_error("시작 씬 파일을 찾을 수 없습니다.");
        for (const auto& part : resolved)
            if (part == "..")
                throw std::runtime_error("시작 씬 경로가 콘텐츠 폴더를 벗어났습니다.");
        sceneFile = target;
        return true;
    } catch (const std::exception&) {
        error = "프로젝트 실행 정보 또는 시작 씬 경로가 올바르지 않습니다.";
        return false;
    }
}
} // namespace LuxEngine
