#include <LuxEngine/Platform/Branding.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stb_image.h>
#include <vector>

namespace LuxEngine::Branding {
SDL_Surface* CreateWindowIcon(bool dark) {
    const auto bytes = WindowIcon(dark);
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                         &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        SDL_SetError("Embedded window icon could not be decoded");
        return nullptr;
    }
    std::vector<unsigned> rows(height, 0), columns(width, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned alpha = pixels[(static_cast<size_t>(y) * width + x) * 4 + 3];
            rows[y] += alpha;
            columns[x] += alpha;
        }
    }
    const unsigned rowThreshold = std::max(1u, *std::max_element(rows.begin(), rows.end()) / 100);
    const unsigned columnThreshold =
        std::max(1u, *std::max_element(columns.begin(), columns.end()) / 100);
    int left = 0, right = width - 1, top = 0, bottom = height - 1;
    while (left < right && columns[left] < columnThreshold)
        ++left;
    while (right > left && columns[right] < columnThreshold)
        --right;
    while (top < bottom && rows[top] < rowThreshold)
        ++top;
    while (bottom > top && rows[bottom] < rowThreshold)
        --bottom;
    SDL_Rect crop{left, top, right - left + 1, bottom - top + 1};
    auto* source = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * 4);
    if (!source) {
        stbi_image_free(pixels);
        return nullptr;
    }
    SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
    SDL_Surface* icon = nullptr;
    for (int size : {32, 64, 128, 256}) {
        auto* image = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
        if (!image) {
            SDL_DestroySurface(icon);
            icon = nullptr;
            break;
        }
        SDL_FillSurfaceRect(image, nullptr, 0);
        const int padding = std::max(1, size / 32);
        const float scale = static_cast<float>(size - 2 * padding) / std::max(crop.w, crop.h);
        const int scaledWidth = std::max(1, static_cast<int>(std::lround(crop.w * scale)));
        const int scaledHeight = std::max(1, static_cast<int>(std::lround(crop.h * scale)));
        SDL_Rect target{(size - scaledWidth) / 2, (size - scaledHeight) / 2, scaledWidth,
                        scaledHeight};
        const bool drawn =
            SDL_BlitSurfaceScaled(source, &crop, image, &target, SDL_SCALEMODE_LINEAR);
        const bool added = !icon || SDL_AddSurfaceAlternateImage(icon, image);
        if (!drawn || !added) {
            SDL_DestroySurface(image);
            SDL_DestroySurface(icon);
            icon = nullptr;
            break;
        }
        if (!icon)
            icon = image;
        else
            SDL_DestroySurface(image);
    }
    SDL_DestroySurface(source);
    stbi_image_free(pixels);
    return icon;
}
} // namespace LuxEngine::Branding
