#include "PointerCaptureBackend.hpp"
#include "pointer-constraints-client-protocol.h"
#include "relative-pointer-client-protocol.h"
#include <QGuiApplication>
#include <QPointer>
#include <QWaylandClientExtension>
#include <QtGui/qguiapplication_platform.h>
#include <algorithm>
#include <utility>
#include <wayland-client.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 10, 0)
#error "The optional public Wayland pointer backend requires Qt 6.10 or newer."
#endif

namespace LuxUI::Detail {
#if QT_CONFIG(wayland)
namespace {

class ProtocolGlobal final : public QWaylandClientExtension {
public:
    ProtocolGlobal(const wl_interface* interface, void (*destroy)(void*))
        : QWaylandClientExtension(1), interface_(interface), destroy_(destroy) {
        connect(this, &QWaylandClientExtension::activeChanged, this, [this] {
            if (!isActive() && proxy_) {
                destroy_(std::exchange(proxy_, nullptr));
                if (lost)
                    lost();
            }
        });
        initialize();
    }

    ~ProtocolGlobal() override {
        if (proxy_)
            destroy_(proxy_);
    }

    const wl_interface* extensionInterface() const override {
        return interface_;
    }

    void bind(wl_registry* registry, int id, int version) override {
        proxy_ = wl_registry_bind(registry, static_cast<uint32_t>(id), interface_,
                                  static_cast<uint32_t>(std::min(1, version)));
    }

    void* Proxy() const {
        return isActive() ? proxy_ : nullptr;
    }

    std::function<void()> lost;

private:
    const wl_interface* interface_;
    void (*destroy_)(void*);
    void* proxy_ = nullptr;
};

class WaylandPointerCapture;
WaylandPointerCapture* activeCapture = nullptr;

class WaylandPointerCapture final : public NativePointerCapture {
public:
    WaylandPointerCapture()
        : relativeManager_(&zwp_relative_pointer_manager_v1_interface,
                           [](void* pointer) {
                               zwp_relative_pointer_manager_v1_destroy(
                                   static_cast<zwp_relative_pointer_manager_v1*>(pointer));
                           }),
          constraints_(&zwp_pointer_constraints_v1_interface, [](void* pointer) {
              zwp_pointer_constraints_v1_destroy(static_cast<zwp_pointer_constraints_v1*>(pointer));
          }) {
        relativeManager_.lost = [this] { Lost(); };
        constraints_.lost = [this] { Lost(); };
    }

    ~WaylandPointerCapture() override {
        End();
        DestroyPointer();
    }

    bool Begin(Widget* target, std::function<void(PointF)> motion, std::function<void()> locked,
               std::function<void()> released) override {
        End();
        if (!target || !qGuiApp || !relativeManager_.Proxy() || !constraints_.Proxy())
            return false;
        auto* application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!application)
            return false;
        auto* seat =
            application->lastInputSeat() ? application->lastInputSeat() : application->seat();
        if (!seat)
            return false;
        if (activeCapture && activeCapture != this)
            activeCapture->Lost();
        // Each gesture starts with a fresh enter/focus snapshot. Reusing an old cached surface
        // could lock a different window while Qt and our pointer process a focus transition.
        DestroyPointer();
        seat_ = seat;
        pointer_ = wl_seat_get_pointer(seat);
        if (!pointer_)
            return false;
        ListenPointer();
        target_ = target;
        motion_ = std::move(motion);
        lockedCallback_ = std::move(locked);
        released_ = std::move(released);
        requested_ = true;
        activeCapture = this;
        // enter on this dedicated wl_pointer provides the surface. The Qt-owned pointer and
        // its listener remain untouched; all objects use Qt's public extension/event queue.
        TryLock();
        return true;
    }

    void End() override {
        requested_ = false;
        locked_ = false;
        if (activeCapture == this)
            activeCapture = nullptr;
        if (relative_)
            zwp_relative_pointer_v1_destroy(std::exchange(relative_, nullptr));
        if (lock_)
            zwp_locked_pointer_v1_destroy(std::exchange(lock_, nullptr));
        target_ = nullptr;
        motion_ = {};
        lockedCallback_ = {};
        released_ = {};
        DestroyPointer();
    }

private:
    void DestroyPointer() {
        surface_ = nullptr;
        seat_ = nullptr;
        if (!pointer_)
            return;
        if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer_)) >=
            WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(pointer_);
        else
            wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }

    void Lost() {
        if (!requested_)
            return;
        auto callback = released_;
        End();
        // The owner may destroy this backend from its capture-lost callback.
        if (callback)
            callback();
    }

    void TryLock() {
        if (!requested_ || lock_ || !surface_ || !pointer_ || !target_ || !constraints_.Proxy() ||
            !relativeManager_.Proxy())
            return;
        lock_ = zwp_pointer_constraints_v1_lock_pointer(
            static_cast<zwp_pointer_constraints_v1*>(constraints_.Proxy()), surface_, pointer_,
            nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        if (!lock_) {
            Lost();
            return;
        }
        static const zwp_locked_pointer_v1_listener lockListener{
            [](void* data, zwp_locked_pointer_v1* pointer) {
                auto* self = static_cast<WaylandPointerCapture*>(data);
                if (!self->requested_ || pointer != self->lock_)
                    return;
                self->locked_ = true;
                auto callback = self->lockedCallback_;
                if (callback)
                    callback();
            },
            [](void* data, zwp_locked_pointer_v1* pointer) {
                auto* self = static_cast<WaylandPointerCapture*>(data);
                if (pointer == self->lock_)
                    self->Lost();
            }};
        zwp_locked_pointer_v1_add_listener(lock_, &lockListener, this);
        relative_ = zwp_relative_pointer_manager_v1_get_relative_pointer(
            static_cast<zwp_relative_pointer_manager_v1*>(relativeManager_.Proxy()), pointer_);
        if (!relative_) {
            Lost();
            return;
        }
        static const zwp_relative_pointer_v1_listener relativeListener{
            [](void* data, zwp_relative_pointer_v1* pointer, uint32_t, uint32_t, wl_fixed_t dx,
               wl_fixed_t dy, wl_fixed_t, wl_fixed_t) {
                auto* self = static_cast<WaylandPointerCapture*>(data);
                if (!self->requested_ || !self->locked_ || pointer != self->relative_ ||
                    !self->target_)
                    return;
                auto callback = self->motion_;
                // Accelerated deltas are in the same logical units as wl_pointer.motion.
                // Do not apply DPI scaling or device acceleration a second time.
                if (callback)
                    callback({wl_fixed_to_double(dx), wl_fixed_to_double(dy)});
            }};
        zwp_relative_pointer_v1_add_listener(relative_, &relativeListener, this);
    }

    void ListenPointer() {
        static const wl_pointer_listener listener = [] {
            wl_pointer_listener value{};
            value.enter = [](void* data, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t,
                             wl_fixed_t) {
                auto* self = static_cast<WaylandPointerCapture*>(data);
                self->surface_ = surface;
                self->TryLock();
            };
            value.leave = [](void* data, wl_pointer*, uint32_t, wl_surface* surface) {
                auto* self = static_cast<WaylandPointerCapture*>(data);
                if (self->surface_ == surface) {
                    self->surface_ = nullptr;
                    self->Lost();
                }
            };
            value.motion = [](void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) {};
            value.button = [](void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {};
            value.axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {};
#if defined(WL_POINTER_FRAME_SINCE_VERSION)
            value.frame = [](void*, wl_pointer*) {};
            value.axis_source = [](void*, wl_pointer*, uint32_t) {};
            value.axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {};
            value.axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {};
#endif
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
            value.axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) {};
#endif
#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
            value.axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {};
#endif
#if defined(WL_POINTER_WARP_SINCE_VERSION)
            value.warp = [](void*, wl_pointer*, wl_fixed_t, wl_fixed_t) {};
#endif
            return value;
        }();
        wl_pointer_add_listener(pointer_, &listener, this);
    }

    ProtocolGlobal relativeManager_;
    ProtocolGlobal constraints_;
    QPointer<QWidget> target_;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_surface* surface_ = nullptr;
    zwp_relative_pointer_v1* relative_ = nullptr;
    zwp_locked_pointer_v1* lock_ = nullptr;
    bool requested_ = false;
    bool locked_ = false;
    std::function<void(PointF)> motion_;
    std::function<void()> lockedCallback_;
    std::function<void()> released_;
};
} // namespace

std::unique_ptr<NativePointerCapture> CreateWaylandPointerCapture() {
    if (!qGuiApp || !QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
        return nullptr;
    return std::make_unique<WaylandPointerCapture>();
}
#else
std::unique_ptr<NativePointerCapture> CreateWaylandPointerCapture() {
    return nullptr;
}
#endif
} // namespace LuxUI::Detail
