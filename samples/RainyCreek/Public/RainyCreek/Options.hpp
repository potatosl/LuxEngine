#pragma once

#include <RainyCreek/CreekState.hpp>
#include <filesystem>
#include <optional>

namespace RainyCreek {

struct Options {
    std::filesystem::path assets;
    std::filesystem::path capture;
    std::optional<double> fixedTime;
    Lighting lighting = Lighting::Night;
    unsigned frames = 0;
    unsigned width = 1280;
    unsigned height = 800;
    bool help = false;
    bool validate = false;
    bool smoke = false;
};

Options ParseOptions(int argc, char** argv);
void PrintHelp();

} // namespace RainyCreek
