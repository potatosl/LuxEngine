#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void AudioEngine::Init() {
        Log::Info("AudioEngine initialized (Prepared for miniaudio).");
    }

    void AudioEngine::Shutdown() {
        Log::Info("AudioEngine shutdown.");
    }

} // namespace LuxEngine
