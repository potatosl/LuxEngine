#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneDependencies.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Environment/EnvironmentMap.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
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
        const auto& environment = scene.GetSettings().render3D.environmentPath;
        if (!environment.empty()) {
            const auto input = PathFromUtf8(environment);
            std::error_code ec;
            const auto file =
                fs::canonical(input.is_absolute() ? input : owner.parent_path() / input, ec);
            const auto relative = file.lexically_relative(root);
            if (ec || relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
                Ignored(relative))
                throw std::runtime_error("Environment image must be inside the project: " +
                                         environment);
            if (textureFiles_.insert(file).second) {
                Charge(fs::file_size(file));
                std::string error;
                if (!LuxEngine::EnvironmentData::ValidateFile(GenericPathUtf8(file), error))
                    throw std::runtime_error(error);
            }
        }
        for (const auto& entity : scene.GetEntities()) {
            if (const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>();
                mesh && !mesh->meshPath.empty()) {
                if (mesh->meshPath.size() > 4096 || mesh->meshPath.find('\0') != std::string::npos)
                    throw std::runtime_error("메시 경로가 올바르지 않습니다.");
                const auto input = PathFromUtf8(mesh->meshPath);
                std::error_code error;
                const auto file =
                    fs::canonical(input.is_absolute() ? input : owner.parent_path() / input, error);
                const auto relative = file.lexically_relative(root);
                if (error || relative.empty() || relative.is_absolute() ||
                    *relative.begin() == ".." || Ignored(relative))
                    throw std::runtime_error("메시 파일은 프로젝트 내부에 있어야 합니다: " +
                                             mesh->meshPath);
                if (meshFiles_.insert(file).second) {
                    const auto size = fs::file_size(file, error);
                    if (error)
                        throw std::runtime_error("메시 파일을 읽을 수 없습니다: " + mesh->meshPath);
                    Charge(size);
                    bool hasUvs = false;
                    if (!LuxEngine::AssetManager::ValidateMeshFile(GenericPathUtf8(file), &hasUvs))
                        throw std::runtime_error("OBJ 메시 파일이 올바르지 않습니다: " +
                                                 mesh->meshPath);
                    meshHasUvs_[file] = hasUvs;
                }
                if (!mesh->material.baseColorTexture.empty() && !meshHasUvs_.at(file))
                    throw std::runtime_error("텍스처를 적용할 UV 좌표가 없는 OBJ입니다: " +
                                             mesh->meshPath);
            }
            if (const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>();
                mesh && !mesh->material.baseColorTexture.empty()) {
                const auto& resource = mesh->material.baseColorTexture;
                if (!LuxEngine::AssetManager::IsBaseColorTextureFile(resource))
                    throw std::runtime_error("지원하지 않는 기본 색상 텍스처 형식입니다: " +
                                             resource);
                const auto input = PathFromUtf8(resource);
                std::error_code error;
                const auto file =
                    fs::canonical(input.is_absolute() ? input : owner.parent_path() / input, error);
                const auto relative = file.lexically_relative(root);
                if (error || relative.empty() || relative.is_absolute() ||
                    *relative.begin() == ".." || Ignored(relative))
                    throw std::runtime_error("기본 색상 텍스처는 프로젝트 내부에 있어야 합니다: " +
                                             resource);
                if (textureFiles_.insert(file).second) {
                    const auto size = fs::file_size(file, error);
                    if (error)
                        throw std::runtime_error("텍스처 파일을 읽을 수 없습니다: " + resource);
                    Charge(size);
                    int width = 0, height = 0;
                    std::vector<LuxEngine::uint8> pixels;
                    if (!LuxEngine::AssetManager::DecodeTextureCpu(GenericPathUtf8(file), width,
                                                                   height, pixels))
                        throw std::runtime_error("기본 색상 텍스처 파일이 올바르지 않습니다: " +
                                                 resource);
                }
            }
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
    std::set<fs::path> meshFiles_;
    std::map<fs::path, bool> meshHasUvs_;
    std::set<fs::path> textureFiles_;
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
