#pragma once

#include <LuxUI/Framework.hpp>
#include <LuxUI/Widgets.hpp>
#include <memory>

namespace LuxUI {

class ConsolePanel {
public:
    explicit ConsolePanel(Widget* parent);
    ~ConsolePanel();
    Widget* View() const;
    // UI-thread API. Rendering is coalesced into the next event-loop iteration.
    void Append(Severity severity, std::string_view category, std::string_view text);
    void SetCommandHandler(std::function<void(std::string)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ProfilerPanel {
public:
    explicit ProfilerPanel(Widget* parent);
    ~ProfilerPanel();
    Widget* View() const;
    void Sample(FrameSample sample);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace LuxUI
