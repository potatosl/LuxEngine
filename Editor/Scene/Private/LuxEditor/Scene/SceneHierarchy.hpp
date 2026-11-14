#pragma once
#include <LuxEngine/Core/Core.hpp>
#include <LuxUI/Widgets.hpp>
#include <functional>
#include <vector>

namespace LuxEditor {
class SceneHierarchy final : public LuxUI::TreeWidget {
public:
    explicit SceneHierarchy(LuxUI::Widget* parent = nullptr);
    std::function<void(const std::vector<LuxEngine::uint32>&, LuxEngine::uint32)> reparent;

protected:
    void dropEvent(LuxUI::DropEvent* event) override;
};
} // namespace LuxEditor
