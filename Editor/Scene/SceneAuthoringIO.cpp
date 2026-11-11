#include "SceneAuthoringIO.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Project/PathEncoding.hpp"
#include <stdexcept>

namespace LuxEditor {
namespace {
namespace fs = std::filesystem;

fs::path CanonicalPath(const fs::path& path) {
    if (path.empty() || path.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
        throw std::runtime_error("씬 파일 경로가 비어 있습니다.");
    std::error_code error;
    const auto absolute = fs::absolute(path, error);
    if (error)
        throw std::runtime_error("씬 파일의 절대 경로를 확인할 수 없습니다.");
    const auto canonical = fs::weakly_canonical(absolute, error);
    if (error)
        throw std::runtime_error("씬 파일 경로를 확인할 수 없습니다.");
    return canonical;
}

void RebaseSceneReferences(LuxEngine::Scene& scene, const fs::path& owner,
                           const fs::path* destination, std::uintmax_t& remaining,
                           size_t depth = 0) {
    if (depth > 16)
        throw std::runtime_error("씬 참조 스냅샷의 중첩 한도를 넘었습니다.");
    auto settings = scene.GetSettings();
    if (!settings.render3D.environmentPath.empty()) {
        auto path = PathFromUtf8(settings.render3D.environmentPath);
        if (path.is_relative())
            path = owner.parent_path() / path;
        const auto absolute = CanonicalPath(path);
        const auto relative =
            destination ? absolute.lexically_relative(destination->parent_path()) : fs::path{};
        settings.render3D.environmentPath =
            GenericPathUtf8(destination && !relative.empty() ? relative : absolute);
        if (!scene.SetSettings(settings))
            throw std::runtime_error("Invalid rebased environment path");
    }
    for (auto entity : scene.GetEntities()) {
        if (auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>())
            for (auto* resource : {&mesh->meshPath, &mesh->material.baseColorTexture}) {
                if (resource->empty())
                    continue;
                auto path = PathFromUtf8(*resource);
                if (path.is_relative())
                    path = owner.parent_path() / path;
                const auto absolute = CanonicalPath(path);
                const auto relative = destination
                                          ? absolute.lexically_relative(destination->parent_path())
                                          : fs::path{};
                *resource = GenericPathUtf8(destination && !relative.empty() ? relative : absolute);
            }
        auto* reference = entity.TryGetComponent<LuxEngine::SceneReferenceComponent>();
        if (!reference)
            continue;
        auto path = PathFromUtf8(reference->path);
        if (!path.is_absolute())
            path = owner.parent_path() / path;
        const auto absolute = CanonicalPath(path);
        if (destination) {
            const auto relative = absolute.lexically_relative(destination->parent_path());
            reference->path = GenericPathUtf8(relative.empty() ? absolute : relative);
        } else
            reference->path = GenericPathUtf8(absolute);
        for (std::string* data : {&reference->baseline, &reference->suspended}) {
            if (data->empty())
                continue;
            if (data->size() > remaining)
                throw std::runtime_error("씬 참조 스냅샷이 128 MiB 한도를 넘었습니다.");
            remaining -= data->size();
            LuxEngine::Scene snapshot;
            if (!snapshot.DeserializeFromString(*data))
                throw std::runtime_error("저장된 씬 참조 상태가 손상되었습니다.");
            RebaseSceneReferences(snapshot, owner, destination, remaining, depth + 1);
            if (!snapshot.SerializeToString(*data))
                throw std::runtime_error("씬 참조 경로를 저장할 수 없습니다.");
        }
    }
}

std::shared_ptr<LuxEngine::Scene> CloneAuthoredScene(LuxEngine::Scene& scene) {
    std::string data;
    auto copy = std::make_shared<LuxEngine::Scene>();
    if (!scene.SerializeToString(data) || !copy->DeserializeFromString(data))
        throw std::runtime_error("씬 상태를 복사할 수 없습니다.");
    return copy;
}
} // namespace

std::shared_ptr<LuxEngine::Scene> ReadAuthoredScene(const fs::path& path, std::string& error) {
    error.clear();
    try {
        const auto origin = CanonicalPath(path);
        auto authored = std::make_shared<LuxEngine::Scene>();
        if (!authored->Deserialize(GenericPathUtf8(origin))) {
            error = "씬 파일 형식이 올바르지 않거나 파일을 읽을 수 없습니다.";
            return {};
        }
        try {
            auto normalized = CloneAuthoredScene(*authored);
            std::uintmax_t remaining = 128 * 1024 * 1024;
            RebaseSceneReferences(*normalized, origin, nullptr, remaining);
            authored = std::move(normalized);
        } catch (const std::exception&) {
            // Keep damaged embedded snapshots intact so authored data remains available to repair.
        }
        authored->SetSourcePath(GenericPathUtf8(origin));
        return authored;
    } catch (const std::exception& failure) {
        error = failure.what();
        return {};
    }
}

bool SaveAuthoredScene(LuxEngine::Scene& scene, const fs::path& destination,
                       const fs::path& previous, std::string& error) {
    error.clear();
    try {
        auto copy = CloneAuthoredScene(scene);
        const auto target = CanonicalPath(destination);
        const auto origin = previous.empty() ? PathFromUtf8(scene.GetSourcePath()) : previous;
        const auto owner = origin.empty() ? target : CanonicalPath(origin);
        std::uintmax_t remaining = 128 * 1024 * 1024;
        RebaseSceneReferences(*copy, owner, &target, remaining);
        if (copy->Serialize(GenericPathUtf8(target)))
            return true;
        error = LuxEngine::Scene::GetLastSaveError();
        if (error.empty())
            error = "씬 파일을 저장할 수 없습니다.";
    } catch (const std::exception& failure) {
        error = failure.what();
    }
    return false;
}
} // namespace LuxEditor
