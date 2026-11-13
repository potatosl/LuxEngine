#include <RainyCreek/CreekGeometry.hpp>
#include <RainyCreek/CreekState.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace RainyCreek {
namespace {
// 고정된 정수 생성기를 사용해 표준 라이브러리 구현과 무관하게 같은 장면을 만듭니다.
class Sequence {
public:
    float Next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 16777216.f;
    }

private:
    std::uint32_t state_ = 0x43524545;
};
} // namespace

CreekState::CreekState() {
    Sequence random;
    const auto add = [&](std::string model, float x, float z, float size, bool vegetation) {
        instances_.push_back({std::move(model),
                              {x, TerrainHeight(x, z) - .035f, z},
                              random.Next() * 2 * std::numbers::pi_v<float>,
                              size,
                              vegetation});
    };
    for (unsigned i = 0; i < 42; ++i) {
        const float z = -20 + random.Next() * 40;
        const float side = i % 2 ? -1.f : 1.f;
        const float x = StreamCenter(z) + side * (1.15f + random.Next() * 2.5f);
        add("rock_" + std::string(1, static_cast<char>('a' + i % 3)), x, z,
            .55f + random.Next() * .8f, false);
    }
    for (unsigned i = 0; i < 100; ++i) {
        const float z = -20 + random.Next() * 40;
        const float side = i % 2 ? -1.f : 1.f;
        const float x = StreamCenter(z) + side * (1.75f + random.Next() * 9.5f);
        add(i % 2 ? "fern_a" : "fern_b", x, z, .6f + random.Next() * .65f, true);
    }
    for (unsigned i = 0; i < 18; ++i) {
        const float z = -21 + random.Next() * 42;
        const float side = i % 2 ? -1.f : 1.f;
        const float x = StreamCenter(z) + side * (4.5f + random.Next() * 9);
        add("pine", x, z, 3.8f + random.Next() * 2.5f, true);
    }
    add("trunk", -4.1f, -2.8f, 3.8f, false);
    add("trunk", 4.9f, -11.7f, 3.0f, false);
    add("trunk", -5.3f, 13.f, 4.2f, false);
}

void CreekState::Update(double dt) {
    if (!settings_.paused && std::isfinite(dt) && dt > 0)
        time_ += std::min(dt, .25);
}

void CreekState::Apply(Command command) {
    switch (command) {
    case Command::Pause:
        settings_.paused = !settings_.paused;
        break;
    case Command::Reset:
        settings_ = Settings{};
        // 설정 초기화는 시뮬레이션 시간을 되감지 않습니다.
        break;
    case Command::Lighting:
        settings_.lighting =
            static_cast<Lighting>((static_cast<unsigned>(settings_.lighting) + 1) % 3);
        break;
    case Command::Lamps:
        settings_.lamps = !settings_.lamps;
        break;
    case Command::Shadows:
        settings_.shadows = !settings_.shadows;
        break;
    case Command::Rain:
        settings_.rain = !settings_.rain;
        break;
    case Command::Water:
        settings_.water = !settings_.water;
        break;
    case Command::Wind:
        settings_.wind = !settings_.wind;
        break;
    case Command::Lod:
        settings_.lod = !settings_.lod;
        break;
    }
}

void CreekState::AdjustExposure(float amount) {
    if (std::isfinite(amount))
        settings_.exposure = std::clamp(settings_.exposure + amount, .2f, 3.f);
}

void CreekState::SetTime(double seconds) {
    if (std::isfinite(seconds) && seconds >= 0 && seconds <= 86400)
        time_ = seconds;
}

double CreekState::Time() const {
    return time_;
}

const Settings& CreekState::GetSettings() const {
    return settings_;
}

const std::vector<Instance>& CreekState::Instances() const {
    return instances_;
}

} // namespace RainyCreek
