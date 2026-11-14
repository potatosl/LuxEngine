#include <LuxEngine/Renderer/RHI/Texture.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace LuxEngine;

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Dimensions() {
    for (const auto dimensions : {std::array<uint32, 2>{1, 1}, {1, 9}, {13, 1}, {7, 5}, {16, 8}}) {
        const auto [width, height] = dimensions;
        std::vector<uint8> pixels(static_cast<size_t>(width) * height * 4, 127);
        std::vector<TextureMipLevel> levels;
        Check(BuildTextureMipChain(width, height, pixels, TextureColorSpace::Linear, false, levels),
              "Valid texture was rejected");
        uint32 w = width, h = height;
        for (const auto& level : levels) {
            Check(level.width == w && level.height == h &&
                      level.pixels.size() == static_cast<size_t>(w) * h * 4,
                  "Mip dimensions or byte budget are wrong");
            for (const auto value : level.pixels)
                Check(value == 127, "Constant image changed during downsampling");
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        Check(levels.back().width == 1 && levels.back().height == 1,
              "Mip chain omitted the final level");
    }
}

void Colour() {
    std::vector<uint8> pixels{0, 0, 0, 255, 255, 255, 255, 255};
    std::vector<TextureMipLevel> levels;
    Check(BuildTextureMipChain(2, 1, pixels, TextureColorSpace::Srgb, false, levels),
          "sRGB texture was rejected");
    Check(levels[1].pixels[0] >= 187 && levels[1].pixels[0] <= 189,
          "sRGB mip was averaged in encoded space");
    Check(BuildTextureMipChain(2, 1, pixels, TextureColorSpace::Linear, false, levels),
          "Linear texture was rejected");
    Check(levels[1].pixels[0] == 128, "Data map did not use linear averaging");
    pixels = {255, 0, 0, 255, 0, 0, 255, 0};
    Check(BuildTextureMipChain(2, 1, pixels, TextureColorSpace::Srgb, false, levels),
          "Alpha texture was rejected");
    Check(levels[1].pixels[0] == 255 && levels[1].pixels[2] == 0 && levels[1].pixels[3] == 128,
          "Transparent texels contaminated the colour mip");
    pixels = {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    Check(BuildTextureMipChain(3, 1, pixels, TextureColorSpace::Linear, false, levels),
          "Odd texture was rejected");
    Check(levels[1].pixels[0] == 85, "Odd-size downsampling lost the final texel");
}

void Normals() {
    const std::array<uint8, 8> pixels{255, 128, 128, 255, 128, 255, 128, 255};
    std::vector<TextureMipLevel> levels;
    Check(BuildTextureMipChain(2, 1, pixels, TextureColorSpace::Linear, true, levels),
          "Normal map was rejected");
    const auto& normal = levels.back().pixels;
    const float x = normal[0] / 127.5f - 1.f, y = normal[1] / 127.5f - 1.f,
                z = normal[2] / 127.5f - 1.f;
    Check(std::abs(std::sqrt(x * x + y * y + z * z) - 1.f) < .015f,
          "Normal mip was not normalized");
    Check(normal[0] > 210 && normal[1] > 210, "Normal mip orientation is wrong");
}

void AtomicFailure() {
    std::vector<TextureMipLevel> levels{{1, 1, {1, 2, 3, 4}}};
    const std::array<uint8, 4> pixel{};
    Check(!BuildTextureMipChain(0, 1, pixel, TextureColorSpace::Linear, false, levels),
          "Zero dimension was accepted");
    Check(!BuildTextureMipChain(8193, 1, pixel, TextureColorSpace::Linear, false, levels),
          "Oversize dimension was accepted");
    Check(!BuildTextureMipChain(2, 1, pixel, TextureColorSpace::Linear, false, levels),
          "Truncated pixels were accepted");
    Check(!BuildTextureMipChain(1, 1, pixel, TextureColorSpace::Srgb, true, levels),
          "sRGB normal map was accepted");
    Check(levels.size() == 1 && levels[0].pixels == std::vector<uint8>({1, 2, 3, 4}),
          "Failed mip generation replaced the existing chain");
}
} // namespace

int main() {
    try {
        Dimensions();
        Colour();
        Normals();
        AtomicFailure();
        std::cout << "Texture mip dimensions, colour space, normals and failure contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
