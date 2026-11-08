#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Scene.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
// In-memory fuzzing helper: avoids disk I/O, prevents multi-threaded path collisions,
// and runs thousands of times faster for high-throughput fuzz testing.
bool TestSceneBytes(const uint8_t* data, size_t size) {
    if (!data || size == 0 || size > 64 * 1024 * 1024)
        return false;

    std::string inMemory(reinterpret_cast<const char*>(data), size);
    LuxEngine::Scene scene;
    return scene.DeserializeFromString(inMemory);
}

std::vector<uint8_t> MakeBytes(std::string_view s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}
} // namespace

#if defined(LUX_FUZZING_ENGINE)
// Coverage-guided fuzzing entry point for libFuzzer / Clang sanitize=fuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) // limit to 1MB per fuzz test case
        return 0;
    TestSceneBytes(data, size);
    return 0;
}
#else
// Standalone runner for deterministic fuzz corpus execution and CI smoke testing
int main(int argc, char** argv) {
    if (argc < 2) {
        // Comprehensive corpus of pathological edge cases:
        // 0-byte, truncated, invalid versions, oversized entities, NaN/Inf, invalid UTF-8,
        // huge tilemaps, excessive keyframes, and negative physics attributes.
        std::vector<std::vector<uint8_t>> seeds = {
            {}, // 0-byte empty
            MakeBytes("LuxEngineScene"), // Truncated header
            MakeBytes("LuxEngineScene 99 0\n"), // Unknown version 99
            MakeBytes("LuxEngineScene 2 100001\n"), // Excessive entity count (> 100,000)
            MakeBytes("LuxEngineScene 2 1\n0\n"), // Entity ID 0
            {0xFFu, 0xFEu, 0xFDu, 0xFCu, 0x00u, 0x01u}, // Binary garbage
            std::vector<uint8_t>(4096, 'A'), // Very long token
            { 'L', 'u', 'x', 'E', 'n', 'g', 'i', 'n', 'e', 'S', 'c', 'e', 'n', 'e', ' ', '2', ' ', '1', '\n',
              '1', '\n', '1', ' ', '"', 0xFFu, 0xFEu, '"', '\n' }, // Invalid UTF-8 in tag
            MakeBytes("LuxEngineScene 2 1\n1\n0\n1 NaN 0 0 0 0 0 1 1 1\n"), // NaN in transform
            MakeBytes("LuxEngineScene 2 1\n1\n0\n0\n0\n0\n0\n0\n0\n0\n0\n1 300 300 1 \"\"\n0\n"), // Oversized tilemap (> 256x256)
        };

        // Also test valid round-tripped Scene
        {
            LuxEngine::Scene validScene;
            auto e = validScene.CreateEntity("SeedEntity");
            e.GetComponent<LuxEngine::TransformComponent>().position = {1.0f, 2.0f, 3.0f};
            std::string serialized;
            if (validScene.SerializeToString(serialized)) {
                seeds.push_back(std::vector<uint8_t>(serialized.begin(), serialized.end()));
            }
        }

        size_t executed = 0;
        for (const auto& seed : seeds) {
            TestSceneBytes(seed.data(), seed.size());
            ++executed;
        }
        std::cout << "FuzzScene standalone corpus checks passed (" << executed << " seeds executed)\n";
        return 0;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file: " << argv[1] << '\n';
        return 1;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    TestSceneBytes(buffer.data(), buffer.size());
    return 0;
}
#endif
