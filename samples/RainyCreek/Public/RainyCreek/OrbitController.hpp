#pragma once

#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>

namespace RainyCreek {

struct OrbitState {
    float yaw;
    float pitch;
    float distance;
    LuxEngine::Vec3 target;
};

class OrbitController {
public:
    OrbitController();

    void Reset();
    void Drag(float dx, float dy);
    void Zoom(float wheel);
    void Pan(float dx, float dy);

    const OrbitState& GetState() const;
    LuxEngine::PerspectiveCamera Camera(float aspect) const;

private:
    OrbitState state_;
};

} // namespace RainyCreek
