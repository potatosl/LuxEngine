#include "Engine/Scene/Scene.hpp"
#include "Engine/Physics/PhysicsEngine.hpp"
#include "Engine/Physics/PhysicsWorld2D.h"
#include "Engine/Scene/Entity.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <vector>

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
    if (!physics2D_)
        physics2D_ = std::make_unique<PhysicsWorld2D>();
    physics2D_->Step(*this, dt, collisionEvents2D_);
    for (auto id : entities_) {
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
void Scene::OnRender() {}
namespace {
std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
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
bool Scene::Serialize(const std::string& filepath) {
    const std::unordered_set<std::type_index> supported{typeid(TagComponent),
                                                        typeid(TransformComponent),
                                                        typeid(SpriteRendererComponent),
                                                        typeid(MeshRendererComponent),
                                                        typeid(CameraComponent),
                                                        typeid(Rigidbody2DComponent),
                                                        typeid(BoxCollider2DComponent),
                                                        typeid(Rigidbody3DComponent),
                                                        typeid(BoxCollider3DComponent)};
    for (const auto& [type, pool] : pools_)
        if (!pool->Empty() && !supported.contains(type))
            return false;
    std::ofstream out(Utf8Path(filepath), std::ios::trunc);
    if (!out)
        return false;
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << "LuxEngineScene 1 "
        << entities_.size() << '\n';
    std::vector<uint32> ids(entities_.begin(), entities_.end());
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
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
    }
    out.flush();
    return static_cast<bool>(out);
}
bool Scene::Deserialize(const std::string& filepath) {
    std::ifstream in(Utf8Path(filepath));
    std::string header;
    unsigned version = 0;
    uint64 count = 0;
    if (!(in >> header >> version >> count) || header != "LuxEngineScene" || version != 1 ||
        count > 1000000)
        return false;
    Scene parsed;
    parsed.nextEntityId_ = nextEntityId_;
    try {
        for (uint64 n = 0; n < count; ++n) {
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
            load.operator()<TransformComponent>(
                [&](auto& c) { in >> c.position >> c.rotation >> c.scale; });
            load.operator()<SpriteRendererComponent>([&](auto& c) { in >> c.color; });
            load.operator()<MeshRendererComponent>([&](auto& c) {
                in >> c.material.albedoColor >> c.material.metallic >> c.material.roughness;
            });
            load.operator()<CameraComponent>(
                [&](auto& c) { in >> c.isPrimary >> c.isPerspective >> c.fov >> c.orthoSize; });
            auto body = [&](auto& c) {
                int type = 0;
                in >> type >> c.velocity >> c.mass >> c.gravityScale;
                if (type < 0 || type > 2)
                    in.setstate(std::ios::failbit);
                c.type = static_cast<BodyType>(type);
            };
            load.operator()<Rigidbody2DComponent>([&](auto& c) {
                body(c);
                in >> c.isGrounded;
            });
            load.operator()<BoxCollider2DComponent>(
                [&](auto& c) { in >> c.size >> c.offset >> c.isTrigger; });
            load.operator()<Rigidbody3DComponent>(body);
            load.operator()<BoxCollider3DComponent>(
                [&](auto& c) { in >> c.size >> c.offset >> c.isTrigger; });
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
    return true;
}
} // namespace LuxEngine
