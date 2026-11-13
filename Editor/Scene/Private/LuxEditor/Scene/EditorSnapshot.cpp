#include <LuxEditor/Scene/EditorSnapshot.hpp>

namespace LuxEditor {
bool EditorSnapshot::isEmpty() const {
    return scene.isEmpty();
}

void EditorSnapshot::clear() {
    scene.clear();
    selection.clear();
    animationTimes.clear();
}
} // namespace LuxEditor

namespace LuxEditor {
bool EditorSnapshot::operator==(const EditorSnapshot& other) const {
    return scene == other.scene && animationTimes == other.animationTimes;
}
} // namespace LuxEditor
