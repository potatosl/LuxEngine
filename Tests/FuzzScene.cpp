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
    if (!scene.DeserializeFromString(inMemory))
        return false;

    // Full pipeline verification on deserialized components
    std::string reserialized;
    scene.SerializeToString(reserialized);
    scene.OnUpdate(0.016);
    scene.OnRender();
    scene.OnRender3D();
    for (auto entity : scene.GetEntities()) {
        entity.IsValid();
    }
    return true;
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
// Standalone runner for deterministic fuzz corpus execution, CI smoke testing,
// and randomized mutational fuzzing (e.g. --runs 10000)
#include <chrono>
#include <random>

int main(int argc, char** argv) {
    // Comprehensive corpus of pathological edge cases:
    // 0-byte, truncated, invalid versions, oversized entities, NaN/Inf, invalid UTF-8,
    // huge tilemaps, excessive keyframes, and negative physics attributes.
    std::vector<std::vector<uint8_t>> seeds = {
        {},                                         // 0-byte empty
        MakeBytes("LuxEngineScene"),                // Truncated header
        MakeBytes("LuxEngineScene 99 0\n"),         // Unknown version 99
        MakeBytes("LuxEngineScene 2 100001\n"),     // Excessive entity count (> 100,000)
        MakeBytes("LuxEngineScene 2 1\n0\n"),       // Entity ID 0
        {0xFFu, 0xFEu, 0xFDu, 0xFCu, 0x00u, 0x01u}, // Binary garbage
        std::vector<uint8_t>(4096, 'A'),            // Very long token
        {'L', 'u', 'x', 'E',  'n', 'g',  'i', 'n', 'e', 'S',   'c',   'e', 'n', 'e', ' ',
         '2', ' ', '1', '\n', '1', '\n', '1', ' ', '"', 0xFFu, 0xFEu, '"', '\n'}, // Invalid UTF-8
                                                                                  // in tag
        MakeBytes("LuxEngineScene 2 1\n1\n0\n1 NaN 0 0 0 0 0 1 1 1\n"), // NaN in transform
        MakeBytes(
            "LuxEngineScene 2 1\n1\n0\n0\n0\n0\n0\n0\n0\n0\n0\n1 300 300 1 \"\"\n0\n"), // Oversized
                                                                                        // tilemap
                                                                                        // (>
                                                                                        // 256x256)
    };

    // Also test valid round-tripped Scene as seed
    {
        LuxEngine::Scene validScene;
        auto e = validScene.CreateEntity("SeedEntity");
        e.GetComponent<LuxEngine::TransformComponent>().position = {1.0f, 2.0f, 3.0f};
        std::string serialized;
        if (validScene.SerializeToString(serialized)) {
            seeds.push_back(std::vector<uint8_t>(serialized.begin(), serialized.end()));
        }
        auto& mesh = e.AddComponent<LuxEngine::MeshRendererComponent>();
        mesh.meshPath = "Assets/seed.obj";
        if (validScene.SerializeToString(serialized))
            seeds.push_back(MakeBytes(serialized));
        mesh.material.baseColorTexture = "Assets/기본 색상.PnG";
        if (validScene.SerializeToString(serialized))
            seeds.push_back(MakeBytes(serialized));
        auto settings = validScene.GetSettings();
        settings.render3D.mode = LuxEngine::Render3DMode::Pbr;
        settings.render3D.environmentPath = "Assets/environment.hdr";
        validScene.SetSettings(settings);
        if (validScene.SerializeToString(serialized))
            seeds.push_back(MakeBytes(serialized));
    }

    size_t targetRuns = 0;
    if (argc >= 2) {
        std::string_view arg1 = argv[1];
        if (arg1.starts_with("-runs=")) {
            targetRuns = std::stoull(std::string(arg1.substr(6)));
        } else if (arg1 == "--runs" && argc >= 3) {
            targetRuns = std::stoull(argv[2]);
        }
    }

    if (targetRuns > 0) {
        std::mt19937_64 rng(1337);
        std::vector<std::string> dict = {"LuxEngineScene",
                                         "Transform",
                                         "Camera",
                                         "SpriteRenderer",
                                         "MeshRenderer",
                                         "Tilemap",
                                         "Rigidbody2D",
                                         "BoxCollider2D",
                                         "CircleCollider2D",
                                         "Animation",
                                         "AudioSource",
                                         "NaN",
                                         "Inf",
                                         "-Inf",
                                         "1e30",
                                         "-1e30",
                                         "0",
                                         "-1",
                                         "999999",
                                         "\"\"",
                                         "\"\n\"",
                                         "\0",
                                         "\\",
                                         "/",
                                         "..",
                                         "null",
                                         "true",
                                         "false"};

        const auto start = std::chrono::steady_clock::now();
        size_t executed = 0;
        for (size_t i = 0; i < targetRuns; ++i) {
            // Select base seed
            auto current = seeds[rng() % seeds.size()];

            // Apply 1 to 4 random mutations
            const int mutations = 1 + static_cast<int>(rng() % 4);
            for (int m = 0; m < mutations; ++m) {
                const int kind = static_cast<int>(rng() % 6);
                if (kind == 0 && !current.empty()) {
                    // Byte flip
                    const size_t pos = rng() % current.size();
                    current[pos] ^= static_cast<uint8_t>(1u << (rng() % 8));
                } else if (kind == 1 && !current.empty()) {
                    // Truncation
                    current.resize(rng() % current.size());
                } else if (kind == 2) {
                    // Dictionary insertion
                    const auto& word = dict[rng() % dict.size()];
                    const size_t pos = current.empty() ? 0 : rng() % (current.size() + 1);
                    current.insert(current.begin() + pos, word.begin(), word.end());
                } else if (kind == 3) {
                    // Random byte insertion
                    const size_t pos = current.empty() ? 0 : rng() % (current.size() + 1);
                    current.insert(current.begin() + pos, static_cast<uint8_t>(rng() & 0xFF));
                } else if (kind == 4 && current.size() > 2) {
                    // Chunk deletion
                    const size_t pos = rng() % (current.size() - 1);
                    const size_t len = 1 + (rng() % std::min<size_t>(16, current.size() - pos));
                    current.erase(current.begin() + pos, current.begin() + pos + len);
                } else {
                    // Boundary number injection
                    const auto numStr = std::to_string(rng() % 1000000);
                    const size_t pos = current.empty() ? 0 : rng() % (current.size() + 1);
                    current.insert(current.begin() + pos, numStr.begin(), numStr.end());
                }
            }

            TestSceneBytes(current.data(), current.size());
            ++executed;

            if ((i + 1) % 2000 == 0 || i + 1 == targetRuns) {
                const auto now = std::chrono::steady_clock::now();
                const auto ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                std::cout << "[FuzzScene] Completed " << (i + 1) << " / " << targetRuns
                          << " mutational fuzz iterations (" << ms << " ms)\n";
            }
        }
        std::cout << "FuzzScene mutational execution PASSED: " << executed
                  << " iterations completed without crashes\n";
        return 0;
    }

    if (argc < 2) {
        size_t executed = 0;
        for (const auto& seed : seeds) {
            TestSceneBytes(seed.data(), seed.size());
            ++executed;
        }
        std::cout << "FuzzScene standalone corpus checks passed (" << executed
                  << " seeds executed)\n";
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
