#pragma once

#include <LuxEngine/Core/Math.hpp>
#include <string>
#include <vector>

namespace RainyCreek {

enum class Lighting { Night, Dusk, Day };
enum class Command { Pause, Reset, Lighting, Lamps, Shadows, Rain, Water, Wind, Lod };

struct Settings {
    Lighting lighting = Lighting::Night;
    bool paused = false;
    bool lamps = true;
    bool shadows = true;
    bool rain = true;
    bool water = true;
    bool wind = true;
    bool lod = true;
    float exposure = 1.0f;
};

struct Instance {
    std::string model;
    LuxEngine::Vec3 position;
    float yaw = 0;
    float size = 1;
    bool vegetation = false;
};

class CreekState {
public:
    CreekState();
    void Update(double dt);
    void Apply(Command command);
    void AdjustExposure(float amount);
    void SetTime(double seconds);
    double Time() const;
    const Settings& GetSettings() const;
    const std::vector<Instance>& Instances() const;

private:
    Settings settings_;
    double time_ = 0;
    std::vector<Instance> instances_;
};

} // namespace RainyCreek
