#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace LuxUI {

enum class Severity { Info, Warning, Error };

struct Message {
    unsigned long long sequence = 0;
    Severity severity = Severity::Info;
    std::chrono::system_clock::time_point timestamp;
    std::string category;
    std::string text;
};

// Bounded, thread-safe store; widgets poll snapshots on their own UI thread.
class MessageStore {
public:
    explicit MessageStore(size_t capacity = 2000);
    void Add(Severity severity, std::string_view category, std::string_view text);
    std::vector<Message> Snapshot(unsigned long long after = 0) const;
    unsigned long long FirstSequence() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::deque<Message> messages_;
    size_t capacity_;
    unsigned long long sequence_ = 0;
};

struct Command {
    std::string id;
    std::string title;
    std::function<void()> execute;
    std::function<bool()> enabled;
};

class CommandRegistry {
public:
    bool Register(Command command);
    bool Remove(std::string_view id);
    bool Execute(std::string_view id) const;
    std::vector<std::pair<std::string, std::string>> Commands() const;

private:
    std::map<std::string, Command, std::less<>> commands_;
};

struct FrameSample {
    double updateMs = 0;
    double renderMs = 0;
    double gpuMs = -1;
    unsigned drawCalls = 0;
    unsigned long long gpuBytes = 0;
};

class FrameHistory {
public:
    void Push(FrameSample sample);
    std::vector<FrameSample> Samples() const;
    double MeanCpuMs() const;
    double Percentile95CpuMs() const;
    void Clear();

private:
    std::deque<FrameSample> samples_;
};

} // namespace LuxUI
