#pragma once
#include "Engine/Core/Core.hpp"
#include <QByteArray>
#include <utility>
#include <vector>

namespace LuxEngine {
// Editing cursors belong to undo history, not to the persisted game scene format.
struct EditorSnapshot {
    QByteArray scene;
    std::vector<std::pair<uint32, float>> animationTimes;

    bool isEmpty() const {
        return scene.isEmpty();
    }
    void clear() {
        scene.clear();
        animationTimes.clear();
    }
    bool operator==(const EditorSnapshot&) const = default;
};
} // namespace LuxEngine
