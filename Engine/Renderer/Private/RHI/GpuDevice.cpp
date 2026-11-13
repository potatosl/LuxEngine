#include "RHI/RenderBackend.hpp"
#include <LuxEngine/Platform/Window.hpp>
#include <LuxEngine/Renderer/RHI/GpuDevice.hpp>
#include <LuxEngine/Renderer/Renderer2D/Renderer2D.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>

namespace LuxEngine {
namespace {
const std::string NoError;
const GpuDeviceInfo NoInfo;
} // namespace

const char* ToString(GpuStatus status) {
    switch (status) {
    case GpuStatus::Uninitialized:
        return "Uninitialized";
    case GpuStatus::Ready:
        return "Ready";
    case GpuStatus::OutOfMemory:
        return "OutOfMemory";
    case GpuStatus::SurfaceLost:
        return "SurfaceLost";
    case GpuStatus::Timeout:
        return "Timeout";
    case GpuStatus::DeviceLost:
        return "DeviceLost";
    case GpuStatus::Error:
        return "Error";
    }
    return "Unknown";
}

GpuDevice::~GpuDevice() {
    Shutdown();
}

bool GpuDevice::Initialize(Window& window, const GpuDeviceConfig& config) {
    if (backend_ && backend_->GetStatus() != GpuStatus::Uninitialized)
        return false;
    backend_.reset();
    if (!window.GetNativeHandle())
        return false;
    // The backend stays attached after a failed start so GetLastError reports the cause.
    backend_ = Rhi::CreateRenderBackend(config);
    return backend_ && backend_->Initialize(window, config);
}

void GpuDevice::Shutdown() {
    Renderer2D::DetachDevice(this);
    Renderer3D::DetachDevice(this);
    if (!backend_)
        return;
    backend_->Shutdown();
    backend_.reset();
}

bool GpuDevice::IsReady() const {
    return backend_ && backend_->Usable();
}

GpuStatus GpuDevice::GetStatus() const {
    return backend_ ? backend_->GetStatus() : GpuStatus::Uninitialized;
}

GpuStatus GpuDevice::GetLastFailure() const {
    return backend_ ? backend_->GetLastFailure() : GpuStatus::Ready;
}

const std::string& GpuDevice::GetLastError() const {
    return backend_ ? backend_->GetLastError() : NoError;
}

const GpuDeviceInfo& GpuDevice::GetInfo() const {
    return backend_ ? backend_->GetInfo() : NoInfo;
}

GpuFrameStats GpuDevice::GetFrameStats() const {
    return backend_ ? backend_->GetFrameStats() : GpuFrameStats{};
}

GpuDeviceCounters GpuDevice::GetCounters() const {
    return backend_ ? backend_->GetCounters() : GpuDeviceCounters{};
}

GpuMemoryStats GpuDevice::GetMemoryStats() const {
    return backend_ ? backend_->GetMemoryStats() : GpuMemoryStats{};
}

GpuDevice::FrameStart GpuDevice::BeginFrame() {
    return backend_ ? backend_->BeginFrame() : FrameStart::Error;
}

bool GpuDevice::Clear(float r, float g, float b, float a) {
    return backend_ && backend_->Clear(r, g, b, a);
}

bool GpuDevice::DrawTriangles(std::span<const RenderVertex> vertices, const Mat4& viewProjection,
                              GpuTexture* texture, bool depth) {
    if (texture && !texture->BelongsTo(*this))
        return false;
    return DrawTexturedTriangles(vertices, viewProjection, texture ? texture->handle_ : 0, depth);
}

bool GpuDevice::DrawTexturedTriangles(std::span<const RenderVertex> vertices,
                                      const Mat4& viewProjection, uint64 textureHandle,
                                      bool depth) {
    return backend_ &&
           backend_->DrawTexturedTriangles(vertices, viewProjection, textureHandle, depth);
}

bool GpuDevice::SetLighting(const GpuLighting& lighting) {
    return backend_ && backend_->SetLighting(lighting);
}

bool GpuDevice::DrawIndexed(const GpuBuffer& vertices, const GpuBuffer& indices, uint32 indexCount,
                            const Mat4& viewProjection, const Mat4& model, const Vec4& albedo,
                            GpuTexture* texture, float metallic, float roughness, bool pbr) {
    if (!backend_ || indexCount == 0 || indexCount % 3 != 0 ||
        vertices.type_ != BufferType::Vertex || indices.type_ != BufferType::Index ||
        !vertices.BelongsTo(*this) || !indices.BelongsTo(*this) ||
        static_cast<uint64>(indexCount) * sizeof(uint32) > indices.sizeBytes_)
        return false;
    if (texture && !texture->BelongsTo(*this))
        return false;
    return backend_->DrawIndexed(vertices.handle_, indices.handle_, indexCount, viewProjection,
                                 model, albedo, texture ? texture->handle_ : 0, metallic, roughness,
                                 pbr);
}

bool GpuDevice::EndFrame() {
    return backend_ && backend_->EndFrame();
}

bool GpuDevice::SetVSync(bool enabled) {
    return backend_ && backend_->SetVSync(enabled);
}

void GpuDevice::NotifyResize() {
    if (backend_)
        backend_->NotifyResize();
}

bool GpuDevice::FlushUploads() {
    return backend_ && backend_->FlushUploads();
}

bool GpuDevice::WaitIdle() {
    return backend_ && backend_->WaitIdle();
}

bool GpuDevice::RequestFrameCapture() {
    return backend_ && backend_->RequestFrameCapture();
}

bool GpuDevice::ReadFrameCapture(GpuFrameCapture& capture) {
    return backend_ && backend_->ReadFrameCapture(capture);
}

GpuDevice::GpuDevice() = default;
} // namespace LuxEngine
