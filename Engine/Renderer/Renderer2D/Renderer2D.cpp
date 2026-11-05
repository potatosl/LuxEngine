#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void Renderer2D::Init() {
        Log::Info("Renderer2D initialized.");
    }

    void Renderer2D::Shutdown() {
        Log::Info("Renderer2D shutdown.");
    }

    void Renderer2D::BeginScene() {
    }

    void Renderer2D::EndScene() {
    }

} // namespace LuxEngine
