#include "Engine/Renderer/Renderer3D/Renderer3D.hpp"
#include "Engine/Core/Log.hpp"

namespace LuxEngine {

    void Renderer3D::Init() {
        Log::Info("Renderer3D initialized.");
    }

    void Renderer3D::Shutdown() {
        Log::Info("Renderer3D shutdown.");
    }

    void Renderer3D::BeginScene() {
    }

    void Renderer3D::EndScene() {
    }

} // namespace LuxEngine
