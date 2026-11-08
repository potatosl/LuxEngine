#include "Engine/Audio/AudioEngine.hpp"
#include "Engine/Core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace LuxEngine {
namespace {
struct CachedAudio {
    SDL_AudioSpec spec{};
    Uint8* bytes = nullptr;
    Uint32 length = 0;
    ~CachedAudio() {
        SDL_free(bytes);
    }
};
struct Voice {
    SDL_AudioStream* stream = nullptr;
    std::shared_ptr<CachedAudio> buffer;
    Uint32 cursor = 0;
    bool loop = false;
    float volume = 1;
    std::atomic<bool> exhausted = false;
    ~Voice() {
        if (stream)
            SDL_DestroyAudioStream(stream);
    }
};
SDL_AudioDeviceID device = 0;
bool initialized = false;
float master = 1;
std::unique_ptr<Voice> music;
std::vector<std::unique_ptr<Voice>> sounds;
std::unordered_map<std::string, std::shared_ptr<CachedAudio>> audioCache;

void SDLCALL Fill(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto& voice = *static_cast<Voice*>(userdata);
    if (!voice.buffer || !voice.buffer->bytes) {
        voice.exhausted = true;
        return;
    }
    while (additional > 0) {
        if (voice.cursor == voice.buffer->length) {
            if (!voice.loop) {
                if (!voice.exhausted.exchange(true))
                    SDL_FlushAudioStream(stream);
                return;
            }
            voice.cursor = 0;
        }
        const auto count =
            std::min(static_cast<Uint32>(additional), voice.buffer->length - voice.cursor);
        if (!SDL_PutAudioStreamData(stream, voice.buffer->bytes + voice.cursor,
                                    static_cast<int>(count))) {
            voice.exhausted = true;
            return;
        }
        voice.cursor += count;
        additional -= static_cast<int>(count);
    }
}
bool Playing(const Voice& voice) {
    return !voice.exhausted.load() || SDL_GetAudioStreamAvailable(voice.stream) > 0 ||
           SDL_GetAudioStreamQueued(voice.stream) > 0;
}
std::shared_ptr<CachedAudio> LoadBuffer(const std::string& file) {
    if (auto it = audioCache.find(file); it != audioCache.end())
        return it->second;
    auto cached = std::make_shared<CachedAudio>();
    if (!SDL_LoadWAV(file.c_str(), &cached->spec, &cached->bytes, &cached->length) ||
        !cached->length || cached->length > static_cast<Uint32>(std::numeric_limits<int>::max()))
        return {};
    if (audioCache.size() >= 128)
        audioCache.clear();
    audioCache.emplace(file, cached);
    return cached;
}
std::unique_ptr<Voice> Load(const std::string& file, bool loop, float volume) {
    if (!initialized || !std::isfinite(volume))
        return {};
    auto buffer = LoadBuffer(file);
    if (!buffer)
        return {};
    auto voice = std::make_unique<Voice>();
    voice->buffer = std::move(buffer);
    voice->volume = std::clamp(volume, 0.0f, 1.0f);
    voice->loop = loop;
    voice->stream = SDL_CreateAudioStream(&voice->buffer->spec, nullptr);
    if (!voice->stream || !SDL_SetAudioStreamGain(voice->stream, master * voice->volume) ||
        !SDL_SetAudioStreamGetCallback(voice->stream, Fill, voice.get()) ||
        !SDL_BindAudioStream(device, voice->stream))
        return {};
    return voice;
}
} // namespace
bool AudioEngine::Init() {
    if (initialized)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device || !SDL_ResumeAudioDevice(device)) {
        if (device)
            SDL_CloseAudioDevice(device);
        device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        Log::Error(SDL_GetError());
        return false;
    }
    initialized = true;
    return true;
}
void AudioEngine::Shutdown() {
    music.reset();
    sounds.clear();
    audioCache.clear();
    if (device)
        SDL_CloseAudioDevice(device);
    device = 0;
    if (initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    initialized = false;
}
void AudioEngine::SetMasterVolume(float volume) {
    if (!std::isfinite(volume))
        return;
    master = std::clamp(volume, 0.0f, 1.0f);
    if (music)
        SDL_SetAudioStreamGain(music->stream, master * music->volume);
    for (auto& sound : sounds)
        SDL_SetAudioStreamGain(sound->stream, master * sound->volume);
}
float AudioEngine::GetMasterVolume() {
    return master;
}
bool AudioEngine::PlaySound(const std::string& file, float volume) {
    std::erase_if(sounds, [](const auto& sound) { return !Playing(*sound); });
    if (sounds.size() >= 64)
        return false;
    auto voice = Load(file, false, volume);
    if (!voice)
        return false;
    sounds.push_back(std::move(voice));
    return true;
}
bool AudioEngine::PlayMusic(const std::string& file, bool loop, float volume) {
    auto voice = Load(file, loop, volume);
    if (!voice)
        return false;
    music = std::move(voice);
    return true;
}
void AudioEngine::StopMusic() {
    music.reset();
}
bool AudioEngine::IsMusicPlaying() {
    return music && Playing(*music);
}
} // namespace LuxEngine
