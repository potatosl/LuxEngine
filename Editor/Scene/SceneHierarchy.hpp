#pragma once
#include "Engine/Core/Core.hpp"
#include <QTreeWidget>
#include <functional>
#include <vector>

namespace LuxEditor {
class SceneHierarchy final : public QTreeWidget {
public:
    explicit SceneHierarchy(QWidget* parent = nullptr);
    std::function<void(const std::vector<LuxEngine::uint32>&, LuxEngine::uint32)> reparent;

protected:
    void dropEvent(QDropEvent* event) override;
};
} // namespace LuxEditor
