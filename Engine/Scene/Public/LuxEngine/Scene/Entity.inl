#pragma once

namespace LuxEngine {
template <typename T, typename... Args> T& Entity::AddComponent(Args&&... args) {
    RequireValid();
    return scene_->AddComponent<T>(id_, std::forward<Args>(args)...);
}

template <typename T> T& Entity::GetComponent() {
    RequireValid();
    return scene_->GetComponent<T>(id_);
}

template <typename T> const T& Entity::GetComponent() const {
    RequireValid();
    return scene_->GetComponent<T>(id_);
}

template <typename T> T* Entity::TryGetComponent() {
    return IsValid() ? scene_->TryGetComponent<T>(id_) : nullptr;
}

template <typename T> const T* Entity::TryGetComponent() const {
    return IsValid() ? scene_->TryGetComponent<T>(id_) : nullptr;
}

template <typename T> bool Entity::HasComponent() const {
    return IsValid() && scene_->HasComponent<T>(id_);
}

template <typename T> void Entity::RemoveComponent() {
    if (IsValid())
        scene_->RemoveComponent<T>(id_);
}
} // namespace LuxEngine
