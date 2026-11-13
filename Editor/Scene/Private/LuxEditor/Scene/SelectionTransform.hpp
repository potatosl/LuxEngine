#pragma once

#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <memory>
#include <vector>

namespace LuxEditor {
// The active object supplies orientation; the selected pivot only replaces its position.
LuxEngine::Vec3 GetSelectionPivot(const EditorDocument& document);
LuxEngine::TransformComponent GetSelectionTransformPivot(const EditorDocument& document);

// A gesture always derives previews from the captured state. Failed previews are atomic and
// retain the previous valid preview; Cancel restores original local values without replacing ECS.
class SelectionTransformSession final {
public:
    bool Begin(EditorDocument& document);
    bool Apply(LuxEngine::Scene& scene, LuxEngine::uint32 activeId,
               const LuxEngine::TransformComponent& target);
    bool Cancel(LuxEngine::Scene& scene);
    void End();
    bool IsActive() const;
    bool HasChanged() const;

private:
    struct Entry {
        LuxEngine::uint32 id;
        LuxEngine::uint32 parent;
        LuxEngine::TransformComponent local;
        LuxEngine::TransformComponent world;
        LuxEngine::Mat4 matrix;
        LuxEngine::Mat4 parentMatrix;
    };

    LuxEngine::Scene* scene_ = nullptr;
    std::weak_ptr<int> lifetime_;
    LuxEngine::uint32 activeId_ = 0;
    LuxEngine::TransformComponent initial_;
    LuxEngine::Vec3 pivot_;
    TransformSpace space_ = TransformSpace::World;
    SelectionPivot pivotMode_ = SelectionPivot::Median;
    std::vector<Entry> entries_;
    bool changed_ = false;
};
} // namespace LuxEditor
