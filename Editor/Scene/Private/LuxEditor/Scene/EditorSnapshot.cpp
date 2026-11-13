#include <LuxEditor/Scene/EditorSnapshot.hpp>

namespace LuxEditor {
bool EditorSnapshot::isEmpty() const {
    return scene.isEmpty();
}

void EditorSnapshot::clear() {
    scene.clear();
    animationTimes.clear();
}
} // namespace LuxEditor

namespace LuxEditor {
bool EditorSnapshot::operator==(const EditorSnapshot&) const = default;
}
