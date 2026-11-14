#pragma once
#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/EditorSnapshot.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace LuxEditor {
size_t SceneHistoryBytes(const EditorSnapshot& snapshot);

class SceneEditCommand final : public LuxUI::UndoCommand {
public:
    SceneEditCommand(LuxUI::String name, std::function<void()> undo, std::function<void()> redo,
                     std::vector<EditorDocument::TabId> tabs = {}, size_t bytes = 0);

    void undo() override;

    void redo() override;

    bool Touches(EditorDocument::TabId id) const;

    size_t Bytes() const noexcept;

    void Discard();

private:
    std::function<void()> undo_, redo_;
    std::vector<EditorDocument::TabId> tabs_;
    bool first_ = true;
    size_t bytes_ = 0;
};
} // namespace LuxEditor
