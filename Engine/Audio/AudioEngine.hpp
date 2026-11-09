#pragma once

#include "Engine/Core/Core.hpp"
#include <string>

namespace LuxEngine {

class LUX_API AudioEngine {
public:
    static bool Init();
    static void Shutdown();

    static void SetMasterVolume(float volume);
    static float GetMasterVolume();

    static bool PlaySound(const std::string& soundName, float volume = 1.0f);
    static bool PlayMusic(const std::string& musicName, bool loop = true, float volume = 1.0f);
    static void StopMusic();
    static bool IsMusicPlaying();

    static uint64 GetCacheUsageBytes();
    static size_t GetCachedFileCount();
};

} // namespace LuxEngine
