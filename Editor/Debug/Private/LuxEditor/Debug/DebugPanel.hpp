#pragma once
#include <QStringList>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;

namespace LuxEngine {
class Scene;
}

namespace LuxEditor {
class DebugPanel final : public QWidget {
public:
    DebugPanel(QWidget* parent, std::function<void(uint32_t)> onSelect);
    void AddLog(const QString& message, bool error);
    void UpdateScene(LuxEngine::Scene& scene, uint32_t selectedId, uint64_t frame, double frameMs);

private:
    void FilterLogs();
    struct LogEntry {
        QString message;
        bool error;
    };
    std::vector<LogEntry> logs_;
    QComboBox* filter_ = nullptr;
    QLineEdit* search_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QTreeWidget* state_ = nullptr;
    QLabel* metrics_ = nullptr;
    std::function<void(uint32_t)> onSelect_;
};
} // namespace LuxEditor
