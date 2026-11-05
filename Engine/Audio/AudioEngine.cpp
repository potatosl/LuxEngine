#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace LuxEngine {

    struct AudioStorage {
        SDL_AudioStream* stream = nullptr;
        float masterVolume = 1.0f;
        bool isMusicPlaying = false;
        bool isInitialized = false;
    };

    static AudioStorage s_AudioStorage;

    bool AudioEngine::Init() {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            Log::Error(std::string("Failed to initialize SDL audio: ") + SDL_GetError());
            return false;
        }

        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = 44100;

        s_AudioStorage.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (s_AudioStorage.stream) {
            SDL_ResumeAudioStreamDevice(s_AudioStorage.stream);
        }

        s_AudioStorage.isInitialized = true;
        Log::Info("AudioEngine initialized successfully.");
        return true;
    }

    void AudioEngine::Shutdown() {
        if (s_AudioStorage.stream) {
            SDL_DestroyAudioStream(s_AudioStorage.stream);
            s_AudioStorage.stream = nullptr;
        }

        if (s_AudioStorage.isInitialized) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            s_AudioStorage.isInitialized = false;
        }

        s_AudioStorage.isMusicPlaying = false;
        Log::Info("AudioEngine shutdown.");
    }

    void AudioEngine::SetMasterVolume(float volume) {
        s_AudioStorage.masterVolume = std::clamp(volume, 0.0f, 1.0f);
        if (s_AudioStorage.stream) {
            SDL_SetAudioStreamGain(s_AudioStorage.stream, s_AudioStorage.masterVolume);
        }
    }

    float AudioEngine::GetMasterVolume() {
        return s_AudioStorage.masterVolume;
    }

    void AudioEngine::PlaySound(const std::string& soundName, float volume) {
        if (!s_AudioStorage.isInitialized) return;
        Log::Info(std::string("AudioEngine::PlaySound: ") + soundName + " (Vol: " + std::to_string(volume) + ")");
    }

    void AudioEngine::PlayMusic(const std::string& musicName, bool loop, float volume) {
        if (!s_AudioStorage.isInitialized) return;
        s_AudioStorage.isMusicPlaying = true;
        Log::Info(std::string("AudioEngine::PlayMusic: ") + musicName + " (Loop: " + (loop ? "true" : "false") + ", Vol: " + std::to_string(volume) + ")");
    }

    void AudioEngine::StopMusic() {
        s_AudioStorage.isMusicPlaying = false;
    }

    bool AudioEngine::IsMusicPlaying() {
        return s_AudioStorage.isMusicPlaying;
    }

} // namespace LuxEngine
