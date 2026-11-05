#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/ECS/ECS.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace LuxEngine {

    struct Keyframe {
        float time = 0.0f;
        Vec3 position{ 0.0f, 0.0f, 0.0f };
        Vec3 rotation{ 0.0f, 0.0f, 0.0f };
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    class LUX_API AnimationClip {
    public:
        std::string name;
        float duration = 1.0f;
        bool loop = true;
        std::vector<Keyframe> keyframes;

        void AddKeyframe(const Keyframe& kf) {
            if (!std::isfinite(kf.time) || kf.time < 0.0f) throw std::invalid_argument("Invalid keyframe time");
            auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.time, [](const Keyframe& k, float time) { return k.time < time; });
            if (it != keyframes.end() && it->time == kf.time) *it = kf;
            else keyframes.insert(it, kf);
        }

        TransformComponent Sample(float time) const;
    };

    class LUX_API AnimationEngine {
    public:
        static void Init();
        static void Shutdown();

        static void UpdateClip(const AnimationClip& clip, float& currentTime, float dt, TransformComponent& outTransform);
    };

} // namespace LuxEngine
