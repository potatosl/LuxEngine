#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Scene/Scene.hpp>

namespace LuxEngine {

class Entity {
public:
    LUX_API Entity();

    LUX_API Entity(uint32 id, Scene* scene);

    LUX_API uint32 GetId() const;

    LUX_API Scene* GetScene() const;

    LUX_API bool IsValid() const;

    LUX_API explicit operator bool() const;

    template <typename T, typename... Args> T& AddComponent(Args&&... args);

    template <typename T> T& GetComponent();

    template <typename T> const T& GetComponent() const;

    template <typename T> T* TryGetComponent();

    template <typename T> const T* TryGetComponent() const;

    template <typename T> bool HasComponent() const;

    template <typename T> void RemoveComponent();

    LUX_API bool operator==(const Entity& other) const;

    LUX_API bool operator!=(const Entity& other) const;

private:
    LUX_API void RequireValid() const;

    uint32 id_ = 0;
    Scene* scene_ = nullptr;
    std::weak_ptr<int> lifetime_;
};

} // namespace LuxEngine

#include <LuxEngine/Scene/Entity.inl>
