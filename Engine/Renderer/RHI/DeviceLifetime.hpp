#pragma once
#include <memory>
#include <unordered_map>
namespace LuxEngine {
class DeviceLifetime {
public:
    static void Register(const void* device) {
        Devices()[device] = std::make_shared<int>(0);
    }
    static void Forget(const void* device) {
        Devices().erase(device);
    }
    void Bind(const void* device) {
        auto it = Devices().find(device);
        tracked_ = it != Devices().end();
        token_ = tracked_ ? it->second : std::weak_ptr<int>{};
    }
    bool Alive() const {
        return !tracked_ || !token_.expired();
    }
    bool Tracked() const {
        return tracked_;
    }

private:
    static std::unordered_map<const void*, std::shared_ptr<int>>& Devices() {
        static std::unordered_map<const void*, std::shared_ptr<int>> devices;
        return devices;
    }
    std::weak_ptr<int> token_;
    bool tracked_ = false;
};
} // namespace LuxEngine
