#include <LuxUI/Framework.hpp>
#include <algorithm>
#include <cmath>

namespace LuxUI {

MessageStore::MessageStore(size_t capacity)
    : capacity_(std::clamp(capacity, size_t{1}, size_t{100000})) {}

void MessageStore::Add(Severity severity, std::string_view category, std::string_view text) {
    std::lock_guard lock(mutex_);
    messages_.push_back({++sequence_, severity, std::chrono::system_clock::now(),
                         std::string(category.substr(0, 128)), std::string(text.substr(0, 16384))});
    while (messages_.size() > capacity_)
        messages_.pop_front();
}

std::vector<Message> MessageStore::Snapshot(unsigned long long after) const {
    std::lock_guard lock(mutex_);
    const auto first = std::upper_bound(messages_.begin(), messages_.end(), after,
                                        [](unsigned long long sequence, const Message& message) {
                                            return sequence < message.sequence;
                                        });
    return {first, messages_.end()};
}

unsigned long long MessageStore::FirstSequence() const {
    std::lock_guard lock(mutex_);
    return messages_.empty() ? sequence_ + 1 : messages_.front().sequence;
}

void MessageStore::Clear() {
    std::lock_guard lock(mutex_);
    messages_.clear();
}

bool CommandRegistry::Register(Command command) {
    if (command.id.empty() || command.title.empty() || !command.execute)
        return false;
    const auto id = command.id;
    return commands_.emplace(id, std::move(command)).second;
}

bool CommandRegistry::Remove(std::string_view id) {
    const auto it = commands_.find(id);
    if (it == commands_.end())
        return false;
    commands_.erase(it);
    return true;
}

bool CommandRegistry::Execute(std::string_view id) const {
    const auto it = commands_.find(id);
    if (it == commands_.end())
        return false;
    const auto command = it->second;
    if (command.enabled && !command.enabled())
        return false;
    command.execute();
    return true;
}

std::vector<std::pair<std::string, std::string>> CommandRegistry::Commands() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [id, command] : commands_)
        result.emplace_back(id, command.title);
    return result;
}

void FrameHistory::Push(FrameSample sample) {
    if (!std::isfinite(sample.updateMs) || !std::isfinite(sample.renderMs) ||
        !std::isfinite(sample.updateMs + sample.renderMs) || sample.updateMs < 0 ||
        sample.renderMs < 0)
        return;
    if (!std::isfinite(sample.gpuMs) || sample.gpuMs < 0)
        sample.gpuMs = -1;
    samples_.push_back(sample);
    if (samples_.size() > 240)
        samples_.pop_front();
}

std::vector<FrameSample> FrameHistory::Samples() const {
    return {samples_.begin(), samples_.end()};
}

double FrameHistory::MeanCpuMs() const {
    double mean = 0;
    size_t count = 0;
    for (const auto& sample : samples_)
        mean += (sample.updateMs + sample.renderMs - mean) / ++count;
    return mean;
}

double FrameHistory::Percentile95CpuMs() const {
    if (samples_.empty())
        return 0;
    std::vector<double> values;
    for (const auto& sample : samples_)
        values.push_back(sample.updateMs + sample.renderMs);
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(std::ceil(values.size() * .95)) - 1];
}

void FrameHistory::Clear() {
    samples_.clear();
}

} // namespace LuxUI
