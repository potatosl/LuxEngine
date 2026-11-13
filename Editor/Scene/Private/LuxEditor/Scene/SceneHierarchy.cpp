#include <LuxEditor/Scene/SceneHierarchy.hpp>
#include <QDropEvent>
#include <algorithm>

namespace LuxEditor {
SceneHierarchy::SceneHierarchy(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(1);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
}

void SceneHierarchy::dropEvent(QDropEvent* event) {
    if (event->source() != this || !reparent) {
        event->ignore();
        return;
    }
    auto* target = itemAt(event->position().toPoint());
    if (target && dropIndicatorPosition() != QAbstractItemView::OnItem)
        target = target->parent();
    const auto parent = target ? target->data(0, Qt::UserRole).toUInt() : 0;
    std::vector<LuxEngine::uint32> roots;
    for (auto* item : selectedItems()) {
        bool descendant = false;
        for (auto* ancestor = item->parent(); ancestor; ancestor = ancestor->parent())
            if (ancestor->isSelected()) {
                descendant = true;
                break;
            }
        if (!descendant)
            roots.push_back(item->data(0, Qt::UserRole).toUInt());
    }
    if (roots.empty() || std::find(roots.begin(), roots.end(), parent) != roots.end()) {
        event->ignore();
        return;
    }
    reparent(roots, parent);
    event->acceptProposedAction();
}
} // namespace LuxEditor
