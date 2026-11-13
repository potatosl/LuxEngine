#include <LuxEngine/Audio/AudioEngine.hpp>
#include <LuxEngine/Core/Log.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LuxEngine {
namespace {
constexpr uint64 MaxAudioCacheBytes = 64ull * 1024ull * 1024ull;  // 64 MiB total audio cache budget
constexpr uint64 MaxSingleAudioBytes = 16ull * 1024ull * 1024ull; // 16 MiB max per WAV file
constexpr size_t MaxCachedAudioFiles = 256;
constexpr size_t MaxConcurrentSounds = 64;

struct CachedAudio {
    SDL_AudioSpec spec{};
    Uint8* bytes = nullptr;
    Uint32 length = 0;
    std::string filepath;
    ~CachedAudio() {
        if (bytes) {
            SDL_free(bytes);
            bytes = nullptr;
        }
    }
};

struct Voice {
    SDL_AudioStream* stream = nullptr;
    std::shared_ptr<CachedAudio> buffer;
    Uint32 cursor = 0;
    bool loop = false;
    float volume = 1.0f;
    std::atomic<bool> exhausted = false;

    ~Voice() {
        if (stream) {
            // Detach callback and unbind stream BEFORE destruction to prevent audio callback UAF
            SDL_SetAudioStreamGetCallback(stream, nullptr, nullptr);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
    }
};

// Thread safety mutex protecting audio engine state, voices, and audio cache
std::mutex s_AudioMutex;
SDL_AudioDeviceID device = 0;
bool initialized = false;
float master = 1.0f;
std::unique_ptr<Voice> music;
std::vector<std::unique_ptr<Voice>> sounds;

// LRU Audio Cache Tracking
struct CacheEntry {
    std::shared_ptr<CachedAudio> audio;
    std::list<std::string>::iterator lruIt;
};
std::list<std::string> s_LruList;
std::unordered_map<std::string, CacheEntry> audioCache;
uint64 s_CurrentCacheBytes = 0;

void SDLCALL Fill(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto* voicePtr = static_cast<Voice*>(userdata);
    if (!voicePtr || !voicePtr->buffer || !voicePtr->buffer->bytes) {
        if (voicePtr)
            voicePtr->exhausted = true;
        return;
    }
    auto& voice = *voicePtr;
    while (additional > 0) {
        if (voice.cursor >= voice.buffer->length) {
            if (!voice.loop) {
                if (!voice.exhausted.exchange(true))
                    SDL_FlushAudioStream(stream);
                return;
            }
            voice.cursor = 0;
        }
        const auto count =
            std::min(static_cast<Uint32>(additional), voice.buffer->length - voice.cursor);
        if (count == 0 || !SDL_PutAudioStreamData(stream, voice.buffer->bytes + voice.cursor,
                                                  static_cast<int>(count))) {
            voice.exhausted = true;
            return;
        }
        voice.cursor += count;
        additional -= static_cast<int>(count);
    }
}

bool Playing(const Voice& voice) {
    if (!voice.stream)
        return false;
    return !voice.exhausted.load() || SDL_GetAudioStreamAvailable(voice.stream) > 0 ||
           SDL_GetAudioStreamQueued(voice.stream) > 0;
}

// Evicts LRU cached audio entries that are not actively held by any playing voice (use_count == 1)
void EvictAudioCacheIfNeeded(uint64 incomingBytes) {
    while (!s_LruList.empty() && (s_CurrentCacheBytes + incomingBytes > MaxAudioCacheBytes ||
                                  audioCache.size() >= MaxCachedAudioFiles)) {
        // Find the oldest entry from the back of LRU list
        bool evicted = false;
        for (auto it = s_LruList.rbegin(); it != s_LruList.rend(); ++it) {
            const std::string& key = *it;
            auto cacheIt = audioCache.find(key);
            if (cacheIt != audioCache.end()) {
                // If only the cache holds the buffer (use_count == 1), it is safe to evict
                if (cacheIt->second.audio.use_count() <= 1) {
                    s_CurrentCacheBytes -= cacheIt->second.audio->length;
                    auto baseIt = std::next(it).base();
                    s_LruList.erase(baseIt);
                    audioCache.erase(cacheIt);
                    evicted = true;
                    break;
                }
            }
        }
        if (!evicted) {
            // All currently cached items are in-use by playing voices; cannot evict further
            break;
        }
    }
}

std::shared_ptr<CachedAudio> LoadBuffer(const std::string& file) {
    if (file.empty())
        return nullptr;

    // Check existing cache
    if (auto it = audioCache.find(file); it != audioCache.end()) {
        // Move to front of LRU list
        s_LruList.erase(it->second.lruIt);
        s_LruList.push_front(file);
        it->second.lruIt = s_LruList.begin();
        return it->second.audio;
    }

    auto cached = std::make_shared<CachedAudio>();
    cached->filepath = file;
    if (!SDL_LoadWAV(file.c_str(), &cached->spec, &cached->bytes, &cached->length) ||
        !cached->bytes || cached->length == 0 || cached->length > MaxSingleAudioBytes ||
        cached->length > static_cast<Uint32>(std::numeric_limits<int>::max())) {
        return nullptr;
    }

    // Evict older unreferenced items to respect memory budget
    EvictAudioCacheIfNeeded(cached->length);

    // If adding this buffer would still exceed budget because active voices hold existing cache
    // entries, bypass the cache entirely to enforce the strict 64 MiB ceiling without blocking
    // playback.
    if (s_CurrentCacheBytes + cached->length <= MaxAudioCacheBytes &&
        audioCache.size() < MaxCachedAudioFiles) {
        s_LruList.push_front(file);
        CacheEntry entry;
        entry.audio = cached;
        entry.lruIt = s_LruList.begin();
        audioCache.emplace(file, std::move(entry));
        s_CurrentCacheBytes += cached->length;
    }

    return cached;
}

std::unique_ptr<Voice> Load(const std::string& file, bool loop, float volume) {
    if (!initialized || !device || !std::isfinite(volume))
        return nullptr;

    auto buffer = LoadBuffer(file);
    if (!buffer)
        return nullptr;

    auto voice = std::make_unique<Voice>();
    voice->buffer = std::move(buffer);
    voice->volume = std::clamp(volume, 0.0f, 1.0f);
    voice->loop = loop;
    voice->stream = SDL_CreateAudioStream(&voice->buffer->spec, nullptr);
    if (!voice->stream)
        return nullptr;

    if (!SDL_SetAudioStreamGain(voice->stream, master * voice->volume) ||
        !SDL_SetAudioStreamGetCallback(voice->stream, Fill, voice.get()) ||
        !SDL_BindAudioStream(device, voice->stream)) {
        // Clean up stream safely before returning
        SDL_SetAudioStreamGetCallback(voice->stream, nullptr, nullptr);
        SDL_UnbindAudioStream(voice->stream);
        SDL_DestroyAudioStream(voice->stream);
        voice->stream = nullptr;
        return nullptr;
    }
    return voice;
}
} // namespace

bool AudioEngine::Init() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    if (initialized)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device || !SDL_ResumeAudioDevice(device)) {
        if (device) {
            SDL_CloseAudioDevice(device);
            device = 0;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        Log::Error(SDL_GetError());
        return false;
    }
    initialized = true;
    return true;
}

void AudioEngine::Shutdown() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    // 1. First detach and destroy all active voices so audio callback stops immediately
    music.reset();
    sounds.clear();

    // 2. Close audio device
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }

    // 3. Clear audio cache only after device and all stream callbacks are fully terminated
    audioCache.clear();
    s_LruList.clear();
    s_CurrentCacheBytes = 0;

    if (initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    initialized = false;
}

void AudioEngine::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    if (!std::isfinite(volume))
        return;
    master = std::clamp(volume, 0.0f, 1.0f);
    if (music && music->stream)
        SDL_SetAudioStreamGain(music->stream, master * music->volume);
    for (auto& sound : sounds) {
        if (sound && sound->stream)
            SDL_SetAudioStreamGain(sound->stream, master * sound->volume);
    }
}

float AudioEngine::GetMasterVolume() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    return master;
}

bool AudioEngine::PlaySound(const std::string& file, float volume) {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    if (!initialized || !device || !std::isfinite(volume))
        return false;

    // Prune exhausted or finished voices
    std::erase_if(sounds, [](const auto& sound) { return !sound || !Playing(*sound); });
    if (sounds.size() >= MaxConcurrentSounds)
        return false;

    auto voice = Load(file, false, volume);
    if (!voice)
        return false;
    sounds.push_back(std::move(voice));
    return true;
}

bool AudioEngine::PlayMusic(const std::string& file, bool loop, float volume) {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    if (!initialized || !device || !std::isfinite(volume))
        return false;

    auto voice = Load(file, loop, volume);
    if (!voice)
        return false;
    music = std::move(voice);
    return true;
}

void AudioEngine::StopMusic() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    music.reset();
}

bool AudioEngine::IsMusicPlaying() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    return music && Playing(*music);
}

uint64 AudioEngine::GetCacheUsageBytes() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    return s_CurrentCacheBytes;
}

size_t AudioEngine::GetCachedFileCount() {
    std::lock_guard<std::mutex> lock(s_AudioMutex);
    return audioCache.size();
}
} // namespace LuxEngine
