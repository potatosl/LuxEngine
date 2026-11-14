#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/PrefabLibrary.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace LuxEngine {
namespace {
namespace fs = std::filesystem;

std::string Utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path Path(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::unique_ptr<Scene> Clone(Scene& source) {
    std::string data;
    auto result = std::make_unique<Scene>();
    if (!source.SerializeToString(data) || !result->DeserializeFromString(data))
        throw std::runtime_error("프리팹 편집을 위한 씬 상태를 복사할 수 없습니다.");
    result->SetSourcePath(source.GetSourcePath());
    return result;
}

fs::path ValidatePath(const fs::path& requested, const Scene* owner = nullptr) {
    if (!PrefabLibrary::IsPrefabPath(requested) ||
        requested.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
        throw std::runtime_error("프리팹 파일은 .luxprefab 확장자를 사용해야 합니다.");
    auto path = requested;
    if (path.is_relative() && owner && !owner->GetSourcePath().empty())
        path = Path(owner->GetSourcePath()).parent_path() / path;
    std::error_code error;
    if (fs::is_symlink(path, error))
        throw std::runtime_error("프리팹 파일의 심볼릭 링크는 지원하지 않습니다.");
    const auto result = fs::weakly_canonical(fs::absolute(path), error);
    if (error)
        throw std::runtime_error("프리팹 파일 경로를 확인할 수 없습니다.");
    return result;
}

void ValidateAsset(const fs::path& path) {
    SceneManager reader;
    const auto loaded = reader.Load(path);
    if (!loaded)
        throw std::runtime_error(loaded.error);
    if (loaded.scene->GetChildren(0).size() != 1)
        throw std::runtime_error("프리팹은 최상위 오브젝트 하나를 포함해야 합니다.");
}

fs::path SourcePath(const Scene& scene, uint32 root) {
    const auto* reference = scene.TryGetComponent<SceneReferenceComponent>(root);
    if (!reference || !PrefabLibrary::IsPrefabPath(Path(reference->path)))
        throw std::runtime_error("프리팹 인스턴스를 선택해 주세요.");
    return ValidatePath(Path(reference->path), &scene);
}
} // namespace

bool PrefabLibrary::IsPrefabPath(const fs::path& path) {
    auto extension = Utf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".luxprefab";
}

bool PrefabLibrary::Save(Scene& source, uint32 root, const fs::path& requested,
                         std::string& error) {
    error.clear();
    try {
        if (!source.IsAlive(root))
            throw std::runtime_error("저장할 프리팹 오브젝트를 선택해 주세요.");
        const auto path = ValidatePath(requested);
        const auto instance = FindInstanceRoot(source, root);
        if (instance && SourcePath(source, instance) == path)
            throw std::runtime_error(
                "연결된 프리팹 원본은 인스턴스에서 덮어쓸 수 없습니다. 새 이름으로 저장해 주세요.");
        Scene asset;
        const auto mapping = asset.ImportEntities(source, {root}, 0, true);
        if (!mapping.contains(root))
            throw std::runtime_error("프리팹 계층 또는 컴포넌트를 복사할 수 없습니다.");
        const auto copiedRoot = mapping.at(root);
        if (const auto* reference = asset.TryGetComponent<SceneReferenceComponent>(copiedRoot);
            reference && IsPrefabPath(Path(reference->path))) {
            asset.RemoveComponent<SceneReferenceComponent>(copiedRoot);
            for (const auto id : asset.GetComponentEntityIds<InstanceSourceComponent>())
                if (asset.GetComponent<InstanceSourceComponent>(id).instanceRoot == copiedRoot)
                    asset.RemoveComponent<InstanceSourceComponent>(id);
        }
        asset.SetSourcePath(source.GetSourcePath());
        if (!SceneManager::Save(asset, path, Path(source.GetSourcePath()), error))
            return false;
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

uint32 PrefabLibrary::Instantiate(Scene& target, const fs::path& requested, std::string& error,
                                  uint32 parent) {
    error.clear();
    try {
        const auto path = ValidatePath(requested, &target);
        ValidateAsset(path);
        SceneManager manager;
        return manager.AddSceneInstance(target, path, SceneReferenceMode::Nested, error, parent);
    } catch (const std::exception& failure) {
        error = failure.what();
        return 0;
    }
}

uint32 PrefabLibrary::FindInstanceRoot(const Scene& scene, uint32 entity) {
    std::unordered_set<uint32> visited;
    while (entity && scene.IsAlive(entity) && visited.insert(entity).second) {
        const auto* reference = scene.TryGetComponent<SceneReferenceComponent>(entity);
        if (reference && IsPrefabPath(Path(reference->path)))
            return entity;
        entity = scene.GetParent(entity);
    }
    return 0;
}

bool PrefabLibrary::Refresh(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        ValidateAsset(SourcePath(target, root));
        SceneManager manager;
        return manager.RefreshInstance(target, root, error);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool PrefabLibrary::Revert(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        ValidateAsset(SourcePath(target, root));
        SceneManager manager;
        return manager.RevertInstance(target, root, error);
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}

bool PrefabLibrary::Unpack(Scene& target, uint32 root, std::string& error) {
    error.clear();
    try {
        SourcePath(target, root);
        auto staged = Clone(target);
        staged->RemoveComponent<SceneReferenceComponent>(root);
        for (const auto id : staged->GetComponentEntityIds<InstanceSourceComponent>())
            if (staged->GetComponent<InstanceSourceComponent>(id).instanceRoot == root)
                staged->RemoveComponent<InstanceSourceComponent>(id);
        if (!target.ApplyState(*staged))
            throw std::runtime_error("프리팹 연결을 해제할 수 없습니다.");
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
} // namespace LuxEngine
