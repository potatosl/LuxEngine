#pragma once

#include <LuxUI/PointerCapture.hpp>
#include <functional>
#include <memory>

namespace LuxUI::Detail {

class NativePointerCapture {
public:
    virtual ~NativePointerCapture();
    virtual bool Begin(Widget* target, std::function<void(PointF)> motion,
                       std::function<void()> locked, std::function<void()> released) = 0;
    virtual void End() = 0;
};

struct PointerCaptureTestAccess {
    static std::unique_ptr<PointerCapture> Create(std::unique_ptr<NativePointerCapture> native);
};

#if defined(LUX_UI_WAYLAND_CAPTURE)
std::unique_ptr<NativePointerCapture> CreateWaylandPointerCapture();
#endif

} // namespace LuxUI::Detail
