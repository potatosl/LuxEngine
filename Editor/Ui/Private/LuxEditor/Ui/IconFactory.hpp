#pragma once
#include <QColor>
#include <QIcon>

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
QIcon MakeEditorIcon(EditorIcon icon, QColor color = QColor(214, 223, 231));
} // namespace LuxEditor
