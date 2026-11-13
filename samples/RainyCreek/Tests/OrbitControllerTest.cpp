#include <LuxEngine/EntryPoint.hpp>
#include <RainyCreek/OrbitController.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right, float tolerance = 0.0001f) {
    return std::abs(left - right) <= tolerance;
}

bool Finite(const LuxEngine::Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Same(const RainyCreek::OrbitState& left, const RainyCreek::OrbitState& right) {
    return left.yaw == right.yaw && left.pitch == right.pitch && left.distance == right.distance &&
           left.target.x == right.target.x && left.target.y == right.target.y &&
           left.target.z == right.target.z;
}

void CheckCamera(const RainyCreek::OrbitController& orbit, float aspect) {
    const auto camera = orbit.Camera(aspect);
    Check(Finite(camera.GetPosition()) && Finite(camera.GetTarget()),
          "camera position and target stay finite");
    for (const float value : camera.GetViewProjectionMatrix().elements)
        Check(std::isfinite(value), "camera matrix stays finite");

    const auto displacement = camera.GetPosition() - camera.GetTarget();
    Check(Near(std::sqrt(displacement.Dot(displacement)), orbit.GetState().distance, 0.003f),
          "camera remains at the requested orbit distance");
}

void TestLimitsAndReset() {
    RainyCreek::OrbitController orbit;
    const auto initial = orbit.GetState();
    Check(Near(initial.distance, 10.0f) && Near(initial.target.y, 1.25f),
          "the initial camera frames the creek");
    CheckCamera(orbit, 1.6f);

    orbit.Drag(1.0e6f, 1.0e6f);
    Check(Near(orbit.GetState().pitch, 1.42f), "upward orbit stops before the pole");
    orbit.Drag(-1.0e6f, -1.0e6f);
    Check(Near(orbit.GetState().pitch, 0.06f), "downward orbit stops above the horizon");
    Check(std::abs(orbit.GetState().yaw) <= 3.142f, "yaw wraps after many revolutions");

    orbit.Zoom(1000000.0f);
    Check(Near(orbit.GetState().distance, 5.0f), "zoom stops at the near distance");
    orbit.Zoom(-1000000.0f);
    Check(Near(orbit.GetState().distance, 24.0f), "zoom stops at the far distance");
    orbit.Pan(50.0f, 30.0f);
    orbit.Reset();
    Check(Same(initial, orbit.GetState()), "reset restores only the owned camera state");
}

void TestInvalidInput() {
    RainyCreek::OrbitController orbit;
    const auto initial = orbit.GetState();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    orbit.Drag(nan, 10.0f);
    orbit.Drag(10.0f, infinity);
    orbit.Zoom(nan);
    orbit.Zoom(infinity);
    orbit.Pan(infinity, 10.0f);
    orbit.Pan(10.0f, nan);
    Check(Same(initial, orbit.GetState()), "non-finite input cannot change camera state");

    const float largest = std::numeric_limits<float>::max();
    orbit.Drag(largest, -largest);
    orbit.Pan(largest, largest);
    orbit.Zoom(-largest);
    Check(Finite(orbit.GetState().target), "extreme pointer deltas cannot overflow the target");
    CheckCamera(orbit, 1.6f);
    CheckCamera(orbit, nan);
    CheckCamera(orbit, infinity);
    CheckCamera(orbit, 0.0f);
    CheckCamera(orbit, -1.0f);
    CheckCamera(orbit, std::numeric_limits<float>::min());
    CheckCamera(orbit, largest);
}

void TestEventPartitioning() {
    RainyCreek::OrbitController oneEvent;
    RainyCreek::OrbitController manyFrames;
    oneEvent.Zoom(2.0f);
    for (int frame = 0; frame < 120; ++frame)
        manyFrames.Zoom(2.0f / 120.0f);
    Check(Near(oneEvent.GetState().distance, manyFrames.GetState().distance),
          "equal wheel input produces equal zoom across frame rates");
    oneEvent.Zoom(-2.0f);
    Check(Near(oneEvent.GetState().distance, 10.0f),
          "reversing wheel input restores distance away from limits");

    oneEvent.Reset();
    manyFrames.Reset();
    oneEvent.Pan(120.0f, 60.0f);
    for (int frame = 0; frame < 60; ++frame)
        manyFrames.Pan(2.0f, 1.0f);
    Check(Near(oneEvent.GetState().target.x, manyFrames.GetState().target.x) &&
              Near(oneEvent.GetState().target.y, manyFrames.GetState().target.y) &&
              Near(oneEvent.GetState().target.z, manyFrames.GetState().target.z),
          "panning depends on pointer distance rather than frame count");
    CheckCamera(manyFrames, 1.6f);
}

void TestPanScaleAndDirection() {
    RainyCreek::OrbitController nearView;
    RainyCreek::OrbitController farView;
    farView.Zoom(-1.0f);
    const auto initial = nearView.GetState().target;
    nearView.Pan(100.0f, 0.0f);
    farView.Pan(100.0f, 0.0f);
    const auto nearDelta = nearView.GetState().target - initial;
    const auto farDelta = farView.GetState().target - initial;
    const float nearDistance = std::sqrt(nearDelta.Dot(nearDelta));
    const float farDistance = std::sqrt(farDelta.Dot(farDelta));
    Check(nearDistance > 0.0f && farDistance > nearDistance,
          "panning covers more world space when zoomed out");
    Check(Near(nearView.GetState().target.y, initial.y),
          "horizontal panning follows the camera right vector");
    nearView.Pan(0.0f, 50.0f);
    Check(nearView.GetState().target.y > initial.y,
          "vertical panning follows the camera up vector");
}

} // namespace

int RunRainyCreekOrbitTests(int, char**) {
    try {
        TestLimitsAndReset();
        TestInvalidInput();
        TestEventPartitioning();
        TestPanScaleAndDirection();
        std::cout << "Rainy Creek orbit checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Rainy Creek orbit check failed: " << error.what() << '\n';
        return 1;
    }
}

LUX_ENGINE_MAIN(RunRainyCreekOrbitTests)
