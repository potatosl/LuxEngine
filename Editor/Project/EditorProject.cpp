#include "EditorProject.hpp"

#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace LuxEngine {
namespace {
constexpr std::array<const char*, 5> TemplateNames = {"empty", "platformer", "top-down", "rpg",
                                                      "puzzle"};

bool ReadField(std::istream& input, const char* expected, std::string& value) {
    std::string key;
    return static_cast<bool>(input >> key >> std::quoted(value)) && key == expected;
}

bool ValidName(const std::string& name) {
    return !name.empty() && name.size() <= 128 && name != "." && name != ".." &&
           name.back() != ' ' && name.back() != '.' &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) {
               return c < 32 || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos;
           });
}

bool ValidRelativeScene(const std::filesystem::path& scene) {
    if (scene.empty() || scene.is_absolute() || scene.extension() != ".lux" ||
        scene.generic_string().find('\\') != std::string::npos ||
        scene.generic_string().find(':') != std::string::npos)
        return false;
    for (const auto& part : scene) {
        if (part == ".." || part == ".")
            return false;
    }
    return true;
}

bool ReplaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
}

bool WriteProjectFile(const EditorProject& project, const std::filesystem::path& scene) {
    const auto temporary = project.GetFile().string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "LUXPROJECT 1\n"
               << "name " << std::quoted(project.GetName()) << '\n'
               << "engine_version " << std::quoted(project.GetEngineVersion()) << '\n'
               << "template " << std::quoted(ProjectTemplateName(project.GetTemplate())) << '\n'
               << "start_scene " << std::quoted(scene.generic_string()) << '\n';
        output.close();
        if (!output)
            return false;
    }
    return ReplaceFile(temporary, project.GetFile());
}

void AddSprite(Scene& scene, const char* name, float x, float y, float width, float height,
               Vec4 color) {
    auto entity = scene.CreateEntity(name);
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.position = {x, y, 0};
    transform.scale = {width, height, 1};
    entity.AddComponent<SpriteRendererComponent>().color = color;
}

void PopulateTemplate(Scene& scene, ProjectTemplate value) {
    switch (value) {
    case ProjectTemplate::Empty:
        return;
    case ProjectTemplate::Platformer:
        AddSprite(scene, "바닥", 0, -3, 12, 1, {0.28f, 0.48f, 0.28f, 1});
        AddSprite(scene, "플레이어", -3, -1.8f, 0.8f, 1.2f, {0.92f, 0.7f, 0.3f, 1});
        break;
    case ProjectTemplate::TopDown:
        AddSprite(scene, "지면", 0, 0, 12, 8, {0.34f, 0.52f, 0.34f, 1});
        AddSprite(scene, "플레이어", 0, 0, 0.8f, 0.8f, {0.95f, 0.82f, 0.43f, 1});
        break;
    case ProjectTemplate::Rpg:
        AddSprite(scene, "마을 광장", 0, 0, 12, 8, {0.52f, 0.48f, 0.36f, 1});
        AddSprite(scene, "주인공", -1, 0, 0.8f, 0.8f, {0.35f, 0.55f, 0.85f, 1});
        AddSprite(scene, "NPC", 2, 1, 0.8f, 0.8f, {0.82f, 0.52f, 0.38f, 1});
        break;
    case ProjectTemplate::Puzzle:
        AddSprite(scene, "보드", 0, 0, 8, 8, {0.3f, 0.34f, 0.43f, 1});
        AddSprite(scene, "블록", 0, 0, 1, 1, {0.78f, 0.63f, 0.35f, 1});
        break;
    }
}
} // namespace

const char* ProjectTemplateName(ProjectTemplate value) {
    const auto index = static_cast<size_t>(value);
    return index < TemplateNames.size() ? TemplateNames[index] : TemplateNames[0];
}

std::optional<EditorProject> EditorProject::Create(const std::filesystem::path& directory,
                                                   const std::string& name,
                                                   ProjectTemplate projectTemplate,
                                                   std::string& error) {
    if (!ValidName(name) || directory.empty() ||
        static_cast<size_t>(projectTemplate) >= TemplateNames.size()) {
        error = "프로젝트 이름, 경로 또는 템플릿이 올바르지 않습니다.";
        return std::nullopt;
    }
    std::error_code ec;
    const auto root = std::filesystem::absolute(directory, ec).lexically_normal();
    if (ec) {
        error = "프로젝트 경로를 확인할 수 없습니다.";
        return std::nullopt;
    }
    if (std::filesystem::exists(root, ec) && !std::filesystem::is_empty(root, ec)) {
        error = "프로젝트 폴더가 비어 있지 않습니다.";
        return std::nullopt;
    }
    if (ec) {
        error = "프로젝트 폴더를 확인할 수 없습니다.";
        return std::nullopt;
    }
    std::filesystem::create_directories(root / "Scenes", ec);
    if (!ec)
        std::filesystem::create_directories(root / "Assets", ec);
    if (ec) {
        error = "프로젝트 폴더를 만들 수 없습니다.";
        return std::nullopt;
    }
    EditorProject project;
    project.file_ = root / FileName;
    project.name_ = name;
    project.projectTemplate_ = projectTemplate;
    Scene scene;
    PopulateTemplate(scene, projectTemplate);
    if (!scene.Serialize(project.GetSceneFile().string())) {
        error = "시작 장면을 저장할 수 없습니다.";
        return std::nullopt;
    }
    if (!WriteProjectFile(project, project.scene_)) {
        error = "프로젝트 파일을 저장할 수 없습니다.";
        return std::nullopt;
    }
    return project;
}

std::optional<EditorProject> EditorProject::Open(const std::filesystem::path& file,
                                                 std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) > 65536 || ec) {
        error = "프로젝트 파일을 읽을 수 없거나 크기가 너무 큽니다.";
        return std::nullopt;
    }
    std::ifstream input(file, std::ios::binary);
    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "LUXPROJECT" || version != 1) {
        error = "지원하지 않는 프로젝트 파일 형식입니다.";
        return std::nullopt;
    }
    EditorProject project;
    std::string projectTemplate;
    std::string scene;
    if (!ReadField(input, "name", project.name_) ||
        !ReadField(input, "engine_version", project.engineVersion_) ||
        !ReadField(input, "template", projectTemplate) || !ReadField(input, "start_scene", scene)) {
        error = "프로젝트 메타데이터가 손상되었습니다.";
        return std::nullopt;
    }
    std::string trailing;
    const auto found = std::find_if(TemplateNames.begin(), TemplateNames.end(),
                                    [&](const char* value) { return projectTemplate == value; });
    project.scene_ = scene;
    if (input >> trailing || !ValidName(project.name_) || project.engineVersion_.empty() ||
        found == TemplateNames.end() || !ValidRelativeScene(project.scene_)) {
        error = "프로젝트 메타데이터 값이 올바르지 않습니다.";
        return std::nullopt;
    }
    project.projectTemplate_ = static_cast<ProjectTemplate>(found - TemplateNames.begin());
    project.file_ = std::filesystem::absolute(file, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(project.GetSceneFile(), ec)) {
        error = "시작 장면을 찾을 수 없습니다.";
        return std::nullopt;
    }
    Scene validation;
    if (!validation.Deserialize(project.GetSceneFile().string())) {
        error = "시작 장면 파일을 읽을 수 없습니다.";
        return std::nullopt;
    }
    return project;
}

bool EditorProject::Rename(const std::string& name, std::string& error) {
    if (!ValidName(name)) {
        error = "프로젝트 이름이 올바르지 않습니다.";
        return false;
    }
    const std::string previous = name_;
    name_ = name;
    if (!WriteProjectFile(*this, scene_)) {
        name_ = previous;
        error = "프로젝트 파일을 저장할 수 없습니다.";
        return false;
    }
    return true;
}

void RecentProjects::Load() {
    files_.clear();
    std::ifstream input(storageFile_, std::ios::binary);
    std::string header;
    if (!(input >> header) || header != "LUXRECENT1")
        return;
    std::string item;
    while (files_.size() < 10 && input >> std::quoted(item)) {
        std::string error;
        if (EditorProject::Open(item, error)) {
            const std::filesystem::path path = std::filesystem::path(item).lexically_normal();
            if (std::find(files_.begin(), files_.end(), path) == files_.end())
                files_.push_back(path);
        }
    }
}

bool RecentProjects::Add(const std::filesystem::path& projectFile) {
    std::string error;
    auto project = EditorProject::Open(projectFile, error);
    if (!project)
        return false;
    const auto path = project->GetFile();
    files_.erase(std::remove(files_.begin(), files_.end(), path), files_.end());
    files_.insert(files_.begin(), path);
    if (files_.size() > 10)
        files_.resize(10);
    return Save();
}

bool RecentProjects::Save() const {
    std::error_code ec;
    if (!storageFile_.parent_path().empty())
        std::filesystem::create_directories(storageFile_.parent_path(), ec);
    if (ec)
        return false;
    const auto temporary = storageFile_.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "LUXRECENT1\n";
        for (const auto& file : files_)
            output << std::quoted(file.string()) << '\n';
        output.close();
        if (!output)
            return false;
    }
    return ReplaceFile(temporary, storageFile_);
}

} // namespace LuxEngine
