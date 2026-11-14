#include <LuxUI/Framework.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

int main() {
    LuxUI::MessageStore messages(16);
    std::thread writer([&] {
        for (int i = 0; i < 100; ++i)
            messages.Add(LuxUI::Severity::Warning, "test", "message");
    });
    writer.join();
    const auto snapshot = messages.Snapshot();
    if (snapshot.size() != 16 || !messages.Snapshot(snapshot.back().sequence).empty())
        return 1;
    if (messages.FirstSequence() != snapshot.front().sequence ||
        messages.Snapshot(snapshot[snapshot.size() - 2].sequence).size() != 1)
        return 7;
    messages.Clear();
    if (!messages.Snapshot().empty())
        return 2;
    messages.Add(LuxUI::Severity::Info, "next", "after clear");
    if (messages.Snapshot().front().sequence <= snapshot.back().sequence)
        return 8;
    LuxUI::CommandRegistry commands;
    bool enabled = false;
    int count = 0;
    if (!commands.Register({"tool", "Tool", [&] { ++count; }, [&] { return enabled; }}))
        return 3;
    if (commands.Execute("tool") || commands.Register({"tool", "Duplicate", [] {}, {}}))
        return 4;
    enabled = true;
    if (!commands.Execute("tool") || count != 1 || !commands.Remove("tool"))
        return 5;
    commands.Register({"self-remove", "Remove self", [&] { commands.Remove("self-remove"); }, {}});
    if (!commands.Execute("self-remove") || commands.Execute("self-remove"))
        return 9;
    LuxUI::FrameHistory history;
    for (int i = 0; i < 300; ++i)
        history.Push({2, 3, -1, 1, 64});
    history.Push({std::numeric_limits<double>::quiet_NaN(), 0});
    if (history.Samples().size() != 240 || history.MeanCpuMs() != 5 ||
        history.Percentile95CpuMs() != 5)
        return 6;
    history.Clear();
    for (int i = 0; i < 240; ++i)
        history.Push({1.0e307, 0, std::numeric_limits<double>::quiet_NaN()});
    if (!std::isfinite(history.MeanCpuMs()) || history.Samples().back().gpuMs != -1)
        return 10;
    return 0;
}
