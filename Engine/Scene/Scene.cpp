#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Engine/Core/Log.hpp"
#include <fstream>

namespace LuxEngine {

    Scene::Scene() {
        Log::Info("Scene created.");
    }

    Scene::~Scene() {
        Log::Info("Scene destroyed.");
    }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity{ nextEntityId_++, this };
        entityCount_++;

        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();

        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        if (!entity.IsValid()) return;
        const uint32 id = entity.GetId();

        RemoveComponent<TagComponent>(id);
        RemoveComponent<TransformComponent>(id);
        RemoveComponent<SpriteRendererComponent>(id);
        RemoveComponent<MeshRendererComponent>(id);
        RemoveComponent<CameraComponent>(id);

        if (entityCount_ > 0) {
            entityCount_--;
        }
    }

    void Scene::OnUpdate(double /*dt*/) {
    }

    void Scene::OnRender() {
    }

    bool Scene::Serialize(const std::string& filepath) {
        std::ofstream file(filepath);
        if (!file.is_open()) return false;

        file << "LuxEngineScene\n";
        file << "Entities: " << entityCount_ << "\n";
        return true;
    }

    bool Scene::Deserialize(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::string header;
        file >> header;
        return header == "LuxEngineScene";
    }

} // namespace LuxEngine
