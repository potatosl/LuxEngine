#pragma once
#include <LuxUI/Widgets.hpp>
#include <cstdint>
#include <functional>
#include <vector>

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {
class DebugPanel final : public LuxUI::Widget {
public:
    DebugPanel(LuxUI::Widget* parent, std::function<void(uint32_t)> onSelect);
    void AddLog(const LuxUI::String& message, bool error);
    void UpdateScene(LuxEngine::Scene& scene, uint32_t selectedId, uint64_t frame, double frameMs);

private:
    void FilterLogs();

    struct LogEntry {
        LuxUI::String message;
        bool error;
    };

    std::vector<LogEntry> logs_;
    LuxUI::ComboBox* filter_ = nullptr;
    LuxUI::LineEdit* search_ = nullptr;
    LuxUI::PlainTextEdit* output_ = nullptr;
    LuxUI::TreeWidget* state_ = nullptr;
    LuxUI::Label* metrics_ = nullptr;
    std::function<void(uint32_t)> onSelect_;
};
} // namespace LuxEditor
