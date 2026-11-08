#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include <array>
#include <memory>
#include <stdexcept>
namespace LuxEngine {
namespace {
struct Batch {
    std::weak_ptr<Vk::DeviceState> owner;
    uint64 texture = 0;
    std::vector<RenderVertex> vertices;
};
GpuDevice* device = nullptr;
Mat4 viewProjection;
std::vector<Batch> batches;
size_t usedBatches = 0;
Renderer2D::Statistics stats;
bool active = false;
bool SameOwner(const std::weak_ptr<Vk::DeviceState>& a, const std::weak_ptr<Vk::DeviceState>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}
void Quad(const Vec3& position, const Vec2& size, float angle,
          const std::weak_ptr<Vk::DeviceState>& owner, uint64 texture, const Vec4& color) {
    if (!active)
        return;
    if (!usedBatches || batches[usedBatches - 1].texture != texture ||
        !SameOwner(batches[usedBatches - 1].owner, owner) ||
        batches[usedBatches - 1].vertices.size() >= 6000) {
        if (usedBatches == batches.size())
            batches.emplace_back();
        auto& batch = batches[usedBatches++];
        batch.owner = owner;
        batch.texture = texture;
        batch.vertices.clear();
        batch.vertices.reserve(6000);
    }
    const float c = std::cos(angle), s = std::sin(angle);
    const std::array<Vec2, 4> corners{{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}}};
    const std::array<Vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    for (auto i : {0, 1, 2, 2, 3, 0}) {
        const float x = corners[i].x * size.x, y = corners[i].y * size.y;
        batches[usedBatches - 1].vertices.push_back(
            {{position.x + c * x - s * y, position.y + s * x + c * y, position.z}, color, uv[i]});
    }
    ++stats.quadCount;
}
} // namespace
OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top) {
    SetProjection(left, right, bottom, top);
}

void OrthographicCamera::SetProjection(float left, float right, float bottom, float top) {
    projectionMatrix_ = Mat4::Orthographic(left, right, bottom, top, -1.0f, 1.0f);
    RecalculateMatrix();
}

void OrthographicCamera::RecalculateMatrix() {
    viewMatrix_ =
        Mat4::RotateZ(-rotation_) * Mat4::Translate(Vec3(-position_.x, -position_.y, -position_.z));
    viewProjectionMatrix_ = projectionMatrix_ * viewMatrix_;
}

void Renderer2D::Init(GpuDevice* gpu) {
    if (device == gpu && !active) {
        for (size_t i = 0; i < usedBatches; ++i)
            batches[i].owner.reset();
        usedBatches = 0;
        stats = {};
        return;
    }
    Shutdown();
    device = gpu;
}
void Renderer2D::Shutdown() {
    batches.clear();
    usedBatches = 0;
    device = nullptr;
    active = false;
    stats = {};
}
void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    if (active)
        throw std::logic_error("2D scene already active");
    viewProjection = camera.GetViewProjectionMatrix();
    usedBatches = 0;
    stats = {};
    active = true;
}
void Renderer2D::EndScene() {
    if (!active)
        return;
    active = false;
    for (size_t i = 0; i < usedBatches; ++i) {
        auto& batch = batches[i];
        const bool sameDevice = !batch.texture || (device && batch.owner.lock() == device->state_);
        if (device && sameDevice &&
            device->DrawTexturedTriangles(batch.vertices, viewProjection, batch.texture, false))
            ++stats.drawCalls;
        batch.owner.reset();
    }
    usedBatches = 0;
}
void Renderer2D::DrawQuad(const Vec2& p, const Vec2& size, const Vec4& color) {
    Quad({p.x, p.y, 0}, size, 0, {}, 0, color);
}
void Renderer2D::DrawQuad(const Vec3& p, const Vec2& size, const Vec4& color) {
    Quad(p, size, 0, {}, 0, color);
}
void Renderer2D::DrawRotatedQuad(const Vec2& p, const Vec2& size, float angle, const Vec4& color) {
    Quad({p.x, p.y, 0}, size, angle, {}, 0, color);
}
void Renderer2D::DrawSprite(const Vec2& p, const Vec2& size, GpuTexture* texture,
                            const Vec4& tint) {
    if (!texture || !texture->IsValid())
        return;
    Quad({p.x, p.y, 0}, size, 0, texture->owner_, texture->handle_, tint);
}
void Renderer2D::ResetStats() {
    stats = {};
}
Renderer2D::Statistics Renderer2D::GetStats() {
    return stats;
}
} // namespace LuxEngine
