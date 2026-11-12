#include "Render3DSettings.hpp"
#include <cmath>
#include <tuple>

namespace LuxEngine {
namespace {
bool Utf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
            continue;
        const int count = first >= 0xc2 && first <= 0xdf   ? 1
                          : first >= 0xe0 && first <= 0xef ? 2
                          : first >= 0xf0 && first <= 0xf4 ? 3
                                                           : -1;
        if (count < 0 || text.size() - i < static_cast<size_t>(count))
            return false;
        uint32 code = first & ((1u << (6 - count)) - 1);
        for (int n = 0; n < count; ++n) {
            const auto c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (c & 0x3f);
        }
        if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
            (count == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
}

bool ValidateRender3DSettings(const Render3DSettings& s) {
    const auto bounded = [](float v, float low, float high) {
        return std::isfinite(v) && v >= low && v <= high;
    };
    const auto color = [&](Vec3 v) {
        return bounded(v.x, 0, 65504) && bounded(v.y, 0, 65504) && bounded(v.z, 0, 65504);
    };
    return (s.mode == Render3DMode::Legacy || s.mode == Render3DMode::Pbr) &&
           bounded(s.sun.direction.x, -1e4f, 1e4f) && bounded(s.sun.direction.y, -1e4f, 1e4f) &&
           bounded(s.sun.direction.z, -1e4f, 1e4f) &&
           s.sun.direction.Dot(s.sun.direction) > 1e-10f && color(s.sun.color) &&
           bounded(s.sun.intensity, 0, 10000) && color(s.environmentColor) &&
           bounded(s.environmentIntensity, 0, 10000) &&
           bounded(s.environmentRotation, -36000, 36000) && Utf8(s.environmentPath) &&
           s.environmentPath.size() <= 4096 && s.environmentPath.find('\0') == std::string::npos &&
           (s.shadowResolution == 1024 || s.shadowResolution == 2048 ||
            s.shadowResolution == 4096) &&
           bounded(s.shadowRange, 1, 10000) && bounded(s.shadowDepthBias, 0, .1f) &&
           bounded(s.shadowNormalBias, 0, 10);
}

bool IsDefaultRender3DSettings(const Render3DSettings& s) {
    const Render3DSettings d;
    const auto fields = [](const Render3DSettings& v) {
        return std::tie(v.mode, v.sun.direction.x, v.sun.direction.y, v.sun.direction.z,
                        v.sun.color.x, v.sun.color.y, v.sun.color.z, v.sun.intensity,
                        v.environmentColor.x, v.environmentColor.y, v.environmentColor.z,
                        v.environmentIntensity, v.environmentRotation, v.environmentPath,
                        v.imageBasedLighting, v.showSkybox, v.toneMapping, v.shadows,
                        v.shadowResolution, v.shadowRange, v.shadowDepthBias, v.shadowNormalBias);
    };
    return fields(s) == fields(d);
}
}
