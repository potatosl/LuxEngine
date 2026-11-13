#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace LuxEngine {

struct Keyframe {
    float time = 0.0f;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

class LUX_API AnimationClip {
public:
    std::string name;
    float duration = 1.0f;
    bool loop = true;
    std::vector<Keyframe> keyframes;

    void AddKeyframe(const Keyframe& kf);

    TransformComponent Sample(float time) const;
};

struct AnimationComponent {
    AnimationClip clip;
    float currentTime = 0.0f;
    bool playing = true;
};

class LUX_API AnimationEngine {
public:
    static void Init();
    static void Shutdown();

    static void UpdateClip(const AnimationClip& clip, float& currentTime, float dt,
                           TransformComponent& outTransform);
};

} // namespace LuxEngine
