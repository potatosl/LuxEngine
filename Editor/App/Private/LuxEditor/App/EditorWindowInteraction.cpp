#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Scene/SceneHistory.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>
#include <unordered_set>

namespace LuxEditor {
namespace {
bool SameSceneLifetime(const std::weak_ptr<int>& original, const LuxEngine::Scene* scene) {
    if (!scene || original.expired())
        return false;
    const auto current = scene->Lifetime();
    return !original.owner_before(current) && !current.owner_before(original);
}
} // namespace

void EditorWindow::RefreshSelection(bool hierarchy) {
    if (auto* space = findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("transformSpace"))) {
        const LuxUI::SignalBlocker blocker(space);
        space->setCurrentIndex(static_cast<int>(document_.GetTransformSpace()));
    }
    if (auto* pivot = findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("selectionPivot"))) {
        const LuxUI::SignalBlocker blocker(pivot);
        pivot->setCurrentIndex(static_cast<int>(document_.GetSelectionPivotMode()));
    }
    if (hierarchy)
        RefreshHierarchy();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    if (animationPreview2D_)
        animationPreview2D_->RefreshScene();
    if (animationPreview3D_)
        animationPreview3D_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::SelectEntity(std::optional<LuxEngine::uint32> id, bool syncHierarchy) {
    StopAnimationPlayback();
    const auto requested = document_.GetSelection();
    if (!viewportEditBefore_.isEmpty() && requested != viewportEditBefore_.selection) {
        CancelViewportEdit();
        document_.SetSelection(requested, id);
    }
    // Viewports update the complete selection before notifying the window. Keep it when the
    // notified primary already matches; older single-selection callers still replace it.
    if (document_.GetSelectedId(document_.GetActiveTabId()) != id)
        document_.Select(id);
    RefreshSelection(syncHierarchy);
}

void EditorWindow::SyncHierarchySelection() {
    if (refreshingHierarchy_)
        return;
    const auto items = hierarchy_->selectedItems();
    std::unordered_set<LuxEngine::uint32> visibleSelection;
    for (auto* item : items)
        visibleSelection.insert(item->data(0, LuxUI::Flags::UserRole).toUInt());
    auto ids = document_.GetSelection();
    std::erase_if(ids, [&](auto id) { return !visibleSelection.contains(id); });
    for (auto* item : items) {
        const auto id = item->data(0, LuxUI::Flags::UserRole).toUInt();
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    std::optional<LuxEngine::uint32> primary;
    if (auto* current = hierarchy_->currentItem(); current && current->isSelected())
        primary = current->data(0, LuxUI::Flags::UserRole).toUInt();
    const bool canceled = !viewportEditBefore_.isEmpty();
    CancelViewportEdit();
    StopAnimationPlayback();
    document_.SetSelection(ids, primary);
    RefreshSelection(canceled);
}

void EditorWindow::BeginViewportEdit() {
    if (restoringHistory_)
        return;
    CancelViewportEdit();
    StopAnimationPlayback();
    EndTimelineEdit();
    viewportEditBefore_ = SnapshotScene();
    if (viewportEditBefore_.isEmpty())
        return;
    viewportEditTab_ = document_.GetActiveTabId();
    viewportEditLifetime_ = document_.GetScene().Lifetime();
    viewportEditWasDirty_ = document_.IsDirty();
    transformRejected_ = false;
    if (!viewportEditWasDirty_)
        cleanScenes_[viewportEditTab_] = viewportEditBefore_;
    viewportEditSelected_ = document_.GetSelectedId(viewportEditTab_);
    selectionTransform_.Begin(document_);
    UpdateChrome();
}

void EditorWindow::ApplyViewportTransform(LuxEngine::uint32 id,
                                          const LuxEngine::TransformComponent& value) {
    if (viewportEditBefore_.isEmpty())
        return;
    if (viewportEditTab_ != document_.GetActiveTabId() ||
        !SameSceneLifetime(viewportEditLifetime_, document_.GetScene(viewportEditTab_)) ||
        document_.GetSelectedId(viewportEditTab_) != id) {
        CancelViewportEdit();
        return;
    }
    if (!selectionTransform_.Apply(document_.GetScene(), id, value)) {
        if (!transformRejected_) {
            statusBar()->showMessage(
                LUX_UI_TEXT("이 변환은 현재 Transform 형식으로 표현할 수 없습니다. 로컬 좌표 "
                            "또는 균일 크기를 사용해 주세요."),
                5000);
            transformRejected_ = true;
        }
        return;
    }
    document_.MarkDirty();
    RefreshInspector();
    canvas_->RefreshScene();
    canvas3D_->RefreshScene();
    overviewCanvas_->RefreshScene();
    UpdateChrome();
}

void EditorWindow::CancelViewportEdit() {
    if (viewportEditBefore_.isEmpty()) {
        selectionTransform_.End();
        return;
    }
    const auto before = viewportEditBefore_;
    const auto tab = viewportEditTab_;
    const auto selected = viewportEditSelected_;
    const bool dirty = viewportEditWasDirty_;
    const bool valid =
        SameSceneLifetime(viewportEditLifetime_, document_.GetScene(viewportEditTab_));
    viewportEditBefore_.clear();
    viewportEditSelected_.reset();
    viewportEditTab_ = 0;
    selectionTransform_.End();
    // A load/replacement may have invalidated the original world. Never overwrite a new scene
    // with the canceled gesture's old data.
    if (!valid || !document_.GetScene(tab)) {
        UpdateChrome();
        return;
    }
    const auto current = SnapshotScene(tab);
    if (current.scene != before.scene || current.animationTimes != before.animationTimes)
        RestoreSceneAt(tab, before, selected, false);
    else
        document_.SetSelection(tab, before.selection, selected);
    document_.SetDirty(tab, dirty);
    if (document_.GetActiveTabId() == tab)
        RefreshSelection();
}

void EditorWindow::EndViewportEdit() {
    if (viewportEditBefore_.isEmpty())
        return;
    if (viewportEditTab_ != document_.GetActiveTabId() ||
        !SameSceneLifetime(viewportEditLifetime_, document_.GetScene(viewportEditTab_))) {
        CancelViewportEdit();
        return;
    }
    const auto after = SnapshotScene();
    const auto before = viewportEditBefore_;
    const auto previous = viewportEditSelected_;
    const auto next = document_.GetSelectedId(viewportEditTab_);
    const auto tab = viewportEditTab_;
    const bool dirty = viewportEditWasDirty_;
    viewportEditBefore_.clear();
    viewportEditSelected_.reset();
    viewportEditTab_ = 0;
    selectionTransform_.End();
    if (after.isEmpty()) {
        RestoreSceneAt(tab, before, previous, false);
        document_.SetDirty(tab, dirty);
        return;
    }
    if (before != after) {
        const char* names[] = {"선택", "이동", "회전", "크기", "타일 붓", "타일 지우개"};
        const LuxUI::String title =
            LuxUI::String::fromUtf8(names[static_cast<int>(tool_)]) + LUX_UI_TEXT(" 도구");
        PushSceneCommand(new SceneEditCommand(
            title, [this, tab, before, previous] { RestoreSceneAt(tab, before, previous); },
            [this, tab, after, next] { RestoreSceneAt(tab, after, next); }, {tab},
            SceneHistoryBytes(before) + SceneHistoryBytes(after)));
        UpdateSceneDirty(tab, after);
    } else {
        document_.SetDirty(tab, dirty);
    }
    RefreshSelection();
}
} // namespace LuxEditor
