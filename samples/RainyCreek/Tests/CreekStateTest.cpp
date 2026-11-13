#include <LuxEngine/EntryPoint.hpp>
#include <RainyCreek/Catalog.hpp>
#include <RainyCreek/CreekGeometry.hpp>
#include <RainyCreek/CreekState.hpp>
#include <RainyCreek/Options.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace rc = RainyCreek;

void Check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

bool Near(double left, double right, double tolerance = 1e-6) {
    return std::abs(left - right) <= tolerance;
}

void CheckDefaults(const rc::Settings& settings) {
    Check(settings.lighting == rc::Lighting::Night && !settings.paused && settings.lamps &&
              settings.shadows && settings.rain && settings.water && settings.wind &&
              settings.lod && Near(settings.exposure, 1.0),
          "Settings did not restore their defaults");
}

void SimulationTime() {
    rc::CreekState state;
    CheckDefaults(state.GetSettings());
    Check(state.Time() == 0.0, "Simulation must start at zero");
    state.Update(.1);
    Check(Near(state.Time(), .1), "A running simulation did not advance");
    state.Apply(rc::Command::Pause);
    Check(state.GetSettings().paused, "Pause did not change the state");
    state.Update(.2);
    Check(Near(state.Time(), .1), "Paused simulation time changed");
    state.Apply(rc::Command::Pause);
    state.Update(.2);
    Check(!state.GetSettings().paused && Near(state.Time(), .3),
          "Resuming did not continue simulation time");
    for (const double dt : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()})
        state.Update(dt);
    Check(Near(state.Time(), .3), "Invalid delta time changed the simulation");
    state.Update(100.0);
    Check(Near(state.Time(), .55), "Large delta time was not bounded");
    state.SetTime(23.5);
    for (const double value : {-1.0, 86401.0, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()})
        state.SetTime(value);
    Check(Near(state.Time(), 23.5), "Invalid fixed time replaced a valid time");
    state.Apply(rc::Command::Lighting);
    state.Apply(rc::Command::Pause);
    state.Apply(rc::Command::Rain);
    state.AdjustExposure(.7f);
    state.Apply(rc::Command::Reset);
    CheckDefaults(state.GetSettings());
    Check(Near(state.Time(), 23.5), "Reset unexpectedly rewound simulation time");
    state.Update(.1);
    Check(Near(state.Time(), 23.6), "Reset did not resume the default running state");
    state.SetTime(0.0);
    Check(state.Time() == 0.0, "The fixed-time lower bound was rejected");
    state.SetTime(86400.0);
    Check(state.Time() == 86400.0, "The fixed-time upper bound was rejected");
}

void SettingsCommands() {
    rc::CreekState state;
    state.Apply(rc::Command::Lighting);
    Check(state.GetSettings().lighting == rc::Lighting::Dusk, "Night did not switch to dusk");
    state.Apply(rc::Command::Lighting);
    Check(state.GetSettings().lighting == rc::Lighting::Day, "Dusk did not switch to day");
    state.Apply(rc::Command::Lighting);
    Check(state.GetSettings().lighting == rc::Lighting::Night, "Lighting did not cycle");

    struct Toggle {
        rc::Command command;
        bool rc::Settings::* field;
    };

    const Toggle toggles[] = {
        {rc::Command::Lamps, &rc::Settings::lamps}, {rc::Command::Shadows, &rc::Settings::shadows},
        {rc::Command::Rain, &rc::Settings::rain},   {rc::Command::Water, &rc::Settings::water},
        {rc::Command::Wind, &rc::Settings::wind},   {rc::Command::Lod, &rc::Settings::lod}};
    for (const auto& toggle : toggles) {
        state.Apply(toggle.command);
        Check(!(state.GetSettings().*(toggle.field)), "Effect toggle did not disable its setting");
        state.Apply(toggle.command);
        Check(state.GetSettings().*(toggle.field), "Effect toggle did not restore its setting");
    }
    CheckDefaults(state.GetSettings());
    state.AdjustExposure(100.0f);
    Check(Near(state.GetSettings().exposure, 3.0), "Exposure exceeded its upper bound");
    state.AdjustExposure(-100.0f);
    Check(Near(state.GetSettings().exposure, .2), "Exposure exceeded its lower bound");
    state.AdjustExposure(std::numeric_limits<float>::quiet_NaN());
    state.AdjustExposure(std::numeric_limits<float>::infinity());
    Check(Near(state.GetSettings().exposure, .2), "Invalid exposure changed the setting");
    state.AdjustExposure(.3f);
    Check(Near(state.GetSettings().exposure, .5), "Finite exposure adjustment was lost");
}

void DeterministicPlacement() {
    const rc::CreekState first, second;
    const auto& left = first.Instances();
    const auto& right = second.Instances();
    Check(left.size() == 163 && right.size() == left.size(), "Unexpected scene instance count");
    std::map<std::string, unsigned> counts;
    for (size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        Check(a.model == b.model && a.position.x == b.position.x && a.position.y == b.position.y &&
                  a.position.z == b.position.z && a.yaw == b.yaw && a.size == b.size &&
                  a.vegetation == b.vegetation,
              "Repeated construction produced a different scene");
        for (const float value : {a.position.x, a.position.y, a.position.z, a.yaw, a.size})
            Check(std::isfinite(value), "An instance contains non-finite placement data");
        Check(std::abs(a.position.x) < 18.0f && std::abs(a.position.z) <= 22.0f,
              "An instance escaped the terrain");
        Check(Near(a.position.y, rc::TerrainHeight(a.position.x, a.position.z) - .035f),
              "An instance is detached from its terrain placement height");
        Check(a.yaw >= 0.0f && a.yaw <= 2.0f * std::numbers::pi_v<float>,
              "An instance rotation is outside a complete turn");
        if (a.model == "rock_a" || a.model == "rock_b" || a.model == "rock_c")
            Check(!a.vegetation && a.size >= .55f && a.size <= 1.35f,
                  "Rock scale or classification is incorrect");
        else if (a.model == "fern_a" || a.model == "fern_b")
            Check(a.vegetation && a.size >= .6f && a.size <= 1.25f,
                  "Fern scale or classification is incorrect");
        else if (a.model == "pine")
            Check(a.vegetation && a.size >= 3.8f && a.size <= 6.3f,
                  "Pine scale or classification is incorrect");
        else
            Check(a.model == "trunk" && !a.vegetation && a.size >= 3.0f && a.size <= 4.2f,
                  "Unknown model or incorrect trunk scale");
        ++counts[a.model];
    }
    Check(counts["rock_a"] == 14 && counts["rock_b"] == 14 && counts["rock_c"] == 14 &&
              counts["fern_a"] == 50 && counts["fern_b"] == 50 && counts["pine"] == 18 &&
              counts["trunk"] == 3,
          "Scene model counts changed unexpectedly");
}

rc::Options Parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> owned{"rainy-creek", "--assets", "assets"};
    for (const auto* argument : arguments)
        owned.emplace_back(argument);
    std::vector<char*> pointers;
    for (auto& argument : owned)
        pointers.push_back(argument.data());
    return rc::ParseOptions(static_cast<int>(pointers.size()), pointers.data());
}

void Options() {
    const auto valid = Parse({"--assets", "임시/비오는 숲", "--time", "2", "--size", "960x640",
                              "--preset", "dusk", "--frames", "5"});
    Check(rc::ToUtf8(valid.assets) == "임시/비오는 숲" && valid.fixedTime &&
              Near(*valid.fixedTime, 2.0) && valid.width == 960 && valid.height == 640 &&
              valid.lighting == rc::Lighting::Dusk && valid.frames == 5,
          "Valid command-line options were not preserved");
    Check(Parse({"--help"}).help, "Help option was ignored");
    Check(Parse({"--validate-assets"}).validate, "Asset validation option was ignored");
    const auto smoke = Parse({"--smoke-test"});
    Check(smoke.smoke && smoke.frames == 72, "Smoke test did not select a bounded run");
    const auto capture = Parse({"--capture", "capture.png"});
    Check(capture.frames > 0 && rc::ToUtf8(capture.capture) == "capture.png",
          "Capture did not select a bounded run");
    Check(Parse({"--capture", "capture.ppm"}).frames > 0, "PPM capture was rejected");
    for (const auto preset : {"night", "day"})
        Check(Parse({"--preset", preset}).lighting ==
                  (std::string(preset) == "night" ? rc::Lighting::Night : rc::Lighting::Day),
              "Valid lighting preset was rejected");

    const auto rejects = [](std::initializer_list<const char*> arguments) {
        bool rejected = false;
        try {
            (void)Parse(arguments);
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected, "Invalid command-line options were accepted");
    };
    rejects({"--frames", "0"});
    rejects({"--frames", "100001"});
    rejects({"--frames", "3junk"});
    rejects({"--time", "nan"});
    rejects({"--time", "inf"});
    rejects({"--time", "-1"});
    rejects({"--time", "86401"});
    rejects({"--size", "bad"});
    rejects({"--size", "960x640x2"});
    rejects({"--size", "20x20"});
    rejects({"--preset", "unknown"});
    rejects({"--capture", "capture.jpg"});
    rejects({"--time"});
    rejects({"--unknown"});
}

int RunStateTests(int, char**) {
    try {
        SimulationTime();
        SettingsCommands();
        DeterministicPlacement();
        Options();
        std::cout << "Rainy Creek state and option tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

} // namespace

LUX_ENGINE_MAIN(RunStateTests)
