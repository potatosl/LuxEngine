#include "PointerCaptureBackend.hpp"
#include <LuxUI/PointerCapture.hpp>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <cmath>
#include <cstdint>

namespace LuxUI {

Detail::NativePointerCapture::~NativePointerCapture() = default;

struct PointerCapture::Impl final : QObject {
    QPointer<QWidget> target;
    PointerCaptureMode mode = PointerCaptureMode::Inactive;
    std::function<void(PointF)> motion;
    std::function<void()> released;
    std::unique_ptr<Detail::NativePointerCapture> native;
    QTimer timeout;
    std::optional<PointF> last;
    QPoint savedPosition;
    QCursor savedCursor;
    bool explicitCursor = false;
    bool ending = false;
    bool starting = false;
    bool warpPending = false;
    bool injected = false;
    std::uint64_t generation = 0;
    QString limitation;

    Impl() {
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, this, [this] {
            if (mode != PointerCaptureMode::Pending)
                return;
            native->End();
            mode = PointerCaptureMode::Limited;
            limitation = QStringLiteral("컴포지터가 상대 포인터 잠금을 허용하지 않았습니다.");
            RestoreCursor();
            last.reset();
        });
    }

    ~Impl() override {
        Stop(false);
    }

    void RestoreCursor() {
        if (!target)
            return;
        if (explicitCursor)
            target->setCursor(savedCursor);
        else
            target->unsetCursor();
    }

    void Stop(bool notify) {
        if (ending || mode == PointerCaptureMode::Inactive)
            return;
        ending = true;
        starting = false;
        ++generation;
        const bool restorePosition = mode == PointerCaptureMode::Recentered;
        auto callback = notify ? released : std::function<void()>{};
        mode = PointerCaptureMode::Inactive;
        timeout.stop();
        if (native)
            native->End();
        if (target) {
            target->removeEventFilter(this);
            QObject::disconnect(target, nullptr, this, nullptr);
            if (QWidget::mouseGrabber() == target)
                target->releaseMouse();
            RestoreCursor();
            if (restorePosition)
                QCursor::setPos(savedPosition);
        }
        target = nullptr;
        motion = {};
        released = {};
        last.reset();
        warpPending = false;
        limitation.clear();
        ending = false;
        // No member access follows this callback: it may destroy the viewport/capture owner.
        if (callback) {
            try {
                callback();
            } catch (...) {
            }
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        QPointer<QObject> watchedGuard(watched);
        if (watched == target && event->type() == QEvent::UngrabMouse && starting)
            return false;
        if (watched == target &&
            (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
             event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse))
            Stop(true);
        return !watchedGuard;
    }

    void Relative(PointF delta) {
        if (mode != PointerCaptureMode::Relative || !target || !std::isfinite(delta.x()) ||
            !std::isfinite(delta.y()))
            return;
        auto callback = motion;
        QPointer<Impl> guard(this);
        if (callback) {
            try {
                callback(delta);
            } catch (...) {
                if (guard)
                    guard->Stop(true);
            }
        }
    }
};

PointerCapture::PointerCapture() : impl_(std::make_unique<Impl>()) {}

PointerCapture::PointerCapture(std::unique_ptr<Detail::NativePointerCapture> native)
    : impl_(std::make_unique<Impl>()) {
    impl_->native = std::move(native);
    impl_->injected = true;
}

PointerCapture::~PointerCapture() = default;

bool PointerCapture::Begin(Widget* target, std::function<void(PointF)> relativeMotion,
                           std::function<void()> released) {
    impl_->Stop(false);
    if (!target || !target->isVisible() || !target->isEnabled())
        return false;
    impl_->target = target;
    impl_->motion = std::move(relativeMotion);
    impl_->released = std::move(released);
    impl_->savedPosition = QCursor::pos();
    impl_->savedCursor = target->cursor();
    impl_->explicitCursor = target->testAttribute(Qt::WA_SetCursor);
    impl_->mode = PointerCaptureMode::Limited;
    impl_->limitation =
        QStringLiteral("현재 플랫폼은 무제한 상대 포인터 이동을 지원하지 않습니다.");
    impl_->last.reset();
    const auto generation = ++impl_->generation;
    target->installEventFilter(impl_.get());
    QObject::connect(target, &QObject::destroyed, impl_.get(),
                     [state = impl_.get()] { state->Stop(false); });
    const auto platform = QGuiApplication::platformName();
    if (impl_->injected || platform.startsWith(QStringLiteral("wayland"))) {
#if defined(LUX_UI_WAYLAND_CAPTURE)
        if (!impl_->native)
            impl_->native = Detail::CreateWaylandPointerCapture();
#endif
        auto* state = impl_.get();
        const QPointer<Impl> guard(state);
        state->mode = PointerCaptureMode::Pending;
        const bool started =
            state->native &&
            state->native->Begin(
                target,
                [guard, generation](PointF delta) {
                    if (guard && guard->generation == generation)
                        guard->Relative(delta);
                },
                [guard, generation] {
                    if (guard && guard->generation == generation &&
                        guard->mode == PointerCaptureMode::Pending && guard->target) {
                        guard->timeout.stop();
                        guard->mode = PointerCaptureMode::Relative;
                        guard->limitation.clear();
                        guard->target->setCursor(Qt::BlankCursor);
                    }
                },
                [guard, generation] {
                    if (guard && guard->generation == generation &&
                        (guard->mode == PointerCaptureMode::Pending ||
                         guard->mode == PointerCaptureMode::Relative))
                        guard->Stop(true);
                });
        // Native implementations may synchronously lock/release and delete the capture owner.
        // Old-session callbacks must not revive this session or overwrite an immediate lock.
        if (!guard || guard->generation != generation || !guard->target)
            return false;
        if (started) {
            if (guard->mode == PointerCaptureMode::Pending) {
                guard->limitation =
                    QStringLiteral("컴포지터의 상대 포인터 잠금을 기다리고 있습니다.");
                guard->timeout.start(750);
            }
            return guard->mode == PointerCaptureMode::Pending ||
                   guard->mode == PointerCaptureMode::Relative;
        }
        if (guard->native)
            guard->native->End();
        ++guard->generation;
        guard->mode = PointerCaptureMode::Limited;
        guard->limitation =
            QStringLiteral("컴포지터에 상대 포인터 또는 포인터 잠금 프로토콜이 없습니다.");
        return false;
    }
    if (platform == QStringLiteral("xcb") || platform == QStringLiteral("windows") ||
        platform == QStringLiteral("cocoa")) {
        const auto center = target->mapToGlobal(target->rect().center());
        QCursor::setPos(center);
        if ((QCursor::pos() - center).manhattanLength() <= 2) {
            impl_->mode = PointerCaptureMode::Recentered;
            impl_->limitation.clear();
            impl_->warpPending = true;
            impl_->starting = true;
            const QPointer<Impl> guard(impl_.get());
            target->grabMouse(Qt::BlankCursor);
            if (!guard || guard->generation != generation)
                return false;
            guard->starting = false;
            return guard->mode == PointerCaptureMode::Recentered;
        }
    }
    return false;
}

std::unique_ptr<PointerCapture>
Detail::PointerCaptureTestAccess::Create(std::unique_ptr<Detail::NativePointerCapture> native) {
    return std::unique_ptr<PointerCapture>(new PointerCapture(std::move(native)));
}

void PointerCapture::End() {
    impl_->Stop(false);
}

PointerCaptureMode PointerCapture::GetMode() const {
    return impl_->mode;
}

bool PointerCapture::HasUnboundedMotion() const {
    return impl_->mode == PointerCaptureMode::Relative ||
           impl_->mode == PointerCaptureMode::Recentered;
}

String PointerCapture::GetLimitation() const {
    return impl_->limitation;
}

std::optional<PointF> PointerCapture::ConsumeMotion(PointF position) {
    if (!impl_->target || !std::isfinite(position.x()) || !std::isfinite(position.y()))
        return std::nullopt;
    if (impl_->mode == PointerCaptureMode::Recentered) {
        const auto center = impl_->target->rect().center();
        const auto delta = position - PointF(center);
        // A logical centre can round to a neighbouring fractional position at noninteger DPI.
        // Consume one pending warp echo instead of warping again and creating an event loop.
        if (impl_->warpPending && std::abs(delta.x()) <= .75 && std::abs(delta.y()) <= .75) {
            impl_->warpPending = false;
            return std::nullopt;
        }
        if (std::hypot(delta.x(), delta.y()) < .01)
            return std::nullopt;
        impl_->warpPending = true;
        QCursor::setPos(impl_->target->mapToGlobal(center));
        return delta;
    }
    if (impl_->mode == PointerCaptureMode::Limited) {
        const auto previous = impl_->last;
        impl_->last = position;
        return previous ? std::optional<PointF>(position - *previous) : std::nullopt;
    }
    return std::nullopt;
}
} // namespace LuxUI
