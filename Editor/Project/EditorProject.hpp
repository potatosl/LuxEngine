#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LuxEngine {

enum class ProjectTemplate { Empty, Platformer, TopDown, Rpg, Puzzle };

const char* ProjectTemplateName(ProjectTemplate value);

class EditorProject final {
public:
    static constexpr const char* FileName = "project.luxproj";
    static std::optional<EditorProject> Create(const std::filesystem::path& directory,
                                               const std::string& name,
                                               ProjectTemplate projectTemplate, std::string& error);
    static std::optional<EditorProject> Open(const std::filesystem::path& file, std::string& error);
    bool Rename(const std::string& name, std::string& error);

    const std::filesystem::path& GetFile() const noexcept {
        return file_;
    }
    std::filesystem::path GetSceneFile() const {
        return file_.parent_path() / scene_;
    }
    const std::string& GetName() const noexcept {
        return name_;
    }
    const std::string& GetEngineVersion() const noexcept {
        return engineVersion_;
    }
    ProjectTemplate GetTemplate() const noexcept {
        return projectTemplate_;
    }

private:
    std::filesystem::path file_;
    std::filesystem::path scene_ = "Scenes/Main.lux";
    std::string name_;
    std::string engineVersion_ = "0.1.0";
    ProjectTemplate projectTemplate_ = ProjectTemplate::Empty;
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

} // namespace LuxEngine
