#include "Engine/Scene/Scene.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
// Helper to test Scene deserialization resilience against arbitrary byte streams.
bool TestSceneBytes(const uint8_t* data, size_t size) {
    namespace fs = std::filesystem;
    const fs::path tempPath = fs::temp_directory_path() /
                              ("lux-fuzz-scene-" + std::to_string(reinterpret_cast<uintptr_t>(data)) + ".lux");
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    LuxEngine::Scene scene;
    const bool ok = scene.Deserialize(tempPath.string());

    std::error_code ec;
    fs::remove(tempPath, ec);
    return ok;
}
} // namespace

#if defined(LUX_FUZZING_ENGINE)
// Coverage-guided fuzzing entry point for libFuzzer / Clang sanitize=fuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) // limit to 1MB per test case
        return 0;
    TestSceneBytes(data, size);
    return 0;
}
#else
// Standalone runner for deterministic fuzz corpus execution
int main(int argc, char** argv) {
    if (argc < 2) {
        // Run internal corpus of pathological edge cases
        std::vector<std::vector<uint8_t>> seeds = {
            {}, // Empty
            {'L', 'U', 'X', 'S', 'C', 'E', 'N', 'E'}, // Truncated header
            {'L', 'U', 'X', 'S', 'C', 'E', 'N', 'E', ' ', '9', '9', '9', '\n'}, // Unknown version
            {'L', 'U', 'X', 'S', 'C', 'E', 'N', 'E', ' ', '1', '\n', 'e', 'n', 't', 'i', 't', 'y', ' ', '0', '\n'}, // Entity 0
            {0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x01}, // Binary garbage
            std::vector<uint8_t>(2048, 'A'), // Large buffer
        };
        for (const auto& seed : seeds) {
            TestSceneBytes(seed.data(), seed.size());
        }
        std::cout << "FuzzScene standalone corpus checks passed\n";
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
