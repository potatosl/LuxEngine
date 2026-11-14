#include <LuxEditor/Scene/SceneHistory.hpp>

namespace LuxEditor {
size_t SceneHistoryBytes(const EditorSnapshot& snapshot) {
    return static_cast<size_t>(snapshot.scene.size()) +
           snapshot.selection.size() * sizeof(LuxEngine::uint32) +
           snapshot.animationTimes.size() * sizeof(decltype(snapshot.animationTimes)::value_type);
}

SceneEditCommand::SceneEditCommand(LuxUI::String name, std::function<void()> undo,
                                   std::function<void()> redo,
                                   std::vector<EditorDocument::TabId> tabs, size_t bytes)
    : LuxUI::UndoCommand(std::move(name)), undo_(std::move(undo)), redo_(std::move(redo)),
      tabs_(std::move(tabs)), bytes_(bytes) {}

void SceneEditCommand::undo() {
    if (undo_)
        undo_();
}

void SceneEditCommand::redo() {
    if (first_)
        first_ = false;
    else if (redo_)
        redo_();
}

bool SceneEditCommand::Touches(EditorDocument::TabId id) const {
    return std::find(tabs_.begin(), tabs_.end(), id) != tabs_.end();
}

size_t SceneEditCommand::Bytes() const noexcept {
    return bytes_;
}

void SceneEditCommand::Discard() {
    undo_ = {};
    redo_ = {};
    bytes_ = 0;
    setObsolete(true);
}
} // namespace LuxEditor
