#pragma once

#include <LuxUI/Widgets.hpp>
#include <functional>
#include <memory>
#include <optional>

namespace LuxUI {
namespace Detail {
class NativePointerCapture;
struct PointerCaptureTestAccess;
} // namespace Detail

enum class PointerCaptureMode { Inactive, Pending, Relative, Recentered, Limited };

// Owns one camera-look gesture. Native relative deltas are accelerated logical pixels; desktop
// recentering preserves that same coordinate space. Unsupported platforms report Limited.
class PointerCapture final {
public:
    PointerCapture();
    ~PointerCapture();
    PointerCapture(const PointerCapture&) = delete;
    PointerCapture& operator=(const PointerCapture&) = delete;

    // Call from the target's pointer-press handler. false means a bounded fallback is in use.
    bool Begin(Widget* target, std::function<void(PointF)> relativeMotion,
               std::function<void()> released = {});
    // Explicit release/destruction does not invoke released; actual capture/focus loss does.
    void End();
    PointerCaptureMode GetMode() const;
    bool HasUnboundedMotion() const;
    String GetLimitation() const;
    // Feed ordinary mouse moves here. Native Wayland delivery uses relativeMotion instead;
    // pending/relative captures return nullopt to prevent duplicate camera rotations.
    std::optional<PointF> ConsumeMotion(PointF localPosition);

private:
    friend struct Detail::PointerCaptureTestAccess;
    explicit PointerCapture(std::unique_ptr<Detail::NativePointerCapture> native);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace LuxUI
