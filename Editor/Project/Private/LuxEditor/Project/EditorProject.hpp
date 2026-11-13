#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {

enum class ProjectTemplate { Empty, Platformer, TopDown, Rpg, Puzzle, Pbr3D };

struct ProjectDependency {
    std::string package;
    std::string version;
    bool operator==(const ProjectDependency&) const = default;
};

struct ProjectSettings {
    std::string name;
    std::string version = "0.1.0";
    std::filesystem::path icon;
    uint32_t autosaveSeconds = 120;
    uint32_t backupLimit = 10;
    bool snapToGrid = true;
    std::vector<ProjectDependency> dependencies;
};

const char* ProjectTemplateName(ProjectTemplate value);

class EditorProject final {
public:
    static constexpr const char* FileName = "project.luxproject";
    static std::optional<EditorProject> Create(const std::filesystem::path& directory,
                                               const std::string& name,
                                               ProjectTemplate projectTemplate, std::string& error);
    static std::optional<EditorProject> Open(const std::filesystem::path& file, std::string& error);
    // Editor recovery only: a broken/missing start scene is accepted only with a valid autosave.
    // Archive validation must continue using Open().
    static std::optional<EditorProject> OpenForRecovery(const std::filesystem::path& file,
                                                        std::string& error);
    bool Rename(const std::string& name, std::string& error);
    bool UpdateSettings(const ProjectSettings& settings, std::string& error);
    bool SetStartScene(const std::filesystem::path& sceneFile, std::string& error);
    bool SetIcon(const std::filesystem::path& image, std::string& error);
    bool BumpVersion(int part, std::string& error);
    std::filesystem::path GetAutosaveFile(const std::filesystem::path& sceneFile) const;
    bool SaveAutosave(const std::filesystem::path& sceneFile, LuxEngine::Scene& scene,
                      std::string& error) const;
    bool ContainsScene(const std::filesystem::path& sceneFile) const;
    bool HasNewerAutosave(const std::filesystem::path& sceneFile) const;
    bool ClearAutosave(const std::filesystem::path& sceneFile) const;
    size_t RescanResources(std::string& error) const;

    const std::filesystem::path& GetFile() const noexcept {
        return file_;
    }

    std::filesystem::path GetSceneFile() const {
        return file_.parent_path() / scene_;
    }

    const std::string& GetName() const noexcept {
        return settings_.name;
    }

    const std::string& GetEngineVersion() const noexcept {
        return engineVersion_;
    }

    ProjectTemplate GetTemplate() const noexcept {
        return projectTemplate_;
    }

    const ProjectSettings& GetSettings() const noexcept {
        return settings_;
    }

    const std::filesystem::path& GetIcon() const noexcept {
        return settings_.icon;
    }

    static std::string CurrentEngineVersion();

private:
    static std::optional<EditorProject> Read(const std::filesystem::path& file, std::string& error,
                                             bool allowRecovery);
    std::filesystem::path file_;
    std::filesystem::path scene_ = "Scenes/Main.lux";
    std::string engineVersion_ = "0.1.0";
    ProjectTemplate projectTemplate_ = ProjectTemplate::Empty;
    ProjectSettings settings_;
};

class RecentProjects final {
public:
    explicit RecentProjects(std::filesystem::path storageFile)
        : storageFile_(std::move(storageFile)) {}

    void Load();
    bool Add(const std::filesystem::path& projectFile);

    const std::vector<std::filesystem::path>& Get() const noexcept {
        return files_;
    }

private:
    bool Save() const;
    std::filesystem::path storageFile_;
    std::vector<std::filesystem::path> files_;
};

} // namespace LuxEditor
