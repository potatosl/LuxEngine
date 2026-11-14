#pragma once
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
enum class EditorIcon {
    Select,
    Move,
    Rotate,
    Scale,
    NewFile,
    Open,
    Save,
    Sprite,
    Mesh,
    Search,
    Grid,
    List,
    Focus,
    Folder,
    Console,
    Delete,
    Brush,
    Eraser,
    Play,
    Pause,
    Step,
    Stop,
    Bug
};
LuxUI::Icon MakeBrandIcon(bool dark);
LuxUI::Icon MakeEditorIcon(EditorIcon icon, LuxUI::Color color = LuxUI::Color(214, 223, 231));
} // namespace LuxEditor
