#pragma once

namespace LuxEngine {
template <class T, class... Args> T& Scene::AddComponent(uint32 id, Args&&... args) {
    RequireAlive(id);
    auto& values = GetPool<T>().values;
    auto [it, added] = values.try_emplace(id, T{std::forward<Args>(args)...});
    if (!added)
        throw std::logic_error("Component already exists");
    return it->second;
}

template <class T> T& Scene::GetComponent(uint32 id) {
    return const_cast<T&>(std::as_const(*this).GetComponent<T>(id));
}

template <class T> const T& Scene::GetComponent(uint32 id) const {
    RequireAlive(id);
    auto it = pools_.find(typeid(T));
    if (it == pools_.end())
        throw std::out_of_range("Component does not exist");
    return static_cast<const Pool<T>&>(*it->second).values.at(id);
}

template <class T> T* Scene::TryGetComponent(uint32 id) {
    return const_cast<T*>(std::as_const(*this).TryGetComponent<T>(id));
}

template <class T> const T* Scene::TryGetComponent(uint32 id) const {
    if (!IsAlive(id))
        return nullptr;
    auto it = pools_.find(typeid(T));
    if (it == pools_.end())
        return nullptr;
    auto& values = static_cast<const Pool<T>&>(*it->second).values;
    auto component = values.find(id);
    return component == values.end() ? nullptr : &component->second;
}

template <class T> bool Scene::HasComponent(uint32 id) const {
    auto it = pools_.find(typeid(T));
    return IsAlive(id) && it != pools_.end() &&
           static_cast<const Pool<T>&>(*it->second).values.contains(id);
}

template <class T> std::vector<uint32> Scene::GetComponentEntityIds() const {
    const auto found = pools_.find(typeid(T));
    if (found == pools_.end())
        return {};
    const auto& values = static_cast<const Pool<T>&>(*found->second).values;
    std::vector<uint32> result;
    result.reserve(values.size());
    for (const auto& [id, component] : values)
        if (IsAlive(id))
            result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

template <class T> void Scene::RemoveComponent(uint32 id) {
    RequireAlive(id);
    if (!HasComponent<T>(id))
        return;
    auto it = pools_.find(typeid(T));
    if (it != pools_.end()) {
        it->second->Erase(id);
        if constexpr (std::is_same_v<T, TransformComponent> ||
                      std::is_same_v<T, Rigidbody2DComponent> ||
                      std::is_same_v<T, BoxCollider2DComponent> ||
                      std::is_same_v<T, Rigidbody3DComponent> ||
                      std::is_same_v<T, BoxCollider3DComponent>)
            RemovePhysicsBody(id);
    }
}

template <class T> void Scene::Pool<T>::Erase(uint32 id) {
    values.erase(id);
}

template <class T> bool Scene::Pool<T>::Empty() const {
    return values.empty();
}

template <class T> bool Scene::Pool<T>::Contains(uint32 id) const {
    return values.contains(id);
}

template <class T> Scene::Pool<T>& Scene::GetPool() {
    auto it = pools_.find(typeid(T));
    if (it == pools_.end())
        it = pools_.emplace(typeid(T), std::make_unique<Pool<T>>()).first;
    return static_cast<Pool<T>&>(*it->second);
}
} // namespace LuxEngine
