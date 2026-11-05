#include "Engine/Animation/AnimationEngine.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void AnimationEngine::Init() {
        Log::Info("AnimationEngine initialized.");
    }

    void AnimationEngine::Shutdown() {
        Log::Info("AnimationEngine shutdown.");
    }

    void AnimationEngine::Update(double /*dt*/) {
    }

} // namespace LuxEngine
