#include <LuxEngine/Renderer/RHI/Texture.hpp>
#include <algorithm>
#include <cmath>
#include <exception>

namespace LuxEngine {
namespace {
float Decode(float value) {
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}

float Encode(float value) {
    return value <= .0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.f / 2.4f) - .055f;
}

uint8 Byte(float value) {
    return static_cast<uint8>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
}
} // namespace

bool BuildTextureMipChain(uint32 width, uint32 height, std::span<const uint8> pixels,
                          TextureColorSpace colorSpace, bool normalMap,
                          std::vector<TextureMipLevel>& output) {
    if (!width || !height || width > 8192 || height > 8192 ||
        pixels.size() != static_cast<size_t>(width) * height * 4 ||
        (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb) ||
        (normalMap && colorSpace == TextureColorSpace::Srgb))
        return false;
    try {
        std::vector<TextureMipLevel> levels;
        size_t bytes = 0;
        for (uint32 w = width, h = height;; w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
            bytes += static_cast<size_t>(w) * h * 4;
            if (bytes > 256ull * 1024 * 1024)
                return false;
            if (w == 1 && h == 1)
                break;
        }
        levels.push_back({width, height, std::vector<uint8>(pixels.begin(), pixels.end())});
        while (levels.back().width > 1 || levels.back().height > 1) {
            const auto& source = levels.back();
            TextureMipLevel target{
                std::max(1u, source.width / 2), std::max(1u, source.height / 2), {}};
            target.pixels.resize(static_cast<size_t>(target.width) * target.height * 4);
            for (uint32 y = 0; y < target.height; ++y) {
                const float top = static_cast<float>(y) * source.height / target.height;
                const float bottom = static_cast<float>(y + 1) * source.height / target.height;
                for (uint32 x = 0; x < target.width; ++x) {
                    const float left = static_cast<float>(x) * source.width / target.width;
                    const float right = static_cast<float>(x + 1) * source.width / target.width;
                    float sum[4] = {}, weight = 0, alphaWeight = 0;
                    for (uint32 sy = static_cast<uint32>(top);
                         sy < static_cast<uint32>(std::ceil(bottom)); ++sy) {
                        for (uint32 sx = static_cast<uint32>(left);
                             sx < static_cast<uint32>(std::ceil(right)); ++sx) {
                            const float area = (std::min(right, static_cast<float>(sx + 1)) -
                                                std::max(left, static_cast<float>(sx))) *
                                               (std::min(bottom, static_cast<float>(sy + 1)) -
                                                std::max(top, static_cast<float>(sy)));
                            const auto* pixel =
                                &source.pixels[(static_cast<size_t>(sy) * source.width + sx) * 4];
                            const float alpha = pixel[3] / 255.f;
                            for (unsigned channel = 0; channel < 3; ++channel) {
                                float value = pixel[channel] / 255.f;
                                if (normalMap)
                                    value = value * 2.f - 1.f;
                                else if (colorSpace == TextureColorSpace::Srgb)
                                    value = Decode(value) * alpha;
                                sum[channel] += value * area;
                            }
                            sum[3] += alpha * area;
                            alphaWeight += alpha * area;
                            weight += area;
                        }
                    }
                    auto* pixel = &target.pixels[(static_cast<size_t>(y) * target.width + x) * 4];
                    float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        float value = sum[channel] / weight;
                        if (normalMap)
                            value = length > 1e-8f ? sum[channel] / length * .5f + .5f
                                                   : (channel == 2 ? 1.f : .5f);
                        else if (colorSpace == TextureColorSpace::Srgb)
                            value = Encode(alphaWeight > 1e-8f ? sum[channel] / alphaWeight : 0.f);
                        pixel[channel] = Byte(value);
                    }
                    pixel[3] = Byte(sum[3] / weight);
                }
            }
            levels.push_back(std::move(target));
        }
        output = std::move(levels);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace LuxEngine
