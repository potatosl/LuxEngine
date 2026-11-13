#pragma once
#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/EditorSnapshot.hpp>
#include <QUndoCommand>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace LuxEditor {
inline size_t SceneHistoryBytes(const EditorSnapshot& snapshot) {
    return static_cast<size_t>(snapshot.scene.size()) +
           snapshot.animationTimes.size() * sizeof(decltype(snapshot.animationTimes)::value_type);
}
class SceneEditCommand final : public QUndoCommand {
public:
    SceneEditCommand(QString name, std::function<void()> undo, std::function<void()> redo,
                     std::vector<EditorDocument::TabId> tabs = {}, size_t bytes = 0)
        : QUndoCommand(std::move(name)), undo_(std::move(undo)), redo_(std::move(redo)),
          tabs_(std::move(tabs)), bytes_(bytes) {}
    void undo() override {
        if (undo_)
            undo_();
    }
    void redo() override {
        if (first_)
            first_ = false;
        else if (redo_)
            redo_();
    }
    bool Touches(EditorDocument::TabId id) const {
        return std::find(tabs_.begin(), tabs_.end(), id) != tabs_.end();
    }
    size_t Bytes() const noexcept {
        return bytes_;
    }
    void Discard() {
        undo_ = {};
        redo_ = {};
        bytes_ = 0;
        setObsolete(true);
    }

private:
    std::function<void()> undo_, redo_;
    std::vector<EditorDocument::TabId> tabs_;
    bool first_ = true;
    size_t bytes_ = 0;
};
} // namespace LuxEditor
