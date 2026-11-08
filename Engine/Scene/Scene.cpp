#include "Engine/Scene/Scene.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Scene/Entity.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
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
    RemovePhysicsBody2D(entity.GetId());
    for (auto& [type, pool] : pools_)
        pool->Erase(entity.GetId());
    entities_.erase(entity.GetId());
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
    physics2D_->Step(*this, dt, collisionEvents2D_);
    updateBuffer_.assign(entities_.begin(), entities_.end());
    for (auto id : updateBuffer_) {
        if (!entities_.contains(id))
            continue;
        if (!HasComponent<TransformComponent>(id))
            continue;
        auto& transform = GetComponent<TransformComponent>(id);
        if (!(HasComponent<Rigidbody2DComponent>(id) && HasComponent<BoxCollider2DComponent>(id)) &&
            HasComponent<Rigidbody3DComponent>(id) && HasComponent<BoxCollider3DComponent>(id))
            PhysicsEngine::Step3D(transform.position, GetComponent<Rigidbody3DComponent>(id),
                                  GetComponent<BoxCollider3DComponent>(id), dt);
    }
}
void Scene::RemovePhysicsBody2D(uint32 id) {
    if (physics2D_)
        physics2D_->Remove(id);
    collisionEvents2D_.clear();
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
} // namespace
void Scene::OnRender() {
    // 개발 중: 타일 종류는 고정 색상으로 표시합니다. 텍스처 아틀라스는 아직 연결되지 않았습니다.
    for (Entity entity : GetEntities()) {
        if (!entity.HasComponent<TransformComponent>())
            continue;
        const auto& transform = entity.GetComponent<TransformComponent>();
        if (!Finite(transform.position) || !Finite(transform.rotation) || !Finite(transform.scale))
            continue;
        if (entity.HasComponent<TilemapComponent>()) {
            const auto& map = entity.GetComponent<TilemapComponent>();
            if (!Finite(map.tileSize) || map.tileSize <= 0.0f)
                continue;
            for (uint32 y = 0; y < map.height; ++y)
                for (uint32 x = 0; x < map.width; ++x) {
                    const uint8_t tile = map.Get(x, y);
                    if (tile == 0)
                        continue;
                    const float size = map.tileSize;
                    Renderer2D::DrawQuad({transform.position.x + (x + .5f) * size,
                                          transform.position.y + (y + .5f) * size,
                                          transform.position.z},
                                         {size, size}, TileColor(tile));
                }
        }
        if (entity.HasComponent<SpriteRendererComponent>()) {
            const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
            if (Finite(sprite.color))
                Renderer2D::DrawRotatedQuad({transform.position.x, transform.position.y},
                                            {transform.scale.x, transform.scale.y},
                                            transform.rotation.z, sprite.color);
        }
    }
}
void Scene::OnRender3D() {
    for (Entity entity : GetEntities()) {
        if (!entity.HasComponent<TransformComponent>() ||
            !entity.HasComponent<MeshRendererComponent>())
            continue;
        const auto& transform = entity.GetComponent<TransformComponent>();
        if (!Finite(transform.position) || !Finite(transform.rotation) || !Finite(transform.scale))
            continue;
        const auto& meshComp = entity.GetComponent<MeshRendererComponent>();
        if (Finite(meshComp.material.albedoColor) && Finite(meshComp.material.metallic) &&
            Finite(meshComp.material.roughness))
            Renderer3D::DrawMesh(transform.GetTransform(), meshComp.material);
    }
}
namespace {
std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

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
        SetSaveError("Scene save failed: injected temporary file write failure (Win32 error 5: ERROR_ACCESS_DENIED) for " +
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
        SetSaveError("Scene save failed: could not open temporary file '" + PathToUtf8(tempPath) + "'");
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
        out.close();
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: write/flush error on temporary file '" + PathToUtf8(tempPath) + "'");
        return false;
    }
    out.close();
    if (!out) {
        fs::remove(tempPath, ec);
        SetSaveError("Scene save failed: close error on temporary file '" + PathToUtf8(tempPath) + "'");
        return false;
    }
    return true;
#endif
}

bool AtomicReplaceFile(const std::filesystem::path& tempPath, const std::filesystem::path& targetPath,
                       uint64 seq) {
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
        // If another writer created targetPath concurrently, fall through to atomic ReplaceFileW with backup.
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

    // Replacement failed. Absolute priority: preserve the original file and never clobber it with tempW.
    const bool backupExists = (GetFileAttributesW(backupW.c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!backupExists) {
        // Original target file was never renamed to backup and remains intact.
        DeleteFileW(tempW.c_str());
        SetSaveError("Scene save failed: ReplaceFileW failed before backup replacement for '" +
                     PathToUtf8(targetPath) + "' (Win32 error " + std::to_string(replaceErr) +
                     "). Original file preserved.");
        return false;
    }

    // Backup exists: the original file was moved to backupW. Attempt safe restoration of backupW -> targetW.
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
bool Scene::SerializeToString(std::string& output) {
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
                                                        typeid(AnimationComponent)};
    for (const auto& [type, pool] : pools_)
        if (!pool->Empty() && !supported.contains(type))
            return false;
    for (const uint32 id : entities_) {
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
                !Finite(mesh->material.roughness))
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
            if (!std::isfinite(animation->clip.duration) || animation->clip.duration <= 0 ||
                animation->clip.keyframes.size() > 4096)
                return false;
            for (const auto& key : animation->clip.keyframes)
                if (!std::isfinite(key.time) || key.time < 0 || !Finite(key.position) ||
                    !Finite(key.rotation) || !Finite(key.scale))
                    return false;
        }
    }
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << "LuxEngineScene 2 "
        << entities_.size() << '\n';
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
    }
    if (!out)
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
    if (filepath.empty()) {
        SetSaveError("Scene save failed: empty filepath");
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
    tempPath +=
        ".saving-" + std::to_string(now) + "-" + std::to_string(threadId) + "-" + std::to_string(seq);

    if (!WriteTempFileFlushed(tempPath, serialized))
        return false;

    std::lock_guard<std::mutex> replaceLock(s_FileReplaceMutex);
    if (!AtomicReplaceFile(tempPath, targetPath, seq))
        return false;

    ClearSaveError();
    return true;
}

bool Scene::DeserializeStream(std::istream& in) {
    std::string header;
    unsigned version = 0;
    uint64 count = 0;
    if (!(in >> header >> version >> count) || header != "LuxEngineScene" ||
        (version != 1 && version != 2) || count > 1000000)
        return false;
    Scene parsed;
    parsed.nextEntityId_ = 1;
    uint32 lastId = 0;
    try {
        for (uint64 n = 0; n < count; ++n) {
            if (version == 2) {
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
            load.operator()<TagComponent>([&](auto& c) { in >> std::quoted(c.tag); });
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
            if (version == 2) {
                load.operator()<TilemapComponent>([&](auto& c) {
                    std::string cells;
                    in >> c.width >> c.height >> c.tileSize >> std::quoted(cells);
                    if (c.width == 0 || c.width > 256 || c.height == 0 || c.height > 256 ||
                        !std::isfinite(c.tileSize) || c.tileSize <= 0.0f || c.tileSize > 64.0f ||
                        cells.size() != c.width * c.height)
                        in.setstate(std::ios::failbit);
                    c.tiles.clear();
                    for (char tile : cells) {
                        if (tile < '0' || tile > '5')
                            in.setstate(std::ios::failbit);
                        c.tiles.push_back(static_cast<uint8_t>(tile - '0'));
                    }
                });
                load.operator()<AnimationComponent>([&](auto& c) {
                    size_t keyCount = 0;
                    in >> std::quoted(c.clip.name) >> c.clip.duration >> c.clip.loop >> c.playing >>
                        keyCount;
                    if (keyCount > 4096 || !std::isfinite(c.clip.duration) ||
                        c.clip.duration <= 0.0f)
                        in.setstate(std::ios::failbit);
                    for (size_t keyIndex = 0; keyIndex < keyCount && in; ++keyIndex) {
                        Keyframe key;
                        in >> key.time >> key.position >> key.rotation >> key.scale;
                        if (!std::isfinite(key.time) || key.time < 0.0f || !Finite(key.position) ||
                            !Finite(key.rotation) || !Finite(key.scale))
                            in.setstate(std::ios::failbit);
                        if (in)
                            c.clip.AddKeyframe(key);
                    }
                });
            }
        }
        in >> std::ws;
        if (!in.eof())
            return false;
    } catch (const std::exception&) {
        return false;
    }
    pools_.swap(parsed.pools_);
    entities_.swap(parsed.entities_);
    nextEntityId_ = parsed.nextEntityId_;
    physics2D_.reset();
    collisionEvents2D_.clear();
    lifetime_ = std::make_shared<int>(0);
    return true;
}

bool Scene::DeserializeFromString(const std::string& data) {
    if (data.empty())
        return false;
    std::istringstream in(data);
    return DeserializeStream(in);
}

bool Scene::Deserialize(const std::string& filepath) {
    if (filepath.empty())
        return false;
    std::ifstream in(Utf8Path(filepath));
    if (!in)
        return false;
    return DeserializeStream(in);
}
} // namespace LuxEngine
