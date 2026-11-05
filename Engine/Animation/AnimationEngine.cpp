#include "Engine/Animation/AnimationEngine.hpp"
#include "Engine/Core/Log.hpp"
#include <algorithm>

namespace LuxEngine {

    TransformComponent AnimationClip::Sample(float time) const {
        TransformComponent result;
        if (keyframes.empty()) return result;
        if (keyframes.size() == 1) {
            result.position = keyframes[0].position;
            result.rotation = keyframes[0].rotation;
            result.scale = keyframes[0].scale;
            return result;
        }

        float clampedTime = time;
        if (duration > 0.0f) {
            if (loop) {
                clampedTime = std::fmod(time, duration);
                if (clampedTime < 0.0f) clampedTime += duration;
            } else {
                clampedTime = std::clamp(time, 0.0f, duration);
            }
        }

        if (!std::isfinite(clampedTime)) clampedTime = 0.0f;
        if (clampedTime >= keyframes.back().time) {
            result.position = keyframes.back().position;
            result.rotation = keyframes.back().rotation;
            result.scale = keyframes.back().scale;
            return result;
        }
        size_t nextIdx = 0;
        for (size_t i = 0; i < keyframes.size(); ++i) {
            if (keyframes[i].time >= clampedTime) {
                nextIdx = i;
                break;
            }
        }

        if (nextIdx == 0) {
            result.position = keyframes[0].position;
            result.rotation = keyframes[0].rotation;
            result.scale = keyframes[0].scale;
            return result;
        }

        const size_t prevIdx = nextIdx - 1;
        const auto& kf0 = keyframes[prevIdx];
        const auto& kf1 = keyframes[nextIdx];

        const float segmentDuration = kf1.time - kf0.time;
        const float factor = (segmentDuration > 0.0001f) ? (clampedTime - kf0.time) / segmentDuration : 0.0f;

        result.position = kf0.position + (kf1.position - kf0.position) * factor;
        result.rotation = kf0.rotation + (kf1.rotation - kf0.rotation) * factor;
        result.scale = kf0.scale + (kf1.scale - kf0.scale) * factor;
        return result;
    }

    void AnimationEngine::Init() {
        Log::Info("AnimationEngine initialized.");
    }

    void AnimationEngine::Shutdown() {
        Log::Info("AnimationEngine shutdown.");
    }

    void AnimationEngine::UpdateClip(const AnimationClip& clip, float& currentTime, float dt, TransformComponent& outTransform) {
        currentTime += dt;
        outTransform = clip.Sample(currentTime);
    }

} // namespace LuxEngine
