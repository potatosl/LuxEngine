#pragma once

#include <LuxEngine/Application.hpp>
#include <RainyCreek/CreekRenderer.hpp>
#include <RainyCreek/CreekState.hpp>
#include <RainyCreek/Options.hpp>
#include <RainyCreek/OrbitController.hpp>
#include <chrono>
#include <memory>

namespace RainyCreek {

class CreekApplication final : public LuxEngine::Application {
public:
    CreekApplication(Options options, Catalog catalog);
    ~CreekApplication() override;
    int RunSample();
    void OnEvent(LuxEngine::Event& event) override;

private:
    void OnInit() override;
    void OnUpdate(double dt) override;
    void OnRender(float elapsedSeconds) override;
    void OnShutdown() override;
    void Execute(Command command);
    void SmokeStep();
    Options options_;
    Catalog catalog_;
    CreekState state_;
    OrbitController orbit_;
    CreekRenderer renderer_;
    unsigned rendered_ = 0;
    bool captureReady_ = false;
    bool orbiting_ = false;
    bool panning_ = false;
    float mouseX_ = 0;
    float mouseY_ = 0;
    std::chrono::steady_clock::time_point environmentWait_;
};

int RunRainyCreek(int argc, char** argv);

} // namespace RainyCreek
