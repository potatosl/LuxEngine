#include <LuxUI/PointerCapture.hpp>
#include <PointerCaptureBackend.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace LuxUI;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct FakeState {
    enum class Immediate { None, Locked, Released };
    bool supported = true;
    Immediate immediate = Immediate::None;
    int ends = 0;
    std::function<void(PointF)> motion;
    std::function<void()> locked;
    std::function<void()> released;
};

class FakeNative final : public Detail::NativePointerCapture {
public:
    explicit FakeNative(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}

    bool Begin(Widget*, std::function<void(PointF)> motion, std::function<void()> locked,
               std::function<void()> released) override {
        // Callback execution can destroy this FakeNative. Keep the independent state and
        // result on the stack, and never access members after invoking a synchronous callback.
        const auto state = state_;
        const bool result = state->supported;
        state->motion = std::move(motion);
        state->locked = std::move(locked);
        state->released = std::move(released);
        if (state->immediate == FakeState::Immediate::Locked)
            state->locked();
        if (state->immediate == FakeState::Immediate::Released)
            state->released();
        return result;
    }

    void End() override {
        ++state_->ends;
    }

private:
    std::shared_ptr<FakeState> state_;
};

std::unique_ptr<PointerCapture> Capture(const std::shared_ptr<FakeState>& state) {
    return Detail::PointerCaptureTestAccess::Create(std::make_unique<FakeNative>(state));
}

void Wait(int milliseconds) {
    EventLoop loop;
    Timer::singleShot(milliseconds, &loop, &EventLoop::quit);
    loop.exec();
}

void Lifecycle() {
    Widget widget;
    widget.resize(320, 240);
    widget.show();
    Application::processEvents();
    auto state = std::make_shared<FakeState>();
    auto capture = Capture(state);
    int releases = 0, movements = 0;
    PointF measured;
    const auto begin = [&] {
        return capture->Begin(
            &widget,
            [&](PointF delta) {
                measured = delta;
                ++movements;
            },
            [&] { ++releases; });
    };
    Check(begin() && capture->GetMode() == PointerCaptureMode::Pending &&
              !capture->HasUnboundedMotion() && !capture->ConsumeMotion({20, 30}),
          "Pending pointer lock was falsely advertised as active or counted absolute motion");
    const auto oldLocked = state->locked;
    const auto oldMotion = state->motion;
    const auto oldReleased = state->released;
    state->motion({2, 3});
    Check(movements == 0, "Relative motion arrived before the compositor confirmed pointer lock");
    state->locked();
    Check(capture->HasUnboundedMotion() && capture->GetMode() == PointerCaptureMode::Relative &&
              capture->GetLimitation().isEmpty() && !capture->ConsumeMotion({40, 50}),
          "Confirmed native relative capture did not suppress duplicate absolute mouse motion");
    state->motion({1.25, -2.5});
    Check(movements == 1 && measured == PointF(1.25, -2.5),
          "Native accelerated logical deltas were rescaled or rounded");
    state->motion({std::numeric_limits<double>::quiet_NaN(), 0});
    Check(movements == 1, "Non-finite relative motion reached the editor camera");
    capture->End();
    oldLocked();
    oldMotion({10, 10});
    oldReleased();
    Check(capture->GetMode() == PointerCaptureMode::Inactive && releases == 0 && movements == 1,
          "Explicit release notified recursively or a late native event revived the old capture");
    Check(begin(), "Pointer capture could not be restarted");
    oldLocked();
    oldMotion({10, 10});
    oldReleased();
    Check(capture->GetMode() == PointerCaptureMode::Pending && releases == 0 && movements == 1,
          "Callbacks from a previous gesture changed the new capture generation");
    state->locked();
    FocusEvent focus(Event::FocusOut);
    Application::sendEvent(&widget, &focus);
    Check(capture->GetMode() == PointerCaptureMode::Inactive && releases == 1,
          "Focus loss failed to release the pointer exactly once");
    Check(begin(), "Pointer capture restart after focus loss failed");
    state->locked();
    state->released();
    state->released();
    Check(capture->GetMode() == PointerCaptureMode::Inactive && releases == 2,
          "Native unlock did not release capture exactly once");
    Check(begin(), "Pointer capture restart before destruction failed");
    const auto afterDestruction = state->locked;
    const auto motionAfterDestruction = state->motion;
    const auto releaseAfterDestruction = state->released;
    capture.reset();
    afterDestruction();
    motionAfterDestruction({10, 10});
    releaseAfterDestruction();
    Check(releases == 2 && movements == 1,
          "Capture destruction invoked user callbacks or retained a dangling native callback");
}

void ImmediateAndTimeout() {
    Widget widget;
    widget.resize(320, 240);
    widget.show();
    Application::processEvents();
    auto state = std::make_shared<FakeState>();
    state->immediate = FakeState::Immediate::Locked;
    auto capture = Capture(state);
    Check(capture->Begin(&widget, {}) && capture->GetMode() == PointerCaptureMode::Relative &&
              capture->GetLimitation().isEmpty(),
          "Synchronous native lock was overwritten by Pending state");
    Wait(850);
    Check(capture->GetMode() == PointerCaptureMode::Relative,
          "An unnecessary negotiation timeout cancelled a synchronous lock");
    capture->End();
    state->immediate = FakeState::Immediate::Released;
    Check(!capture->Begin(&widget, {}, [&] { capture.reset(); }) && !capture,
          "Synchronous native release did not tolerate capture owner destruction during Begin");
    state = std::make_shared<FakeState>();
    capture = Capture(state);
    int releases = 0;
    Check(capture->Begin(&widget, {}, [&] { ++releases; }), "Pending timeout fixture failed");
    Wait(850);
    Check(capture->GetMode() == PointerCaptureMode::Limited && !capture->HasUnboundedMotion() &&
              !capture->GetLimitation().isEmpty() && releases == 0 && state->ends > 0,
          "Unsupported compositor lock did not transition honestly to bounded fallback");
    state->locked();
    Check(capture->GetMode() == PointerCaptureMode::Limited,
          "Late compositor lock revived a negotiation that had already timed out");
    Check(!capture->ConsumeMotion({10, 20}), "Limited motion did not initialize its origin");
    const auto delta = capture->ConsumeMotion({13.5, 18});
    Check(delta && *delta == PointF(3.5, -2),
          "Bounded fallback changed logical pointer sensitivity");
    capture->End();
    state->supported = false;
    Check(!capture->Begin(&widget, {}) && capture->GetMode() == PointerCaptureMode::Limited &&
              !capture->HasUnboundedMotion(),
          "Unsupported native backend was advertised as unlimited pointer support");
}

void TargetAndCallbackLifetime() {
    auto state = std::make_shared<FakeState>();
    auto capture = Capture(state);
    auto widget = std::make_unique<Widget>();
    widget->show();
    Application::processEvents();
    Check(capture->Begin(widget.get(), {}), "Target destruction fixture could not capture");
    state->locked();
    widget.reset();
    state->locked();
    state->motion({1, 2});
    Check(capture->GetMode() == PointerCaptureMode::Inactive,
          "Destroyed target retained a pointer lock or was revived by a late lock event");
    widget = std::make_unique<Widget>();
    widget->show();
    Application::processEvents();
    int releases = 0;
    Check(capture->Begin(
              widget.get(), [](PointF) { throw std::runtime_error("callback failure"); },
              [&] { ++releases; }),
          "Callback failure fixture could not capture");
    state->locked();
    state->motion({1, 2});
    Check(capture->GetMode() == PointerCaptureMode::Inactive && releases == 1,
          "A failing camera callback left the desktop pointer locked");
    Check(capture->Begin(widget.get(), [&](PointF) { capture.reset(); }),
          "Callback destruction fixture could not capture");
    state->locked();
    state->motion({1, 2});
    state->motion({1, 2});
    Check(!capture, "Motion callback could not safely destroy its own capture owner");
}

int Desktop() {
    Widget widget;
    widget.resize(320, 240);
    widget.show();
    widget.activateWindow();
    widget.setFocus();
    Application::processEvents();
    PointerCapture capture;
    int releases = 0;
    if (!capture.Begin(&widget, {}, [&] { ++releases; }) ||
        capture.GetMode() != PointerCaptureMode::Recentered) {
        std::cout << "SKIP: desktop cursor recentering is unavailable\n";
        return 77;
    }
    const PointF center(widget.rect().center());
    Check(!capture.ConsumeMotion(center + PointF(.4, -.4)),
          "Fractional-DPI warp echo was counted as real mouse input");
    const auto delta = capture.ConsumeMotion(center + PointF(15, -8));
    Check(delta && *delta == PointF(15, -8) && !capture.ConsumeMotion(center + PointF(-.4, .4)) &&
              releases == 0,
          "Desktop capture sensitivity, warp-echo filtering or implicit-to-explicit grab failed");
    capture.End();
    Check(releases == 0 && capture.GetMode() == PointerCaptureMode::Inactive,
          "Explicit desktop capture release notified recursively");
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    Application app(argc, argv);
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--desktop")
            return Desktop();
        Lifecycle();
        ImmediateAndTimeout();
        TargetAndCallbackLifetime();
        std::cout << "Pointer capture capability, sensitivity, late events and lifetime passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
