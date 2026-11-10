#include "Engine/Renderer/Renderer2D/Renderer2D.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace LuxEngine {
namespace {
struct Batch {
    std::weak_ptr<Vk::DeviceState> owner;
    Ref<GpuTexture> retainedTexture;
    uint64 texture = 0;
    std::vector<RenderVertex> vertices;
};

bool SameOwner(const std::weak_ptr<Vk::DeviceState>& a, const std::weak_ptr<Vk::DeviceState>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

thread_local Renderer2D::Context* t_ActiveContext2D = nullptr;
std::recursive_mutex s_DefaultContext2DMutex;

Renderer2D::Context& DefaultContext2D() {
    static Renderer2D::Context s_Default;
    return s_Default;
}
} // namespace

struct Renderer2D::Context::Impl {
    GpuDevice* device = nullptr;
    std::weak_ptr<Vk::DeviceState> deviceOwner;
    Mat4 viewProjection;
    std::vector<Batch> batches;
    size_t usedBatches = 0;
    Renderer2D::Statistics stats;
    bool active = false;
    bool depthTest = false;

    std::shared_ptr<Vk::DeviceState> LockValidState() {
        if (!device)
            return nullptr;
        auto locked = deviceOwner.lock();
        if (!locked || !locked->Usable()) {
            device = nullptr;
            deviceOwner.reset();
            return nullptr;
        }
        return locked;
    }

    void Quad(const Vec3& position, const Vec2& size, float angle,
              const std::weak_ptr<Vk::DeviceState>& owner, uint64 texture, const Vec4& color) {
        if (!active)
            return;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z) || !std::isfinite(size.x) || !std::isfinite(size.y) ||
            !std::isfinite(angle) || !std::isfinite(color.x) || !std::isfinite(color.y) ||
            !std::isfinite(color.z) || !std::isfinite(color.w))
            return;
        if (!usedBatches || batches[usedBatches - 1].texture != texture ||
            !SameOwner(batches[usedBatches - 1].owner, owner) ||
            batches[usedBatches - 1].vertices.size() >= 6000) {
            if (usedBatches == batches.size())
                batches.emplace_back();
            auto& batch = batches[usedBatches++];
            batch.retainedTexture.reset();
            batch.owner = owner;
            batch.texture = texture;
            batch.vertices.clear();
            batch.vertices.reserve(6000);
        }
        const float c = std::cos(angle), s = std::sin(angle);
        const std::array<Vec2, 4> corners{
            {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}}};
        const std::array<Vec2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
        for (auto i : {0, 1, 2, 2, 3, 0}) {
            const float x = corners[i].x * size.x, y = corners[i].y * size.y;
            batches[usedBatches - 1].vertices.push_back(
                {{position.x + c * x - s * y, position.y + s * x + c * y, position.z},
                 color,
                 uv[i]});
        }
        ++stats.quadCount;
    }
};

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

Renderer2D::Context::Context() : impl_(std::make_unique<Impl>()) {}

Renderer2D::Context::Context(GpuDevice* gpu) : impl_(std::make_unique<Impl>()) {
    Init(gpu);
}

Renderer2D::Context::~Context() {
    Shutdown();
}

Renderer2D::Context::Context(Context&& other) noexcept = default;
Renderer2D::Context& Renderer2D::Context::operator=(Context&& other) noexcept = default;

void Renderer2D::Context::Init(GpuDevice* gpu) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    const std::weak_ptr<Vk::DeviceState> nextOwner =
        gpu ? std::weak_ptr<Vk::DeviceState>(gpu->state_) : std::weak_ptr<Vk::DeviceState>{};
    if (impl_->device == gpu && SameOwner(impl_->deviceOwner, nextOwner) && !impl_->active) {
        for (size_t i = 0; i < impl_->usedBatches; ++i) {
            impl_->batches[i].owner.reset();
            impl_->batches[i].retainedTexture.reset();
        }
        impl_->usedBatches = 0;
        impl_->stats = {};
        return;
    }
    Shutdown();
    impl_->device = gpu;
    impl_->deviceOwner = nextOwner;
}

void Renderer2D::Context::Shutdown() {
    if (!impl_)
        return;
    impl_->batches.clear();
    impl_->usedBatches = 0;
    impl_->device = nullptr;
    impl_->deviceOwner.reset();
    impl_->active = false;
    impl_->stats = {};
}

void Renderer2D::Context::DetachDevice(const GpuDevice* gpu) {
    if (!impl_ || !gpu)
        return;
    if (impl_->device == gpu) {
        AbortScene();
        impl_->device = nullptr;
        impl_->deviceOwner.reset();
    }
}

bool Renderer2D::Context::IsDeviceValid() const {
    if (!impl_ || !impl_->device)
        return false;
    auto locked = impl_->deviceOwner.lock();
    return locked && locked->Usable();
}

GpuDevice* Renderer2D::Context::GetDevice() const {
    return IsDeviceValid() ? impl_->device : nullptr;
}

void Renderer2D::Context::BeginScene(const OrthographicCamera& camera) {
    BeginScene(camera.GetViewProjectionMatrix());
}

void Renderer2D::Context::BeginScene(const Mat4& viewProjection, bool depthTest) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    if (impl_->active)
        throw std::logic_error("2D scene already active");
    impl_->viewProjection = viewProjection;
    impl_->depthTest = depthTest;
    impl_->usedBatches = 0;
    impl_->stats = {};
    impl_->active = true;
}

void Renderer2D::Context::EndScene() {
    if (!impl_ || !impl_->active)
        return;
    impl_->active = false;
    auto lockedState = impl_->LockValidState();
    GpuDevice* activeDevice = lockedState ? impl_->device : nullptr;
    for (size_t i = 0; i < impl_->usedBatches; ++i) {
        auto& batch = impl_->batches[i];
        const bool sameDevice =
            !batch.texture || (activeDevice && batch.owner.lock() == lockedState);
        if (activeDevice && sameDevice &&
            activeDevice->DrawTexturedTriangles(batch.vertices, impl_->viewProjection,
                                                batch.texture, impl_->depthTest, true))
            ++impl_->stats.drawCalls;
        batch.owner.reset();
        batch.retainedTexture.reset();
    }
    impl_->usedBatches = 0;
}

void Renderer2D::Context::AbortScene() {
    if (!impl_)
        return;
    impl_->active = false;
    for (size_t i = 0; i < impl_->usedBatches; ++i) {
        impl_->batches[i].owner.reset();
        impl_->batches[i].retainedTexture.reset();
    }
    impl_->usedBatches = 0;
}

void Renderer2D::Context::DrawQuad(const Vec2& p, const Vec2& size, const Vec4& color) {
    if (impl_)
        impl_->Quad({p.x, p.y, 0}, size, 0, {}, 0, color);
}

void Renderer2D::Context::DrawQuad(const Vec3& p, const Vec2& size, const Vec4& color) {
    if (impl_)
        impl_->Quad(p, size, 0, {}, 0, color);
}

void Renderer2D::Context::DrawRotatedQuad(const Vec2& p, const Vec2& size, float angle,
                                          const Vec4& color) {
    if (impl_)
        impl_->Quad({p.x, p.y, 0}, size, angle, {}, 0, color);
}

void Renderer2D::Context::DrawSprite(const Vec2& p, const Vec2& size, GpuTexture* texture,
                                     const Vec4& tint) {
    DrawRotatedSprite(p, size, 0.0f, texture, tint);
}

void Renderer2D::Context::DrawRotatedSprite(const Vec2& p, const Vec2& size, float angle,
                                           GpuTexture* texture, const Vec4& tint) {
    DrawRotatedSprite(Vec3{p.x, p.y, 0.0f}, size, angle, texture, tint);
}

void Renderer2D::Context::DrawRotatedSprite(const Vec3& p, const Vec2& size, float angle,
                                           GpuTexture* texture, const Vec4& tint) {
    if (!impl_ || !texture || !texture->IsValid())
        return;
    impl_->Quad(p, size, angle, texture->owner_, texture->handle_, tint);
}

void Renderer2D::Context::DrawRotatedSprite(const Vec2& p, const Vec2& size, float angle,
                                           const Ref<GpuTexture>& texture, const Vec4& tint) {
    DrawRotatedSprite(Vec3{p.x, p.y, 0.0f}, size, angle, texture, tint);
}

void Renderer2D::Context::DrawRotatedSprite(const Vec3& p, const Vec2& size, float angle,
                                           const Ref<GpuTexture>& texture, const Vec4& tint) {
    if (!impl_)
        return;
    const auto quads = impl_->stats.quadCount;
    DrawRotatedSprite(p, size, angle, texture.get(), tint);
    if (impl_->stats.quadCount != quads)
        impl_->batches[impl_->usedBatches - 1].retainedTexture = texture;
}

void Renderer2D::Context::ResetStats() {
    if (impl_)
        impl_->stats = {};
}

Renderer2D::Statistics Renderer2D::Context::GetStats() const {
    return impl_ ? impl_->stats : Statistics{};
}

Renderer2D::ScopedContext::ScopedContext(Context& context)
    : previous_(t_ActiveContext2D), current_(&context) {
    t_ActiveContext2D = current_;
}

Renderer2D::ScopedContext::~ScopedContext() {
    if (current_)
        current_->AbortScene();
    t_ActiveContext2D = previous_;
}

void Renderer2D::Init(GpuDevice* gpu) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->Init(gpu);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().Init(gpu);
}

void Renderer2D::Shutdown() {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->Shutdown();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().Shutdown();
}

void Renderer2D::DetachDevice(const GpuDevice* gpu) {
    if (t_ActiveContext2D)
        t_ActiveContext2D->DetachDevice(gpu);
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DetachDevice(gpu);
}

GpuDevice* Renderer2D::GetDevice() {
    if (t_ActiveContext2D)
        return t_ActiveContext2D->GetDevice();
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    return DefaultContext2D().GetDevice();
}

void Renderer2D::BeginScene(const OrthographicCamera& camera) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->BeginScene(camera);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().BeginScene(camera);
}

void Renderer2D::BeginScene(const Mat4& viewProjection, bool depthTest) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->BeginScene(viewProjection, depthTest);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().BeginScene(viewProjection, depthTest);
}

void Renderer2D::EndScene() {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->EndScene();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().EndScene();
}

void Renderer2D::DrawQuad(const Vec2& p, const Vec2& size, const Vec4& color) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawQuad(p, size, color);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawQuad(p, size, color);
}

void Renderer2D::DrawQuad(const Vec3& p, const Vec2& size, const Vec4& color) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawQuad(p, size, color);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawQuad(p, size, color);
}

void Renderer2D::DrawRotatedQuad(const Vec2& p, const Vec2& size, float angle, const Vec4& color) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawRotatedQuad(p, size, angle, color);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawRotatedQuad(p, size, angle, color);
}

void Renderer2D::DrawSprite(const Vec2& p, const Vec2& size, GpuTexture* texture,
                            const Vec4& tint) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawSprite(p, size, texture, tint);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawSprite(p, size, texture, tint);
}

void Renderer2D::DrawRotatedSprite(const Vec2& p, const Vec2& size, float angle,
                                   GpuTexture* texture, const Vec4& tint) {
    DrawRotatedSprite(Vec3{p.x, p.y, 0.0f}, size, angle, texture, tint);
}

void Renderer2D::DrawRotatedSprite(const Vec3& p, const Vec2& size, float angle,
                                   GpuTexture* texture, const Vec4& tint) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawRotatedSprite(p, size, angle, texture, tint);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawRotatedSprite(p, size, angle, texture, tint);
}

void Renderer2D::DrawRotatedSprite(const Vec2& p, const Vec2& size, float angle,
                                   const Ref<GpuTexture>& texture, const Vec4& tint) {
    DrawRotatedSprite(Vec3{p.x, p.y, 0.0f}, size, angle, texture, tint);
}

void Renderer2D::DrawRotatedSprite(const Vec3& p, const Vec2& size, float angle,
                                   const Ref<GpuTexture>& texture, const Vec4& tint) {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->DrawRotatedSprite(p, size, angle, texture, tint);
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().DrawRotatedSprite(p, size, angle, texture, tint);
}

void Renderer2D::ResetStats() {
    if (t_ActiveContext2D) {
        t_ActiveContext2D->ResetStats();
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    DefaultContext2D().ResetStats();
}

Renderer2D::Statistics Renderer2D::GetStats() {
    if (t_ActiveContext2D)
        return t_ActiveContext2D->GetStats();
    std::lock_guard<std::recursive_mutex> lock(s_DefaultContext2DMutex);
    return DefaultContext2D().GetStats();
}
} // namespace LuxEngine
