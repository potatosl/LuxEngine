#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectFileOperations.hpp>
#include <LuxEditor/Project/ProjectJson.hpp>
#include <LuxEditor/Project/ProjectTemplates.hpp>

#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace LuxEditor {
namespace {
constexpr std::array<const char*, 6> TemplateNames = {"empty", "platformer", "top-down",
                                                      "rpg",   "puzzle",     "pbr-3d"};

bool ParseVersion(std::string_view text, std::array<unsigned, 3>& parts) {
    for (size_t index = 0; index < parts.size(); ++index) {
        const auto dot = text.find('.');
        const auto token = text.substr(0, dot);
        if (token.empty() || token.size() > 4)
            return false;
        const auto [end, error] =
            std::from_chars(token.data(), token.data() + token.size(), parts[index]);
        if (error != std::errc{} || end != token.data() + token.size() || parts[index] > 9999)
            return false;
        if (index < 2 && dot == std::string_view::npos)
            return false;
        if (index == 2 && dot != std::string_view::npos)
            return false;
        text.remove_prefix(token.size() + (dot != std::string_view::npos ? 1 : 0));
    }
    return true;
}

bool ValidDependency(const ProjectDependency& dependency) {
    if (dependency.package.empty() || dependency.package.size() > 64 ||
        !std::isalpha(static_cast<unsigned char>(dependency.package[0])))
        return false;
    for (unsigned char c : dependency.package)
        if (!std::isalnum(c) && c != '_')
            return false;
    if (dependency.version.empty())
        return true;
    return std::all_of(dependency.version.begin(), dependency.version.end(),
                       [](unsigned char c) { return std::isdigit(c) || c == '.'; });
}

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
        GenericPathUtf8(scene).find('\\') != std::string::npos ||
        GenericPathUtf8(scene).find(':') != std::string::npos)
        return false;
    for (const auto& part : scene) {
        if (part == ".." || part == ".")
            return false;
    }
    return true;
}

bool ValidSettings(const ProjectSettings& settings) {
    std::array<unsigned, 3> version{};
    if (!ValidName(settings.name) || !ParseVersion(settings.version, version) ||
        settings.autosaveSeconds > 3600 || settings.backupLimit == 0 ||
        settings.backupLimit > 100 || settings.dependencies.size() > 64)
        return false;
    if (!settings.icon.empty()) {
        if (settings.icon.is_absolute() ||
            GenericPathUtf8(settings.icon).find("..") != std::string::npos ||
            GenericPathUtf8(settings.icon).find('\\') != std::string::npos)
            return false;
        const auto extension = PathUtf8(settings.icon.extension());
        if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
            extension != ".ico" && extension != ".icns")
            return false;
    }
    for (const auto& dependency : settings.dependencies)
        if (!ValidDependency(dependency))
            return false;
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
    static std::atomic<uint64_t> sequence{0};
    auto temporary = project.GetFile();
    temporary += ".writing-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(++sequence);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{temporary};
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto data = SerializeProjectJson(project, scene);
        if (data.empty())
            return false;
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output)
            return false;
    }
    return ReplaceFile(temporary, project.GetFile());
}

bool WriteDependencies(const EditorProject& project) {
    const auto target = project.GetFile().parent_path() / "Dependencies.cmake";
    auto temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "# LuxEditor에서 생성한 프로젝트 종속성입니다.\n";
        for (const auto& dependency : project.GetSettings().dependencies) {
            output << "find_package(" << dependency.package;
            if (!dependency.version.empty())
                output << ' ' << dependency.version;
            output << " CONFIG REQUIRED)\n";
        }
        output.close();
        if (!output)
            return false;
    }
    return ReplaceFile(temporary, target);
}

void AddSprite(LuxEngine::Scene& scene, const char* name, float x, float y, float width,
               float height, LuxEngine::Vec4 color) {
    auto entity = scene.CreateEntity(name);
    auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
    transform.position = {x, y, 0};
    transform.scale = {width, height, 1};
    entity.AddComponent<LuxEngine::SpriteRendererComponent>().color = color;
}

void PopulateTemplate(LuxEngine::Scene& scene, ProjectTemplate value) {
    switch (value) {
    case ProjectTemplate::Pbr3D: {
        auto settings = scene.GetSettings();
        settings.render3D.mode = LuxEngine::Render3DMode::Pbr;
        settings.render3D.showSkybox = true;
        scene.SetSettings(settings);
        for (int i = 0; i < 4; ++i) {
            auto entity = scene.CreateEntity(i == 3 ? "Ground" : "PBR Material");
            auto& transform = entity.GetComponent<LuxEngine::TransformComponent>();
            transform.position = {static_cast<float>(i - 1) * 2.5f, 1, 0};
            auto& mesh = entity.AddComponent<LuxEngine::MeshRendererComponent>();
            mesh.material.albedoColor = {.7f, .35f, .12f, 1};
            mesh.material.metallic = i == 0 ? 1.f : 0.f;
            mesh.material.roughness = i == 1 ? .15f : .65f;
            if (i == 3) {
                transform.position = {0, -.25f, 0};
                transform.scale = {14, .5f, 10};
                mesh.material.albedoColor = {.3f, .3f, .3f, 1};
            }
        }
        break;
    }
    case ProjectTemplate::Empty:
        return;
    case ProjectTemplate::Platformer:
        AddSprite(scene, "바닥", 0, -3, 12, 1, {0.28f, 0.48f, 0.28f, 1});
        AddSprite(scene, "플레이어", -3, -1.8f, 0.8f, 1.2f, {0.92f, 0.7f, 0.3f, 1});
        {
            auto entities = scene.GetEntities();
            auto floor = entities[0];
            floor.AddComponent<LuxEngine::BoxCollider2DComponent>();
            auto player = entities[1];
            player.AddComponent<LuxEngine::Rigidbody2DComponent>();
            player.AddComponent<LuxEngine::BoxCollider2DComponent>();
        }
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
        AddSprite(scene, "목표", 2, 2, 1, 1, {0.42f, 0.72f, 0.48f, 1});
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
    error.clear();
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
    if (std::filesystem::exists(root, ec)) {
        error = "프로젝트 폴더가 이미 있습니다. 새 폴더 경로를 선택해 주세요.";
        return std::nullopt;
    }
    if (ec) {
        error = "프로젝트 폴더를 확인할 수 없습니다.";
        return std::nullopt;
    }
    std::filesystem::create_directories(root.parent_path(), ec);
    auto staging = root;
    staging += ".lux-creating-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    bool createdStaging = false;
    if (!ec)
        createdStaging = std::filesystem::create_directory(staging, ec);
    if (!createdStaging && !ec)
        ec = std::make_error_code(std::errc::file_exists);
    if (!ec)
        std::filesystem::create_directories(staging / "Scenes", ec);
    if (!ec)
        std::filesystem::create_directories(staging / "Assets", ec);
    if (ec) {
        error = "프로젝트 폴더를 만들 수 없습니다.";
        std::error_code cleanup;
        if (createdStaging)
            std::filesystem::remove_all(staging, cleanup);
        return std::nullopt;
    }
    const auto discardStaging = [&staging]() {
        std::error_code cleanup;
        std::filesystem::remove_all(staging, cleanup);
    };
    EditorProject project;
    project.file_ = staging / FileName;
    project.settings_.name = name;
    project.engineVersion_ = CurrentEngineVersion();
    project.projectTemplate_ = projectTemplate;
    LuxEngine::Scene scene;
    PopulateTemplate(scene, projectTemplate);
    if (!scene.Serialize(PathUtf8(project.GetSceneFile()))) {
        error = "시작 장면을 저장할 수 없습니다.";
        discardStaging();
        return std::nullopt;
    }
    if (!WriteProjectFile(project, project.scene_)) {
        error = "프로젝트 파일을 저장할 수 없습니다.";
        discardStaging();
        return std::nullopt;
    }
    if (!WriteDependencies(project) || !CreateTemplateFiles(staging, projectTemplate, error)) {
        if (error.empty())
            error = "프로젝트 종속성 파일을 저장할 수 없습니다.";
        discardStaging();
        return std::nullopt;
    }
    RenameProjectPath(staging, root, ec);
    if (ec) {
        error = "프로젝트 폴더를 최종 위치로 옮길 수 없습니다.";
        discardStaging();
        return std::nullopt;
    }
    project.file_ = root / FileName;
    return project;
}

std::optional<EditorProject> EditorProject::Open(const std::filesystem::path& file,
                                                 std::string& error) {
    return Read(file, error, false);
}

std::optional<EditorProject> EditorProject::OpenForRecovery(const std::filesystem::path& file,
                                                            std::string& error) {
    return Read(file, error, true);
}

std::optional<EditorProject> EditorProject::Read(const std::filesystem::path& file,
                                                 std::string& error, bool allowRecovery) {
    error.clear();
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) > 65536 || ec) {
        error = "프로젝트 파일을 읽을 수 없거나 크기가 너무 큽니다.";
        return std::nullopt;
    }
    std::ifstream input(file, std::ios::binary);
    EditorProject project;
    std::string projectTemplate;
    std::string scene;
    int legacyVersion = 0;
    input >> std::ws;
    if (input.peek() == '{') {
        const std::string data((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        if (!DeserializeProjectJson(data, project.settings_, project.engineVersion_,
                                    projectTemplate, scene)) {
            error = "프로젝트 JSON 형식 또는 메타데이터가 올바르지 않습니다.";
            return std::nullopt;
        }
        input.peek();
    } else {
        std::string magic;
        if (!(input >> magic >> legacyVersion) || magic != "LUXPROJECT" ||
            (legacyVersion != 1 && legacyVersion != 2)) {
            error = "지원하지 않는 프로젝트 파일 형식입니다.";
            return std::nullopt;
        }
        if (!ReadField(input, "name", project.settings_.name) ||
            !ReadField(input, "engine_version", project.engineVersion_) ||
            (legacyVersion == 2 &&
             !ReadField(input, "project_version", project.settings_.version)) ||
            !ReadField(input, "template", projectTemplate) ||
            !ReadField(input, "start_scene", scene)) {
            error = "프로젝트 메타데이터가 손상되었습니다.";
            return std::nullopt;
        }
        if (legacyVersion == 2) {
            std::string icon;
            std::string key;
            size_t dependencyCount = 0;
            if (!ReadField(input, "icon", icon) ||
                !(input >> key >> project.settings_.autosaveSeconds) || key != "autosave_seconds" ||
                !(input >> key >> project.settings_.backupLimit) || key != "backup_limit" ||
                !(input >> key >> project.settings_.snapToGrid) || key != "snap_to_grid" ||
                !(input >> key >> dependencyCount) || key != "dependency_count" ||
                dependencyCount > 64) {
                error = "프로젝트 설정이 손상되었습니다.";
                return std::nullopt;
            }
            project.settings_.icon = PathFromUtf8(icon);
            for (size_t index = 0; index < dependencyCount; ++index) {
                ProjectDependency dependency;
                if (!(input >> key >> std::quoted(dependency.package) >>
                      std::quoted(dependency.version)) ||
                    key != "dependency") {
                    error = "프로젝트 종속성 정보가 손상되었습니다.";
                    return std::nullopt;
                }
                project.settings_.dependencies.push_back(std::move(dependency));
            }
        }
    }
    std::string trailing;
    const auto found = std::find_if(TemplateNames.begin(), TemplateNames.end(),
                                    [&](const char* value) { return projectTemplate == value; });
    project.scene_ = PathFromUtf8(scene);
    std::array<unsigned, 3> currentVersion{}, fileVersion{};
    if (input >> trailing || !ValidSettings(project.settings_) ||
        !ParseVersion(project.engineVersion_, fileVersion) ||
        !ParseVersion(CurrentEngineVersion(), currentVersion) || found == TemplateNames.end() ||
        !ValidRelativeScene(project.scene_)) {
        error = "프로젝트 메타데이터 값이 올바르지 않습니다.";
        return std::nullopt;
    }
    // Windows cannot replace a file that is still open, and the v1 migration below rewrites it.
    input.close();
    if (fileVersion[0] != currentVersion[0] || fileVersion[1] != currentVersion[1]) {
        error = "프로젝트와 현재 엔진의 주·부 버전이 다릅니다. 호환되는 엔진으로 열어 주세요.";
        return std::nullopt;
    }
    project.projectTemplate_ = static_cast<ProjectTemplate>(found - TemplateNames.begin());
    project.file_ = std::filesystem::absolute(file, ec).lexically_normal();
    if (ec) {
        error = "프로젝트 경로를 확인할 수 없습니다.";
        return std::nullopt;
    }
    if (!project.ContainsScene(project.GetSceneFile())) {
        error = "시작 씬은 프로젝트 내부에 있어야 합니다.";
        return std::nullopt;
    }
    LuxEngine::Scene validation;
    if (!validation.Deserialize(PathUtf8(project.GetSceneFile())) &&
        (!allowRecovery ||
         !validation.Deserialize(PathUtf8(project.GetAutosaveFile(project.GetSceneFile()))))) {
        error = "시작 장면 파일을 읽을 수 없습니다.";
        return std::nullopt;
    }
    if (!project.settings_.icon.empty() &&
        !std::filesystem::is_regular_file(project.file_.parent_path() / project.settings_.icon,
                                          ec)) {
        error = "프로젝트 아이콘 파일을 찾을 수 없습니다.";
        return std::nullopt;
    }
    if (legacyVersion == 1) {
        auto backup = project.file_;
        backup += ".v1.bak";
        std::filesystem::copy_file(project.file_, backup,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "프로젝트 파일을 새 형식으로 변환하지 못했습니다.";
            return std::nullopt;
        }
        const auto root = project.file_.parent_path();
        if (!std::filesystem::exists(root / "CMakeLists.txt") &&
            !std::filesystem::exists(root / "Source" / "Main.cpp") &&
            !CreateTemplateFiles(root, project.projectTemplate_, error))
            return std::nullopt;
        if (!WriteDependencies(project) || !WriteProjectFile(project, project.scene_)) {
            error = "프로젝트 파일을 새 형식으로 변환하지 못했습니다.";
            return std::nullopt;
        }
    }
    return project;
}

bool EditorProject::Rename(const std::string& name, std::string& error) {
    auto settings = settings_;
    settings.name = name;
    return UpdateSettings(settings, error);
}

bool EditorProject::SetStartScene(const std::filesystem::path& sceneFile, std::string& error) {
    error.clear();
    std::error_code ec;
    const auto root = std::filesystem::canonical(file_.parent_path(), ec);
    if (ec) {
        error = "프로젝트 경로를 확인할 수 없습니다.";
        return false;
    }
    const auto candidate = sceneFile.is_absolute() ? sceneFile : root / sceneFile;
    const auto target = std::filesystem::canonical(candidate, ec);
    if (ec || !std::filesystem::is_regular_file(target, ec) || ec) {
        error = "시작 씬 파일을 찾을 수 없습니다.";
        return false;
    }
    auto relative = target.lexically_relative(root);
    if (!ValidRelativeScene(relative) || !ContainsScene(target)) {
        error = "시작 씬은 프로젝트 내부의 .lux 파일이어야 합니다.";
        return false;
    }
    if (!LuxEngine::SceneManager().Load(target, error))
        return false;
    if (relative == scene_)
        return true;
    if (!WriteProjectFile(*this, relative)) {
        error = "시작 씬 설정을 저장할 수 없습니다.";
        return false;
    }
    scene_ = std::move(relative);
    return true;
}

bool EditorProject::UpdateSettings(const ProjectSettings& settings, std::string& error) {
    error.clear();
    if (!ValidSettings(settings)) {
        error = "프로젝트 설정 값이 올바르지 않습니다.";
        return false;
    }
    std::error_code ec;
    if (!settings.icon.empty() &&
        !std::filesystem::is_regular_file(file_.parent_path() / settings.icon, ec)) {
        error = "프로젝트 아이콘 파일을 찾을 수 없습니다.";
        return false;
    }
    const auto previous = settings_;
    settings_ = settings;
    if (!WriteProjectFile(*this, scene_)) {
        settings_ = previous;
        error = "프로젝트 파일을 저장할 수 없습니다.";
        return false;
    }
    if (!WriteDependencies(*this)) {
        settings_ = previous;
        WriteProjectFile(*this, scene_);
        error = "종속성 파일을 저장할 수 없습니다.";
        return false;
    }
    return true;
}

std::string EditorProject::CurrentEngineVersion() {
#ifdef LUX_EDITOR_ENGINE_VERSION
    return LUX_EDITOR_ENGINE_VERSION;
#else
    return "0.1.0";
#endif
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
        const auto path = PathFromUtf8(item).lexically_normal();
        if (EditorProject::OpenForRecovery(path, error)) {
            if (std::find(files_.begin(), files_.end(), path) == files_.end())
                files_.push_back(path);
        }
    }
}

bool RecentProjects::Add(const std::filesystem::path& projectFile) {
    std::string error;
    auto project = EditorProject::OpenForRecovery(projectFile, error);
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
    auto temporary = storageFile_;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "LUXRECENT1\n";
        for (const auto& file : files_)
            output << std::quoted(PathUtf8(file)) << '\n';
        output.close();
        if (!output)
            return false;
    }
    return ReplaceFile(temporary, storageFile_);
}

} // namespace LuxEditor
