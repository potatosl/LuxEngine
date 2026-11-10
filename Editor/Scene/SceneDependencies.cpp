#include "SceneDependencies.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/SceneManager.hpp"
#include "Project/EditorProject.hpp"
#include "Project/PathEncoding.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace LuxEditor {
namespace {
namespace fs = std::filesystem;
constexpr size_t MaximumDepth = 16;
constexpr std::uintmax_t MaximumBytes = 128 * 1024 * 1024;

bool Ignored(const fs::path& relative) {
    if (relative.empty())
        return false;
    const auto first = PathUtf8(*relative.begin());
    if (first == "build" || first == "out" || first.starts_with("cmake-build"))
        return true;
    for (const auto& part : relative)
        if (part == ".git" || part == ".lux")
            return true;
    return false;
}

class ReferenceValidator final {
public:
    explicit ReferenceValidator(const EditorProject& project) : project_(project) {
        std::error_code error;
        root = fs::canonical(project.GetFile().parent_path(), error);
        if (error)
            throw std::runtime_error("프로젝트 경로를 확인할 수 없습니다.");
    }

    void ValidateMemory(LuxEngine::Scene& scene) {
        const auto origin = scene.GetSourcePath().empty() ? project_.GetSceneFile()
                                                          : PathFromUtf8(scene.GetSourcePath());
        std::error_code error;
        const auto canonical = fs::weakly_canonical(origin, error);
        const bool hasOrigin = !scene.GetSourcePath().empty() && !error;
        if (hasOrigin)
            active_.insert(canonical);
        Inspect(scene, origin, 0);
        if (hasOrigin)
            active_.erase(canonical);
    }

    void ValidateFile(const fs::path& path, size_t depth = 0) {
        CheckDepth(depth);
        const auto canonical = RequireFile(path);
        if (active_.contains(canonical))
            throw std::runtime_error("씬 참조가 순환합니다: " + GenericPathUtf8(canonical));
        if (const auto checked = checked_.find(canonical);
            checked != checked_.end() && checked->second >= depth)
            return;
        const auto data = Read(canonical);
        LuxEngine::Scene parsed;
        if (!parsed.DeserializeFromString(data))
            throw std::runtime_error("씬 파일 형식이 올바르지 않습니다: " +
                                     GenericPathUtf8(canonical));
        active_.insert(canonical);
        Inspect(parsed, canonical, depth);
        active_.erase(canonical);
        checked_[canonical] = depth;
    }

    fs::path root;

private:
    static void CheckDepth(size_t depth) {
        if (depth > MaximumDepth)
            throw std::runtime_error("씬 참조 검증의 중첩 한도 16을 넘었습니다.");
    }
    void Charge(std::uintmax_t bytes) {
        if (bytes > remaining_)
            throw std::runtime_error("씬 참조 검증 데이터가 128 MiB를 넘었습니다.");
        remaining_ -= bytes;
    }
    fs::path RequireFile(const fs::path& path) const {
        std::error_code error;
        const auto resolved = fs::canonical(path, error);
        if (error || !fs::is_regular_file(resolved, error) || error)
            throw std::runtime_error("참조 씬 파일을 찾을 수 없습니다: " + GenericPathUtf8(path));
        if (!project_.ContainsScene(resolved))
            throw std::runtime_error("프로젝트 밖의 씬은 게임에 포함할 수 없습니다: " +
                                     GenericPathUtf8(path));
        return resolved;
    }
    std::string Read(const fs::path& path) {
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error || size == 0)
            throw std::runtime_error("씬 파일을 읽을 수 없습니다: " + GenericPathUtf8(path));
        Charge(size);
        std::ifstream input(path, std::ios::binary);
        std::string data(static_cast<size_t>(size), '\0');
        if (!input || !input.read(data.data(), static_cast<std::streamsize>(data.size())) ||
            input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("씬 파일을 읽는 동안 내용이 변경되었습니다: " +
                                     GenericPathUtf8(path));
        return data;
    }
    void Inspect(LuxEngine::Scene& scene, const fs::path& owner, size_t depth) {
        CheckDepth(depth);
        for (const auto& entity : scene.GetEntities()) {
            const auto* reference = entity.TryGetComponent<LuxEngine::SceneReferenceComponent>();
            if (!reference)
                continue;
            if (reference->path.empty() || reference->path.size() > 4096)
                throw std::runtime_error("씬 참조 경로가 올바르지 않습니다.");
            const auto path = PathFromUtf8(reference->path);
            ValidateFile(path.is_absolute() ? path : owner.parent_path() / path, depth + 1);
            for (const std::string* data : {&reference->baseline, &reference->suspended}) {
                if (data->empty())
                    continue;
                CheckDepth(depth + 1);
                Charge(data->size());
                LuxEngine::Scene snapshot;
                if (!snapshot.DeserializeFromString(*data))
                    throw std::runtime_error("저장된 씬 참조 상태가 손상되었습니다.");
                // On-disk snapshots share the containing scene's path context. Manager-loaded
                // snapshots contain absolute paths, so the same rule also accepts live scenes.
                Inspect(snapshot, owner, depth + 1);
            }
        }
    }

    const EditorProject& project_;
    std::uintmax_t remaining_ = MaximumBytes;
    std::set<fs::path> active_;
    std::map<fs::path, size_t> checked_;
};
} // namespace

bool ValidateProjectSceneReferences(const EditorProject& project, LuxEngine::Scene& scene,
                                    std::string& error) {
    error.clear();
    try {
        ReferenceValidator validator(project);
        validator.ValidateMemory(scene);
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool ValidateProjectScenes(const EditorProject& project, std::string& error) {
    error.clear();
    try {
        const auto root = ReferenceValidator(project).root;
        LuxEngine::SceneManager manager;
        const auto validateGraph = [&](const fs::path& path) {
            // Limits apply to each composed graph, not the sum of independent authored scenes.
            ReferenceValidator graph(project);
            graph.ValidateFile(path);
            // The file walk already includes saved baselines and suspended snapshots. A second
            // memory walk would charge that same graph again after runtime composition.
            return static_cast<bool>(manager.Load(path, error));
        };
        if (!validateGraph(project.GetSceneFile()))
            return false;
        std::error_code errorCode;
        fs::recursive_directory_iterator entry(root, errorCode), end;
        if (errorCode)
            throw std::runtime_error("프로젝트의 씬 목록을 읽을 수 없습니다.");
        for (; entry != end; entry.increment(errorCode)) {
            if (errorCode)
                throw std::runtime_error("프로젝트의 씬 목록을 읽을 수 없습니다.");
            const auto relative = entry->path().lexically_relative(root);
            if (Ignored(relative)) {
                entry.disable_recursion_pending();
                continue;
            }
            if (entry->path().extension() != ".lux")
                continue;
            const bool directory = entry->is_directory(errorCode);
            if (errorCode)
                throw std::runtime_error("씬 파일 상태를 확인할 수 없습니다.");
            if (directory)
                continue;
            if (!validateGraph(entry->path()))
                return false;
        }
        if (errorCode)
            throw std::runtime_error("프로젝트의 씬 목록을 읽을 수 없습니다.");
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
} // namespace LuxEditor
