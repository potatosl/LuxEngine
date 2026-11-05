#pragma once

#include "Engine/Core/Core.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/ECS/ECS.hpp"
#include <string>
#include <vector>

namespace LuxEngine {

    struct Keyframe {
        float time = 0.0f;
        Vec3 position{ 0.0f, 0.0f, 0.0f };
        Vec3 rotation{ 0.0f, 0.0f, 0.0f };
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    class AnimationClip {
    public:
        std::string name;
        float duration = 1.0f;
        bool loop = true;
        std::vector<Keyframe> keyframes;

        void AddKeyframe(const Keyframe& kf) {
            keyframes.push_back(kf);
        }

        TransformComponent Sample(float time) const;
    };

    class AnimationEngine {
    public:
        static void Init();
        static void Shutdown();

        static void UpdateClip(const AnimationClip& clip, float& currentTime, float dt, TransformComponent& outTransform);
    };

} // namespace LuxEngine
