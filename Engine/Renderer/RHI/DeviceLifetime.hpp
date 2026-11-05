#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <unordered_map>
namespace LuxEngine {
class DeviceLifetime {
public:
    static void Register(SDL_GPUDevice* device) { Devices()[device] = std::make_shared<int>(0); }
    static void Forget(SDL_GPUDevice* device) { Devices().erase(device); }
    void Bind(SDL_GPUDevice* device) {
        auto it = Devices().find(device);
        tracked_ = it != Devices().end();
        token_ = tracked_ ? it->second : std::weak_ptr<int>{};
    }
    bool Alive() const { return !tracked_ || !token_.expired(); }
private:
    static std::unordered_map<SDL_GPUDevice*, std::shared_ptr<int>>& Devices() {
        static std::unordered_map<SDL_GPUDevice*, std::shared_ptr<int>> devices;
        return devices;
    }
    std::weak_ptr<int> token_;
    bool tracked_ = false;
};
}
