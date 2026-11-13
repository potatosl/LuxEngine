#include <RainyCreek/OrbitController.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace RainyCreek {
namespace {

constexpr double RotationPerPixel = 0.004;
constexpr double PanPerPixel = 0.0015;
constexpr double ZoomPerNotch = 0.15;
constexpr double MinimumPitch = 0.06;
constexpr double MaximumPitch = 1.42;
constexpr double MinimumDistance = 5.0;
constexpr double MaximumDistance = 24.0;
constexpr double MaximumTargetCoordinate = 10000.0;

bool ValidDelta(float dx, float dy) {
    return std::isfinite(dx) && std::isfinite(dy);
}

float TargetCoordinate(double value) {
    // Keep camera offsets representable even after an extreme pointer delta.
    return static_cast<float>(std::clamp(value, -MaximumTargetCoordinate, MaximumTargetCoordinate));
}

} // namespace

OrbitController::OrbitController() {
    Reset();
}

void OrbitController::Reset() {
    state_ = {0.55f, 0.32f, 10.0f, {0.0f, 1.25f, 0.0f}};
}

void OrbitController::Drag(float dx, float dy) {
    if (!ValidDelta(dx, dy))
        return;

    const double yaw = state_.yaw - static_cast<double>(dx) * RotationPerPixel;
    const double pitch = state_.pitch + static_cast<double>(dy) * RotationPerPixel;
    state_.yaw = static_cast<float>(std::remainder(yaw, 2.0 * std::numbers::pi));
    state_.pitch = static_cast<float>(std::clamp(pitch, MinimumPitch, MaximumPitch));
}

void OrbitController::Zoom(float wheel) {
    if (!std::isfinite(wheel))
        return;

    const double exponent = std::clamp(-static_cast<double>(wheel) * ZoomPerNotch, -100.0, 100.0);
    const double distance = state_.distance * std::exp(exponent);
    state_.distance = static_cast<float>(std::clamp(distance, MinimumDistance, MaximumDistance));
}

void OrbitController::Pan(float dx, float dy) {
    if (!ValidDelta(dx, dy))
        return;

    const double yaw = state_.yaw;
    const double pitch = state_.pitch;
    const double horizontal = -static_cast<double>(dx) * state_.distance * PanPerPixel;
    const double vertical = static_cast<double>(dy) * state_.distance * PanPerPixel;
    const double rightX = std::cos(yaw);
    const double rightZ = -std::sin(yaw);
    const double upX = -std::sin(yaw) * std::sin(pitch);
    const double upY = std::cos(pitch);
    const double upZ = -std::cos(yaw) * std::sin(pitch);

    state_.target.x = TargetCoordinate(state_.target.x + horizontal * rightX + vertical * upX);
    state_.target.y = TargetCoordinate(state_.target.y + vertical * upY);
    state_.target.z = TargetCoordinate(state_.target.z + horizontal * rightZ + vertical * upZ);
}

const OrbitState& OrbitController::GetState() const {
    return state_;
}

LuxEngine::PerspectiveCamera OrbitController::Camera(float aspect) const {
    const float safeAspect =
        std::isfinite(aspect) && aspect > 0.0f ? std::clamp(aspect, 0.1f, 10.0f) : 16.0f / 10.0f;
    LuxEngine::PerspectiveCamera camera(52.0f * std::numbers::pi_v<float> / 180.0f, safeAspect,
                                        0.08f, 100.0f);
    const float horizontalDistance = state_.distance * std::cos(state_.pitch);
    camera.SetTarget(state_.target);
    camera.SetPosition({state_.target.x + horizontalDistance * std::sin(state_.yaw),
                        state_.target.y + state_.distance * std::sin(state_.pitch),
                        state_.target.z + horizontalDistance * std::cos(state_.yaw)});
    return camera;
}

} // namespace RainyCreek
