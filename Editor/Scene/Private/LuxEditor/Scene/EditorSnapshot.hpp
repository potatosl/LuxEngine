#pragma once
#include <LuxEngine/Core/Core.hpp>
#include <LuxUI/Widgets.hpp>
#include <utility>
#include <vector>

namespace LuxEditor {
// Editing cursors belong to undo history, not to the persisted game scene format.
struct EditorSnapshot {
    LuxUI::ByteArray scene;
    std::vector<LuxEngine::uint32> selection;
    std::vector<std::pair<LuxEngine::uint32, float>> animationTimes;

    bool isEmpty() const;

    void clear();

    bool operator==(const EditorSnapshot&) const;
};
} // namespace LuxEditor
