#include "PhysicsWorld2D.hpp"
#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Physics/PhysicsEngine.hpp>
#include <LuxEngine/Renderer/Renderer2D/Renderer2D.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace LuxEngine {
Scene::Scene() = default;
Scene::~Scene() = default;
Entity Scene::CreateEntity(const std::string& name) {
    if (nextEntityId_ == std::numeric_limits<uint32>::max())
        throw std::overflow_error("Entity IDs exhausted");
    const auto id = nextEntityId_++;
    entities_.insert(id);
    try {
        Entity entity{id, this};
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    } catch (...) {
        for (auto& [type, pool] : pools_)
            pool->Erase(id);
        entities_.erase(id);
        throw;
    }
}
void Scene::DestroyEntity(Entity entity) {
    if (entity.GetScene() != this || !entity.IsValid())
        return;
    std::unordered_map<uint32, std::vector<uint32>> children;
    for (const uint32 id : entities_)
        children[GetParent(id)].push_back(id);
    std::vector<uint32> pending{entity.GetId()};
    std::unordered_set<uint32> removed;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!removed.insert(id).second)
            continue;
        const auto found = children.find(id);
        if (found != children.end())
            pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    for (const auto id : removed) {
        RemovePhysicsBody2D(id);
        for (auto& [type, pool] : pools_)
            pool->Erase(id);
        entities_.erase(id);
    }
    RepairReferenceLinks();
}
std::vector<Entity> Scene::GetEntities() {
    std::vector<uint32> ids(entities_.begin(), entities_.end());
    std::sort(ids.begin(), ids.end());
    std::vector<Entity> result;
    result.reserve(ids.size());
    for (auto id : ids)
        result.emplace_back(id, this);
    return result;
}
std::vector<Entity> Scene::GetEntitiesIn2DLayerOrder() {
    auto entities = GetEntities();
    std::erase_if(entities, [](Entity entity) {
        const auto* transform = entity.TryGetComponent<TransformComponent>();
        return !transform || !std::isfinite(transform->position.z);
    });
    std::stable_sort(entities.begin(), entities.end(), [this](Entity a, Entity b) {
        return GetWorldTransform(a.GetId()).position.z < GetWorldTransform(b.GetId()).position.z;
    });
    return entities;
}
void Scene::OnUpdate(double dt) {
    if (!std::isfinite(dt) || dt <= 0.0)
        return;
    updateBuffer_.assign(entities_.begin(), entities_.end());
    for (auto id : updateBuffer_) {
        if (!entities_.contains(id))
            continue;
        if (!HasComponent<AnimationComponent>(id) || !HasComponent<TransformComponent>(id))
            continue;
        auto& animation = GetComponent<AnimationComponent>(id);
        if (animation.playing && !animation.clip.keyframes.empty())
            AnimationEngine::UpdateClip(animation.clip, animation.currentTime,
                                        static_cast<float>(dt),
                                        GetComponent<TransformComponent>(id));
    }
    if (!physics2D_)
        physics2D_ = std::make_unique<PhysicsWorld2D>();
    collisionEvents2D_.clear();
    physicsAccumulator_ += std::min(dt, .25);
    const double fixedStep = settings_.fixedStep;
    while (physicsAccumulator_ + 1e-8 >= fixedStep) {
        physicsAccumulator_ = std::max(0.0, physicsAccumulator_ - fixedStep);
        const double substep = fixedStep / settings_.physicsSubsteps;
        for (uint32 index = 0; index < settings_.physicsSubsteps; ++index) {
            std::unordered_map<uint32, TransformComponent> world3D;
            std::unordered_map<uint32, Rigidbody3DComponent> bodies3D;
            const auto hierarchy = GetEntitiesInHierarchyOrder();
            for (const auto& entity : hierarchy) {
                const auto id = entity.GetId();
                if (!HasComponent<TransformComponent>(id) ||
                    (HasComponent<Rigidbody2DComponent>(id) &&
                     HasComponent<BoxCollider2DComponent>(id)) ||
                    !HasComponent<Rigidbody3DComponent>(id) ||
                    !HasComponent<BoxCollider3DComponent>(id))
                    continue;
                auto world = GetWorldTransform(id);
                auto body = GetComponent<Rigidbody3DComponent>(id);
                PhysicsEngine::Step3D(world.position, body,
                                      GetComponent<BoxCollider3DComponent>(id), substep,
                                      settings_.overrideGravity ? settings_.gravity3D
                                                                : PhysicsEngine::GetGravity3D());
                world3D.emplace(id, world);
                bodies3D.emplace(id, body);
            }
            std::vector<Collision2DEvent> contacts;
            physics2D_->Step(*this, substep, contacts);
            collisionEvents2D_.insert(collisionEvents2D_.end(), contacts.begin(), contacts.end());
            const auto world2D = physics2D_->GetWorldPoses(*this);
            // Apply every physics family together, parent before child. A hierarchy may contain
            // both 2D and 3D bodies; each simulation's absolute world pose must remain independent.
            for (const auto& entity : hierarchy) {
                const auto id = entity.GetId();
                if (const auto pose = world3D.find(id); pose != world3D.end()) {
                    if (SetWorldTransform(id, pose->second))
                        GetComponent<Rigidbody3DComponent>(id) = bodies3D.at(id);
                } else if (const auto pose2D = world2D.find(id); pose2D != world2D.end()) {
                    SetWorldTransform(id, pose2D->second);
                }
            }
        }
    }
}
void Scene::RemovePhysicsBody2D(uint32 id) {
    if (physics2D_)
        physics2D_->Remove(id);
    if (auto* body = TryGetComponent<Rigidbody2DComponent>(id))
        body->isGrounded = false;
    std::erase_if(collisionEvents2D_,
                  [id](const auto& event) { return event.entityA == id || event.entityB == id; });
}
namespace {
Vec4 TileColor(uint8_t tile) {
    switch (tile) {
    case 1:
        return {0.36f, 0.65f, 0.27f, 1.0f};
    case 2:
        return {0.43f, 0.31f, 0.21f, 1.0f};
    case 3:
        return {0.48f, 0.51f, 0.54f, 1.0f};
    case 4:
        return {0.22f, 0.46f, 0.74f, 0.82f};
    case 5:
        return {0.76f, 0.66f, 0.42f, 1.0f};
    default:
        return {0, 0, 0, 0};
    }
}
bool Finite(float v) {
    return std::isfinite(v);
}
bool Finite(const Vec2& v) {
    return Finite(v.x) && Finite(v.y);
}
bool Finite(const Vec3& v) {
    return Finite(v.x) && Finite(v.y) && Finite(v.z);
}
bool Finite(const Vec4& v) {
    return Finite(v.x) && Finite(v.y) && Finite(v.z) && Finite(v.w);
}
Vec4 Exposed(Vec4 color, float exposure) {
    color.x *= exposure;
    color.y *= exposure;
    color.z *= exposure;
    return color;
}
} // namespace
void Scene::OnRender() {
    // 개발 중: 타일 종류는 고정 색상으로 표시합니다. 텍스처 아틀라스는 아직 연결되지 않았습니다.
    const auto entities = GetEntitiesIn2DLayerOrder();
    for (const bool tilemaps : {true, false}) {
        for (Entity entity : entities) {
            if (!entity.HasComponent<TransformComponent>())
                continue;
            const auto transform = GetWorldTransform(entity.GetId());
            const auto matrix = GetWorldMatrix(entity.GetId());
            if (!Finite(transform.position) || !Finite(transform.rotation) ||
                !Finite(transform.scale))
                continue;
            if (tilemaps && entity.HasComponent<TilemapComponent>()) {
                const auto& map = entity.GetComponent<TilemapComponent>();
                if (map.width == 0 || map.width > 256 || map.height == 0 || map.height > 256 ||
                    !std::isfinite(map.tileSize) || map.tileSize <= 0.0f || map.tileSize > 64.0f ||
                    map.tiles.size() != static_cast<size_t>(map.width) * map.height)
                    continue;
                for (uint32 y = 0; y < map.height; ++y)
                    for (uint32 x = 0; x < map.width; ++x) {
                        const uint8_t tile = map.Get(x, y);
                        if (tile == 0)
                            continue;
                        const float size = map.tileSize;
                        Renderer2D::DrawQuad(
                            matrix * Mat4::Translate({(x + .5f) * size, (y + .5f) * size, 0}) *
                                Mat4::Scale({size, size, 1}),
                            Exposed(TileColor(tile), settings_.exposure));
                    }
            }
            if (!tilemaps && entity.HasComponent<SpriteRendererComponent>()) {
                const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                if (Finite(sprite.color))
                    Renderer2D::DrawQuad(matrix, Exposed(sprite.color, settings_.exposure));
            }
        }
    }
}
void Scene::OnRender3D() {
    for (Entity entity : GetEntities()) {
        if (!entity.HasComponent<TransformComponent>() ||
            !entity.HasComponent<MeshRendererComponent>())
            continue;
        const auto transform = GetWorldTransform(entity.GetId());
        if (!Finite(transform.position) || !Finite(transform.rotation) || !Finite(transform.scale))
            continue;
        const auto& meshComp = entity.GetComponent<MeshRendererComponent>();
        if (Finite(meshComp.material.albedoColor) && Finite(meshComp.material.metallic) &&
            Finite(meshComp.material.roughness)) {
            auto material = meshComp.material;
            if (settings_.render3D.mode == Render3DMode::Legacy)
                material.albedoColor = Exposed(material.albedoColor, settings_.exposure);
            if (!material.baseColorTexture.empty())
                material.baseColorTexture = ResolveAssetPath(material.baseColorTexture);
            if (meshComp.meshPath.empty())
                Renderer3D::DrawMesh(GetWorldMatrix(entity.GetId()), material);
            else
                Renderer3D::DrawMeshAsset(ResolveAssetPath(meshComp.meshPath),
                                          GetWorldMatrix(entity.GetId()), material);
        }
    }
}
namespace {
std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
} // namespace

std::string Scene::ResolveAssetPath(const std::string& path) const {
    if (path.empty() || path.find('\0') != std::string::npos)
        return {};
    try {
        auto resolved = Utf8Path(path);
        if (resolved.is_relative() && !sourcePath_.empty())
            resolved = Utf8Path(sourcePath_).parent_path() / resolved;
        const auto bytes = std::filesystem::absolute(resolved).lexically_normal().u8string();
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    } catch (const std::exception&) {
        return {};
    }
}

namespace {

std::string PathToUtf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::mutex s_SaveErrorMutex;
std::string s_LastSaveError;
std::mutex s_SaveFaultMutex;
Scene::SaveFault s_InjectedSaveFault = Scene::SaveFault::None;
uint32 s_InjectedSaveFaultCount = 0;
std::atomic<uint64> s_SaveSequence{0};
std::mutex s_FileReplaceMutex;

void SetSaveError(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(s_SaveErrorMutex);
        s_LastSaveError = message;
    }
    if (!message.empty())
        Log::Error(message);
}

void ClearSaveError() {
    std::lock_guard<std::mutex> lock(s_SaveErrorMutex);
    s_LastSaveError.clear();
}

bool ConsumeSaveFault(Scene::SaveFault expected) {
    std::lock_guard<std::mutex> lock(s_SaveFaultMutex);
    if (s_InjectedSaveFault == expected && s_InjectedSaveFaultCount > 0) {
        --s_InjectedSaveFaultCount;
        if (s_InjectedSaveFaultCount == 0)
            s_InjectedSaveFault = Scene::SaveFault::None;
        return true;
    }
    return false;
}

bool WriteTempFileFlushed(const std::filesystem::path& tempPath, const std::string& data) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (ConsumeSaveFault(Scene::SaveFault::TempWriteFailure)) {
        SetSaveError("Scene save failed: injected temporary file write failure (Win32 error 5: "
                     "ERROR_ACCESS_DENIED) for " +
                     PathToUtf8(tempPath));
        return false;
    }
#ifdef _WIN32
    const std::wstring tempW = tempPath.wstring();
    HANDLE handle = CreateFileW(tempW.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        SetSaveError("Scene save failed: could not create temporary file '" + PathToUtf8(tempPath) +
                     "' (Win32 error " + std::to_string(err) + ")");
        return false;
    }
    if (ConsumeSaveFault(Scene::SaveFault::DiskFull)) {
        CloseHandle(handle);
        DeleteFileW(tempW.c_str());
        SetSaveError("Scene save failed: disk full while writing temporary file '" +
                     PathToUtf8(tempPath) + "' (Win32 error 112: ERROR_DISK_FULL)");
        return false;
    }
    const char* ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, ptr, chunk, &written, nullptr) || written != chunk) {
            const DWORD err = GetLastError();
            CloseHandle(handle);
            DeleteFileW(tempW.c_str());
            SetSaveError("Scene save failed: WriteFile failed on temporary file '" +
                         PathToUtf8(tempPath) + "' (Win32 error " + std::to_string(err) + ")");
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(handle)) {
        const DWORD err = GetLastError();
        CloseHandle(handle);
        DeleteFileW(tempW.c_str());
        SetSaveError("Scene save failed: FlushFileBuffers failed on temporary file '" +
                     PathToUtf8(tempPath) + "' (Win32 error " + std::to_string(err) + ")");
        return false;
    }
    if (!CloseHandle(handle)) {
        const DWORD err = GetLastError();
        DeleteFileW(tempW.c_str());
        SetSaveError("Scene save failed: CloseHandle failed on temporary file '" +
                     PathToUtf8(tempPath) + "' (Win32 error " + std::to_string(err) + ")");
        return false;
    }
    return true;
#else
    if (ConsumeSaveFault(Scene::SaveFault::DiskFull)) {
        SetSaveError("Scene save failed: disk full while writing temporary file '" +
                     PathToUtf8(tempPath) + "'");
        return false;
    }
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        SetSaveError("Scene save failed: could not open temporary file '" + PathToUtf8(tempPath) +
                     "'");
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
        out.close();
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: write/flush error on temporary file '" +
                     PathToUtf8(tempPath) + "'");
        return false;
    }
    out.close();
    if (!out) {
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: close error on temporary file '" + PathToUtf8(tempPath) +
                     "'");
        return false;
    }
    return true;
#endif
}

bool AtomicReplaceFile(const std::filesystem::path& tempPath,
                       const std::filesystem::path& targetPath, [[maybe_unused]] uint64 seq) {
    namespace fs = std::filesystem;
    std::error_code ec;

#ifdef _WIN32
    const std::wstring targetW = targetPath.wstring();
    const std::wstring tempW = tempPath.wstring();
    const DWORD initialTargetAttrs = GetFileAttributesW(targetW.c_str());
    const bool targetExistedInitially = (initialTargetAttrs != INVALID_FILE_ATTRIBUTES);

    if (!targetExistedInitially) {
        const DWORD attrErr = GetLastError();
        if (attrErr != ERROR_FILE_NOT_FOUND) {
            DeleteFileW(tempW.c_str());
            SetSaveError("Scene save failed: cannot access target path '" + PathToUtf8(targetPath) +
                         "' (Win32 error " + std::to_string(attrErr) + ")");
            return false;
        }
        if (ConsumeSaveFault(Scene::SaveFault::ReplaceAccessDenied)) {
            DeleteFileW(tempW.c_str());
            SetSaveError("Scene save failed: access denied moving temporary file to '" +
                         PathToUtf8(targetPath) + "' (Win32 error 5: ERROR_ACCESS_DENIED)");
            return false;
        }
        if (MoveFileExW(tempW.c_str(), targetW.c_str(), MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        const DWORD moveErr = GetLastError();
        if (moveErr != ERROR_ALREADY_EXISTS && moveErr != ERROR_FILE_EXISTS) {
            DeleteFileW(tempW.c_str());
            SetSaveError("Scene save failed: MoveFileExW to new target '" + PathToUtf8(targetPath) +
                         "' failed (Win32 error " + std::to_string(moveErr) + ")");
            return false;
        }
        // If another writer created targetPath concurrently, fall through to atomic ReplaceFileW
        // with backup.
    }

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::wstring backupW = targetW + L".lux-bak-" + std::to_wstring(timestamp) + L"-" +
                                 std::to_wstring(threadId) + L"-" + std::to_wstring(seq);
    const fs::path backupPath(backupW);

    bool replaceSucceeded = false;
    DWORD replaceErr = ERROR_SUCCESS;
    bool injectRestoreFailure = false;

    if (ConsumeSaveFault(Scene::SaveFault::ReplaceAccessDenied)) {
        replaceSucceeded = false;
        replaceErr = ERROR_ACCESS_DENIED;
    } else if (ConsumeSaveFault(Scene::SaveFault::ReplaceFailedAfterBackup)) {
        // Simulate step 1 of ReplaceFileW succeeding (target moved to backup) and step 2 failing.
        if (MoveFileExW(targetW.c_str(), backupW.c_str(), MOVEFILE_WRITE_THROUGH)) {
            replaceSucceeded = false;
            replaceErr = ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        } else {
            replaceSucceeded = false;
            replaceErr = GetLastError();
        }
    } else if (ConsumeSaveFault(Scene::SaveFault::BackupRestoreFailure)) {
        // Simulate target moved to backup, replacement failing, AND backup restoration failing.
        if (MoveFileExW(targetW.c_str(), backupW.c_str(), MOVEFILE_WRITE_THROUGH)) {
            replaceSucceeded = false;
            replaceErr = ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
            injectRestoreFailure = true;
        } else {
            replaceSucceeded = false;
            replaceErr = GetLastError();
        }
    } else {
        if (ReplaceFileW(targetW.c_str(), tempW.c_str(), backupW.c_str(),
                         REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr,
                         nullptr)) {
            replaceSucceeded = true;
        } else {
            replaceErr = GetLastError();
        }
    }

    if (replaceSucceeded) {
        if (GetFileAttributesW(targetW.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(backupW.c_str());
            return true;
        }
        replaceErr = ERROR_FILE_NOT_FOUND;
    }

    // Replacement failed. Absolute priority: preserve the original file and never clobber it with
    // tempW.
    const bool backupExists = (GetFileAttributesW(backupW.c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!backupExists) {
        // Original target file was never renamed to backup and remains intact.
        DeleteFileW(tempW.c_str());
        SetSaveError("Scene save failed: ReplaceFileW failed before backup replacement for '" +
                     PathToUtf8(targetPath) + "' (Win32 error " + std::to_string(replaceErr) +
                     "). Original file preserved.");
        return false;
    }

    // Backup exists: the original file was moved to backupW. Attempt safe restoration of backupW ->
    // targetW.
    bool restored = false;
    DWORD restoreErr = ERROR_SUCCESS;
    if (injectRestoreFailure) {
        restored = false;
        restoreErr = ERROR_ACCESS_DENIED;
    } else {
        const bool targetCurrentlyExists =
            (GetFileAttributesW(targetW.c_str()) != INVALID_FILE_ATTRIBUTES);
        if (!targetCurrentlyExists) {
            if (MoveFileExW(backupW.c_str(), targetW.c_str(), MOVEFILE_WRITE_THROUGH)) {
                restored = true;
            } else {
                restoreErr = GetLastError();
            }
        } else {
            if (ReplaceFileW(targetW.c_str(), backupW.c_str(), nullptr,
                             REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                             nullptr, nullptr)) {
                restored = true;
            } else {
                restoreErr = GetLastError();
            }
        }
    }

    // Clean up temporary file so no partial/uncommitted replacement file lingers.
    DeleteFileW(tempW.c_str());

    if (restored && GetFileAttributesW(targetW.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // Confirm targetW has non-zero size before removing any lingering backupW.
        const auto targetSize = fs::file_size(targetPath, ec);
        if (!ec && targetSize > 0) {
            if (GetFileAttributesW(backupW.c_str()) != INVALID_FILE_ATTRIBUTES)
                DeleteFileW(backupW.c_str());
        }
        SetSaveError("Scene save failed for '" + PathToUtf8(targetPath) + "' (Win32 error " +
                     std::to_string(replaceErr) +
                     "); original file was safely restored from backup.");
        return false;
    }

    // Backup restoration failed! NEVER delete backupW — it holds the user's original scene data.
    SetSaveError("CRITICAL: Scene save failed for '" + PathToUtf8(targetPath) + "' (Win32 error " +
                 std::to_string(replaceErr) + ") and backup restoration failed (Win32 error " +
                 std::to_string(restoreErr) +
                 "). Original scene preserved at backup path: " + PathToUtf8(backupPath));
    return false;
#else
    if (ConsumeSaveFault(Scene::SaveFault::ReplaceAccessDenied)) {
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: access denied replacing '" + PathToUtf8(targetPath) + "'");
        return false;
    }
    fs::rename(tempPath, targetPath, ec);
    if (ec) {
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: rename failed for '" + PathToUtf8(targetPath) + "'");
        return false;
    }
    return true;
#endif
}

std::ostream& operator<<(std::ostream& o, const Vec2& v) {
    return o << v.x << ' ' << v.y << ' ';
}
std::ostream& operator<<(std::ostream& o, const Vec3& v) {
    return o << v.x << ' ' << v.y << ' ' << v.z << ' ';
}
std::ostream& operator<<(std::ostream& o, const Vec4& v) {
    return o << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w << ' ';
}
std::istream& operator>>(std::istream& i, Vec2& v) {
    return i >> v.x >> v.y;
}
std::istream& operator>>(std::istream& i, Vec3& v) {
    return i >> v.x >> v.y >> v.z;
}
std::istream& operator>>(std::istream& i, Vec4& v) {
    return i >> v.x >> v.y >> v.z >> v.w;
}
} // namespace
namespace {
constexpr uint64 MaxSceneSizeBytes = 64ull * 1024ull * 1024ull; // 64 MiB
constexpr uint64 MaxSceneEntities = 100000;                     // 100,000 entities
constexpr size_t MaxTagStringLength = 256;                      // 256 bytes per tag
constexpr size_t MaxClipNameLength = 256;                       // 256 bytes per animation clip name

bool IsValidUtf8(std::string_view str) {
    size_t i = 0;
    while (i < str.size()) {
        const auto c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            if ((c1 & 0xC0) != 0x80 || c < 0xC2)
                return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            const auto c2 = static_cast<unsigned char>(str[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
                return false;
            if (c == 0xE0 && c1 < 0xA0)
                return false;
            if (c == 0xED && c1 >= 0xA0)
                return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4 || i + 3 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            const auto c2 = static_cast<unsigned char>(str[i + 2]);
            const auto c3 = static_cast<unsigned char>(str[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
                return false;
            if (c == 0xF0 && c1 < 0x90)
                return false;
            if (c == 0xF4 && c1 > 0x8F)
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool ValidSceneMetadata(Scene& scene) {
    if (!scene.ValidateHierarchy())
        return false;
    const auto entities = scene.GetEntities();
    std::unordered_map<uint32, std::vector<uint32>> children;
    for (const auto& entity : entities)
        children[scene.GetParent(entity.GetId())].push_back(entity.GetId());
    // Pre/post indices make ancestry validation linear even for a very deep hierarchy.
    std::unordered_map<uint32, std::pair<uint32, uint32>> intervals;
    std::vector<std::pair<uint32, bool>> pending;
    for (const auto root : children[0])
        pending.emplace_back(root, false);
    uint32 sequence = 0;
    while (!pending.empty()) {
        const auto [id, leaving] = pending.back();
        pending.pop_back();
        if (leaving) {
            intervals[id].second = ++sequence;
            continue;
        }
        intervals[id].first = ++sequence;
        pending.emplace_back(id, true);
        for (const auto child : children[id])
            pending.emplace_back(child, false);
    }
    const auto descendant = [&](uint32 child, uint32 parent) {
        if (child == parent || !intervals.contains(child) || !intervals.contains(parent))
            return false;
        return intervals.at(parent).first < intervals.at(child).first &&
               intervals.at(child).second < intervals.at(parent).second;
    };
    uint64 referenceBytes = 0;
    for (const auto& entity : entities) {
        if (const auto* reference = entity.TryGetComponent<SceneReferenceComponent>()) {
            const auto mode = static_cast<int>(reference->mode);
            referenceBytes +=
                reference->path.size() + reference->baseline.size() + reference->suspended.size();
            if (reference->path.empty() || reference->path.size() > 4096 ||
                reference->path.find('\0') != std::string::npos || !IsValidUtf8(reference->path) ||
                !IsValidUtf8(reference->baseline) || !IsValidUtf8(reference->suspended) ||
                referenceBytes > MaxSceneSizeBytes || mode < 0 || mode > 2 ||
                !Finite(reference->streamCenter) || !Finite(reference->streamLoadRadius) ||
                !Finite(reference->streamUnloadRadius) || reference->streamLoadRadius < 0 ||
                reference->streamUnloadRadius < reference->streamLoadRadius ||
                reference->entityMap.size() > MaxSceneEntities)
                return false;
            std::unordered_set<uint32> sources, locals;
            for (const auto& [source, local] : reference->entityMap)
                if (!source || !descendant(local, entity.GetId()) ||
                    !sources.insert(source).second || !locals.insert(local).second)
                    return false;
        }
        if (const auto* source = entity.TryGetComponent<InstanceSourceComponent>())
            if (!source->sourceEntity || !descendant(entity.GetId(), source->instanceRoot) ||
                !scene.HasComponent<SceneReferenceComponent>(source->instanceRoot))
                return false;
    }
    return true;
}
} // namespace

bool Scene::SerializeToString(std::string& output) {
    if (entities_.size() > MaxSceneEntities || !ValidSceneMetadata(*this))
        return false;
    const std::unordered_set<std::type_index> supported{typeid(TagComponent),
                                                        typeid(TransformComponent),
                                                        typeid(SpriteRendererComponent),
                                                        typeid(MeshRendererComponent),
                                                        typeid(CameraComponent),
                                                        typeid(Rigidbody2DComponent),
                                                        typeid(BoxCollider2DComponent),
                                                        typeid(Rigidbody3DComponent),
                                                        typeid(BoxCollider3DComponent),
                                                        typeid(TilemapComponent),
                                                        typeid(AnimationComponent),
                                                        typeid(HierarchyComponent),
                                                        typeid(SceneReferenceComponent),
                                                        typeid(InstanceSourceComponent)};
    for (const auto& [type, pool] : pools_)
        if (!pool->Empty() && !supported.contains(type))
            return false;
    for (const uint32 id : entities_) {
        if (const auto* tag = TryGetComponent<TagComponent>(id))
            if (tag->tag.size() > MaxTagStringLength || !IsValidUtf8(tag->tag))
                return false;
        if (const auto* transform = TryGetComponent<TransformComponent>(id)) {
            if (!Finite(transform->position) || !Finite(transform->rotation) ||
                !Finite(transform->scale))
                return false;
        }
        if (const auto* sprite = TryGetComponent<SpriteRendererComponent>(id)) {
            if (!Finite(sprite->color))
                return false;
        }
        if (const auto* mesh = TryGetComponent<MeshRendererComponent>(id)) {
            if (!Finite(mesh->material.albedoColor) || !Finite(mesh->material.metallic) ||
                !Finite(mesh->material.roughness) || mesh->meshPath.size() > 4096 ||
                mesh->meshPath.find('\0') != std::string::npos || !IsValidUtf8(mesh->meshPath) ||
                mesh->material.baseColorTexture.size() > 4096 ||
                mesh->material.baseColorTexture.find('\0') != std::string::npos ||
                !IsValidUtf8(mesh->material.baseColorTexture))
                return false;
        }
        if (const auto* camera = TryGetComponent<CameraComponent>(id)) {
            if (!Finite(camera->fov) || camera->fov <= 0.0f || camera->fov >= 180.0f ||
                !Finite(camera->orthoSize) || camera->orthoSize <= 0.0f)
                return false;
        }
        if (const auto* rb2d = TryGetComponent<Rigidbody2DComponent>(id)) {
            const int type = static_cast<int>(rb2d->type);
            if (type < 0 || type > 2 || !Finite(rb2d->velocity) || !Finite(rb2d->mass) ||
                rb2d->mass <= 0.0f || !Finite(rb2d->gravityScale))
                return false;
        }
        if (const auto* col2d = TryGetComponent<BoxCollider2DComponent>(id)) {
            if (!Finite(col2d->size) || col2d->size.x <= 0.0f || col2d->size.y <= 0.0f ||
                !Finite(col2d->offset))
                return false;
        }
        if (const auto* rb3d = TryGetComponent<Rigidbody3DComponent>(id)) {
            const int type = static_cast<int>(rb3d->type);
            if (type < 0 || type > 2 || !Finite(rb3d->velocity) || !Finite(rb3d->mass) ||
                rb3d->mass <= 0.0f || !Finite(rb3d->gravityScale))
                return false;
        }
        if (const auto* col3d = TryGetComponent<BoxCollider3DComponent>(id)) {
            if (!Finite(col3d->size) || col3d->size.x <= 0.0f || col3d->size.y <= 0.0f ||
                col3d->size.z <= 0.0f || !Finite(col3d->offset))
                return false;
        }
        if (const auto* map = TryGetComponent<TilemapComponent>(id)) {
            if (map->width == 0 || map->width > 256 || map->height == 0 || map->height > 256 ||
                !std::isfinite(map->tileSize) || map->tileSize <= 0 || map->tileSize > 64 ||
                map->tiles.size() != static_cast<size_t>(map->width) * map->height ||
                std::any_of(map->tiles.begin(), map->tiles.end(),
                            [](uint8_t tile) { return tile > 5; }))
                return false;
        }
        if (const auto* animation = TryGetComponent<AnimationComponent>(id)) {
            if (animation->clip.name.size() > MaxClipNameLength ||
                !IsValidUtf8(animation->clip.name) || !std::isfinite(animation->clip.duration) ||
                animation->clip.duration <= 0 || animation->clip.keyframes.size() > 4096)
                return false;
            for (const auto& key : animation->clip.keyframes)
                if (!std::isfinite(key.time) || key.time < 0 || !Finite(key.position) ||
                    !Finite(key.rotation) || !Finite(key.scale))
                    return false;
        }
    }
    const bool meshAssets = std::any_of(entities_.begin(), entities_.end(), [this](uint32 id) {
        const auto* mesh = TryGetComponent<MeshRendererComponent>(id);
        return mesh && (!mesh->meshPath.empty() || !mesh->material.baseColorTexture.empty());
    });
    const bool textureAssets = std::any_of(entities_.begin(), entities_.end(), [this](uint32 id) {
        const auto* mesh = TryGetComponent<MeshRendererComponent>(id);
        return mesh && !mesh->material.baseColorTexture.empty();
    });
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << "LuxEngineScene "
        << (!IsDefaultRender3DSettings(settings_.render3D) ? 6
            : textureAssets                                ? 5
            : meshAssets                                   ? 4
                                                           : 3)
        << ' ' << entities_.size() << '\n';
    out << "Settings " << settings_.clearColor << settings_.exposure << ' ' << settings_.gravity
        << settings_.gravity3D << settings_.overrideGravity << ' ' << settings_.fixedStep << ' '
        << settings_.physicsSubsteps << '\n';
    if (!IsDefaultRender3DSettings(settings_.render3D)) {
        const auto& r = settings_.render3D;
        out << "Render3D " << static_cast<int>(r.mode) << ' ' << r.sun.direction << r.sun.color
            << r.sun.intensity << ' ' << r.environmentColor << r.environmentIntensity << ' '
            << r.environmentRotation << ' ' << std::quoted(r.environmentPath) << ' '
            << r.imageBasedLighting << ' ' << r.showSkybox << ' ' << r.toneMapping << ' '
            << r.shadows << ' ' << r.shadowResolution << ' ' << r.shadowRange << ' '
            << r.shadowDepthBias << ' ' << r.shadowNormalBias << '\n';
    }
    std::vector<uint32> ids(entities_.begin(), entities_.end());
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
        out << id << '\n';
        auto save = [&]<class T>(auto write) {
            const bool present = HasComponent<T>(id);
            out << present << ' ';
            if (present)
                write(GetComponent<T>(id));
            out << '\n';
        };
        save.operator()<TagComponent>([&](const auto& c) { out << std::quoted(c.tag); });
        save.operator()<TransformComponent>(
            [&](const auto& c) { out << c.position << c.rotation << c.scale; });
        save.operator()<SpriteRendererComponent>([&](const auto& c) { out << c.color; });
        save.operator()<MeshRendererComponent>([&](const auto& c) {
            out << c.material.albedoColor << c.material.metallic << ' ' << c.material.roughness;
            if (meshAssets || !IsDefaultRender3DSettings(settings_.render3D))
                out << ' ' << std::quoted(c.meshPath);
            if (textureAssets || !IsDefaultRender3DSettings(settings_.render3D))
                out << ' ' << std::quoted(c.material.baseColorTexture);
        });
        save.operator()<CameraComponent>([&](const auto& c) {
            out << c.isPrimary << ' ' << c.isPerspective << ' ' << c.fov << ' ' << c.orthoSize;
        });
        save.operator()<Rigidbody2DComponent>([&](const auto& c) {
            out << static_cast<int>(c.type) << ' ' << c.velocity << c.mass << ' ' << c.gravityScale
                << ' ' << c.isGrounded;
        });
        save.operator()<BoxCollider2DComponent>(
            [&](const auto& c) { out << c.size << c.offset << c.isTrigger; });
        save.operator()<Rigidbody3DComponent>([&](const auto& c) {
            out << static_cast<int>(c.type) << ' ' << c.velocity << c.mass << ' ' << c.gravityScale;
        });
        save.operator()<BoxCollider3DComponent>(
            [&](const auto& c) { out << c.size << c.offset << c.isTrigger; });
        save.operator()<TilemapComponent>([&](const auto& c) {
            std::string cells;
            cells.reserve(c.tiles.size());
            for (uint8_t tile : c.tiles)
                cells.push_back(static_cast<char>('0' + tile));
            out << c.width << ' ' << c.height << ' ' << c.tileSize << ' ' << std::quoted(cells);
        });
        save.operator()<AnimationComponent>([&](const auto& c) {
            out << std::quoted(c.clip.name) << ' ' << c.clip.duration << ' ' << c.clip.loop << ' '
                << c.playing << ' ' << c.clip.keyframes.size();
            for (const auto& key : c.clip.keyframes)
                out << ' ' << key.time << ' ' << key.position << key.rotation << key.scale;
        });
        save.operator()<HierarchyComponent>(
            [&](const auto& c) { out << c.parent << ' ' << c.group; });
        save.operator()<SceneReferenceComponent>([&](const auto& c) {
            out << std::quoted(c.path) << ' ' << static_cast<int>(c.mode) << ' '
                << std::quoted(c.baseline) << ' ' << std::quoted(c.suspended) << ' '
                << c.streamCenter << c.streamLoadRadius << ' ' << c.streamUnloadRadius << ' '
                << c.loaded << ' ' << c.entityMap.size();
            for (const auto& [source, local] : c.entityMap)
                out << ' ' << source << ' ' << local;
        });
        save.operator()<InstanceSourceComponent>(
            [&](const auto& c) { out << c.instanceRoot << ' ' << c.sourceEntity; });
        if (!out || out.tellp() > static_cast<std::streamoff>(MaxSceneSizeBytes))
            return false;
    }
    if (!out || out.tellp() > static_cast<std::streamoff>(MaxSceneSizeBytes))
        return false;
    output = out.str();
    return true;
}

void Scene::InjectSaveFault(SaveFault fault, uint32 count) {
    std::lock_guard<std::mutex> lock(s_SaveFaultMutex);
    s_InjectedSaveFault = fault;
    s_InjectedSaveFaultCount = count;
}

void Scene::ClearSaveFaults() {
    std::lock_guard<std::mutex> lock(s_SaveFaultMutex);
    s_InjectedSaveFault = SaveFault::None;
    s_InjectedSaveFaultCount = 0;
}

std::string Scene::GetLastSaveError() {
    std::lock_guard<std::mutex> lock(s_SaveErrorMutex);
    return s_LastSaveError;
}

bool Scene::Serialize(const std::string& filepath) {
    if (filepath.empty() || filepath.find('\0') != std::string::npos || !IsValidUtf8(filepath)) {
        SetSaveError("Scene save failed: invalid UTF-8 or embedded NUL in filepath");
        return false;
    }
    std::string serialized;
    if (!SerializeToString(serialized)) {
        SetSaveError("Scene save failed: scene validation/serialization failed");
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path targetPath = Utf8Path(filepath);
    std::error_code ec;
    const fs::path parentDir = targetPath.parent_path();
    if (!parentDir.empty() && !fs::is_directory(parentDir, ec)) {
        SetSaveError("Scene save failed: parent directory does not exist for '" + filepath + "'");
        return false;
    }

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const uint64 seq = ++s_SaveSequence;
    fs::path tempPath = targetPath;
    tempPath += ".saving-" + std::to_string(now) + "-" + std::to_string(threadId) + "-" +
                std::to_string(seq);

    if (!WriteTempFileFlushed(tempPath, serialized))
        return false;

    std::lock_guard<std::mutex> replaceLock(s_FileReplaceMutex);
    if (!AtomicReplaceFile(tempPath, targetPath, seq))
        return false;

    ClearSaveError();
    return true;
}

bool Scene::DeserializeStream(std::istream& in) {
    in.imbue(std::locale::classic());
    std::string header;
    unsigned version = 0;
    uint64 count = 0;
    if (!(in >> header >> version >> count) || header != "LuxEngineScene" ||
        (version < 1 || version > 6) || count > MaxSceneEntities)
        return false;
    Scene parsed;
    parsed.nextEntityId_ = 1;
    uint32 lastId = 0;
    try {
        if (version >= 3) {
            std::string marker;
            SceneSettings settings;
            if (!(in >> marker >> settings.clearColor >> settings.exposure >> settings.gravity >>
                  settings.gravity3D >> settings.overrideGravity >> settings.fixedStep >>
                  settings.physicsSubsteps) ||
                marker != "Settings" || !parsed.SetSettings(settings))
                return false;
        }
        if (version >= 6) {
            std::string marker;
            int mode = -1;
            auto settings = parsed.GetSettings();
            auto& r = settings.render3D;
            if (!(in >> marker >> mode >> r.sun.direction >> r.sun.color >> r.sun.intensity >>
                  r.environmentColor >> r.environmentIntensity >> r.environmentRotation >>
                  std::quoted(r.environmentPath) >> r.imageBasedLighting >> r.showSkybox >>
                  r.toneMapping >> r.shadows >> r.shadowResolution >> r.shadowRange >>
                  r.shadowDepthBias >> r.shadowNormalBias) ||
                marker != "Render3D" || mode < 0 || mode > 1 || !IsValidUtf8(r.environmentPath))
                return false;
            r.mode = static_cast<Render3DMode>(mode);
            if (!parsed.SetSettings(settings))
                return false;
        }
        for (uint64 n = 0; n < count; ++n) {
            if (version >= 2) {
                uint32 id = 0;
                if (!(in >> id) || id == 0 || id <= lastId ||
                    id == std::numeric_limits<uint32>::max())
                    return false;
                parsed.nextEntityId_ = id;
                lastId = id;
            }
            auto entity = parsed.CreateEntity();
            entity.RemoveComponent<TagComponent>();
            entity.RemoveComponent<TransformComponent>();
            auto load = [&]<class T>(auto read) {
                bool present = false;
                if (!(in >> present))
                    throw std::runtime_error("Invalid scene");
                if (present) {
                    T c{};
                    read(c);
                    if (!in)
                        throw std::runtime_error("Invalid component");
                    entity.AddComponent<T>(std::move(c));
                }
            };
            load.operator()<TagComponent>([&](auto& c) {
                in >> std::quoted(c.tag);
                if (c.tag.size() > MaxTagStringLength || !IsValidUtf8(c.tag))
                    in.setstate(std::ios::failbit);
            });
            load.operator()<TransformComponent>([&](auto& c) {
                in >> c.position >> c.rotation >> c.scale;
                if (!Finite(c.position) || !Finite(c.rotation) || !Finite(c.scale))
                    in.setstate(std::ios::failbit);
            });
            load.operator()<SpriteRendererComponent>([&](auto& c) {
                in >> c.color;
                if (!Finite(c.color))
                    in.setstate(std::ios::failbit);
            });
            load.operator()<MeshRendererComponent>([&](auto& c) {
                in >> c.material.albedoColor >> c.material.metallic >> c.material.roughness;
                if (version >= 4) {
                    in >> std::quoted(c.meshPath);
                    if (c.meshPath.size() > 4096 || c.meshPath.find('\0') != std::string::npos ||
                        !IsValidUtf8(c.meshPath))
                        in.setstate(std::ios::failbit);
                }
                if (version >= 5) {
                    in >> std::quoted(c.material.baseColorTexture);
                    if (c.material.baseColorTexture.size() > 4096 ||
                        c.material.baseColorTexture.find('\0') != std::string::npos ||
                        !IsValidUtf8(c.material.baseColorTexture))
                        in.setstate(std::ios::failbit);
                }
                if (!Finite(c.material.albedoColor) || !Finite(c.material.metallic) ||
                    !Finite(c.material.roughness))
                    in.setstate(std::ios::failbit);
            });
            load.operator()<CameraComponent>([&](auto& c) {
                in >> c.isPrimary >> c.isPerspective >> c.fov >> c.orthoSize;
                if (!Finite(c.fov) || c.fov <= 0.0f || c.fov >= 180.0f || !Finite(c.orthoSize) ||
                    c.orthoSize <= 0.0f)
                    in.setstate(std::ios::failbit);
            });
            auto body = [&](auto& c) {
                int type = 0;
                in >> type >> c.velocity >> c.mass >> c.gravityScale;
                if (type < 0 || type > 2 || !Finite(c.velocity) || !Finite(c.mass) ||
                    c.mass <= 0.0f || !Finite(c.gravityScale))
                    in.setstate(std::ios::failbit);
                c.type = static_cast<BodyType>(type);
            };
            load.operator()<Rigidbody2DComponent>([&](auto& c) {
                body(c);
                in >> c.isGrounded;
            });
            load.operator()<BoxCollider2DComponent>([&](auto& c) {
                in >> c.size >> c.offset >> c.isTrigger;
                if (!Finite(c.size) || c.size.x <= 0.0f || c.size.y <= 0.0f || !Finite(c.offset))
                    in.setstate(std::ios::failbit);
            });
            load.operator()<Rigidbody3DComponent>(body);
            load.operator()<BoxCollider3DComponent>([&](auto& c) {
                in >> c.size >> c.offset >> c.isTrigger;
                if (!Finite(c.size) || c.size.x <= 0.0f || c.size.y <= 0.0f || c.size.z <= 0.0f ||
                    !Finite(c.offset))
                    in.setstate(std::ios::failbit);
            });
            if (version >= 2) {
                load.operator()<TilemapComponent>([&](auto& c) {
                    std::string cells;
                    in >> c.width >> c.height >> c.tileSize >> std::quoted(cells);
                    if (c.width == 0 || c.width > 256 || c.height == 0 || c.height > 256 ||
                        !std::isfinite(c.tileSize) || c.tileSize <= 0.0f || c.tileSize > 64.0f ||
                        cells.size() != static_cast<size_t>(c.width) * c.height) {
                        in.setstate(std::ios::failbit);
                        return;
                    }
                    c.tiles.clear();
                    c.tiles.reserve(cells.size());
                    for (char tile : cells) {
                        if (tile < '0' || tile > '5') {
                            in.setstate(std::ios::failbit);
                            return;
                        }
                        c.tiles.push_back(static_cast<uint8_t>(tile - '0'));
                    }
                });
                load.operator()<AnimationComponent>([&](auto& c) {
                    size_t keyCount = 0;
                    in >> std::quoted(c.clip.name) >> c.clip.duration >> c.clip.loop >> c.playing >>
                        keyCount;
                    if (c.clip.name.size() > MaxClipNameLength || !IsValidUtf8(c.clip.name) ||
                        keyCount > 4096 || !std::isfinite(c.clip.duration) ||
                        c.clip.duration <= 0.0f) {
                        in.setstate(std::ios::failbit);
                        return;
                    }
                    for (size_t keyIndex = 0; keyIndex < keyCount && in; ++keyIndex) {
                        Keyframe key;
                        in >> key.time >> key.position >> key.rotation >> key.scale;
                        if (!std::isfinite(key.time) || key.time < 0.0f || !Finite(key.position) ||
                            !Finite(key.rotation) || !Finite(key.scale)) {
                            in.setstate(std::ios::failbit);
                            return;
                        }
                        if (in)
                            c.clip.AddKeyframe(key);
                    }
                });
            }
            if (version >= 3) {
                load.operator()<HierarchyComponent>([&](auto& c) { in >> c.parent >> c.group; });
                load.operator()<SceneReferenceComponent>([&](auto& c) {
                    int mode = 0;
                    uint64 mappingCount = 0;
                    in >> std::quoted(c.path) >> mode >> std::quoted(c.baseline) >>
                        std::quoted(c.suspended) >> c.streamCenter >> c.streamLoadRadius >>
                        c.streamUnloadRadius >> c.loaded >> mappingCount;
                    if (mappingCount > MaxSceneEntities || mode < 0 || mode > 2) {
                        in.setstate(std::ios::failbit);
                        return;
                    }
                    c.mode = static_cast<SceneReferenceMode>(mode);
                    for (uint64 index = 0; index < mappingCount && in; ++index) {
                        uint32 source = 0, local = 0;
                        in >> source >> local;
                        c.entityMap.emplace_back(source, local);
                    }
                });
                load.operator()<InstanceSourceComponent>(
                    [&](auto& c) { in >> c.instanceRoot >> c.sourceEntity; });
            }
        }
        if (!ValidSceneMetadata(parsed))
            return false;
        in >> std::ws;
        if (!in.eof())
            return false;
    } catch (const std::exception&) {
        return false;
    }
    pools_.swap(parsed.pools_);
    entities_.swap(parsed.entities_);
    nextEntityId_ = parsed.nextEntityId_;
    settings_ = parsed.settings_;
    physicsAccumulator_ = 0;
    physics2D_.reset();
    collisionEvents2D_.clear();
    lifetime_ = std::make_shared<int>(0);
    return true;
}

bool Scene::DeserializeFromString(const std::string& data) {
    if (data.empty() || data.size() > MaxSceneSizeBytes)
        return false;
    std::istringstream in(data);
    return DeserializeStream(in);
}

bool Scene::Deserialize(const std::string& filepath) {
    if (filepath.empty() || filepath.find('\0') != std::string::npos || !IsValidUtf8(filepath))
        return false;
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(Utf8Path(filepath), ec);
    if (ec || fileSize == 0 || fileSize > MaxSceneSizeBytes)
        return false;
    std::ifstream in(Utf8Path(filepath), std::ios::binary);
    if (!in)
        return false;
    std::string data(static_cast<size_t>(fileSize), '\0');
    if (!in.read(data.data(), static_cast<std::streamsize>(data.size())) ||
        in.peek() != std::char_traits<char>::eof() || !DeserializeFromString(data))
        return false;
    sourcePath_ = filepath;
    return true;
}
} // namespace LuxEngine
