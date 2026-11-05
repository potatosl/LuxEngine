#include "Engine/Scene/Scene.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    Scene::Scene() {
        Log::Info("Scene created.");
    }

    Scene::~Scene() {
        Log::Info("Scene destroyed.");
    }

    void Scene::OnUpdate(double /*dt*/) {
    }

    void Scene::OnRender() {
    }

} // namespace LuxEngine
