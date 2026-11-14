#pragma once

#include <LuxUI/Widgets.hpp>

namespace LuxUI {
// Asset rows store absolute UTF-8 paths in UserRole. Dragging emits file URLs.
class AssetList final : public ListWidget {
public:
    explicit AssetList(Widget* parent);

private:
    void startDrag(Flags::DropActions actions) override;
};
} // namespace LuxUI
