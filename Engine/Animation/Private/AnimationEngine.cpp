#include <LuxEngine/Animation/AnimationEngine.hpp>
#include <LuxEngine/Core/Log.hpp>
#include <algorithm>
#include <cmath>

namespace LuxEngine {

TransformComponent AnimationClip::Sample(float time) const {
    TransformComponent result;
    if (keyframes.empty())
        return result;
    if (keyframes.size() == 1) {
        result.position = keyframes[0].position;
        result.rotation = keyframes[0].rotation;
        result.scale = keyframes[0].scale;
        return result;
    }

    if (!std::isfinite(time))
        time = 0.0f;

    float clampedTime = time;
    if (duration > 0.0f) {
        if (loop) {
            clampedTime = std::fmod(time, duration);
            if (clampedTime < 0.0f)
                clampedTime += duration;
        } else {
            clampedTime = std::clamp(time, 0.0f, duration);
        }
    }

    if (!std::isfinite(clampedTime))
        clampedTime = 0.0f;
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), clampedTime,
                               [](const Keyframe& k, float t) { return k.time < t; });
    if (it == keyframes.end()) {
        result.position = keyframes.back().position;
        result.rotation = keyframes.back().rotation;
        result.scale = keyframes.back().scale;
        return result;
    }
    if (it == keyframes.begin()) {
        result.position = keyframes[0].position;
        result.rotation = keyframes[0].rotation;
        result.scale = keyframes[0].scale;
        return result;
    }

    const auto& kf0 = *(it - 1);
    const auto& kf1 = *it;

    const float segmentDuration = kf1.time - kf0.time;
    const float rawFactor =
        (segmentDuration > 0.0001f) ? (clampedTime - kf0.time) / segmentDuration : 0.0f;
    const float factor = std::clamp(rawFactor, 0.0f, 1.0f);

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

void AnimationEngine::UpdateClip(const AnimationClip& clip, float& currentTime, float dt,
                                 TransformComponent& outTransform) {
    if (!std::isfinite(currentTime))
        currentTime = 0.0f;
    if (std::isfinite(dt) && dt > 0.0f)
        currentTime += dt;
    if (clip.loop && clip.duration > 0.0f && std::isfinite(currentTime)) {
        currentTime = std::fmod(currentTime, clip.duration);
        if (currentTime < 0.0f)
            currentTime += clip.duration;
    }
    outTransform = clip.Sample(currentTime);
}

void AnimationClip::AddKeyframe(const Keyframe& kf) {
    if (!std::isfinite(kf.time) || kf.time < 0.0f)
        throw std::invalid_argument("Invalid keyframe time");
    auto it = std::lower_bound(keyframes.begin(), keyframes.end(), kf.time,
                               [](const Keyframe& k, float time) { return k.time < time; });
    if (it != keyframes.end() && it->time == kf.time)
        *it = kf;
    else
        keyframes.insert(it, kf);
}
} // namespace LuxEngine
