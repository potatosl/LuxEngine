#pragma once

#include "Engine/Core/Core.hpp"
#include <string>

namespace LuxEngine {

    class AudioEngine {
    public:
        static bool Init();
        static void Shutdown();

        static void SetMasterVolume(float volume);
        static float GetMasterVolume();

        static void PlaySound(const std::string& soundName, float volume = 1.0f);
        static void PlayMusic(const std::string& musicName, bool loop = true, float volume = 1.0f);
        static void StopMusic();
        static bool IsMusicPlaying();
    };

} // namespace LuxEngine
