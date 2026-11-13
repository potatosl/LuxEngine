#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace LuxEditor;
using namespace LuxEngine;

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float first, float second) {
    return std::abs(first - second) < .001f;
}

void Position(Scene& scene, uint32 id, Vec3 value) {
    const auto actual = scene.GetWorldTransform(id).position;
    Check(Near(actual.x, value.x) && Near(actual.y, value.y) && Near(actual.z, value.z),
          "Selection transform produced an unexpected world position");
}

void SelectionState() {
    EditorDocument document;
    auto first = document.GetScene().CreateEntity("first");
    auto second = document.GetScene().CreateEntity("second");
    const auto initialTab = document.GetActiveTabId();
    document.SetSelection({first.GetId(), second.GetId(), first.GetId(), 999}, first.GetId());
    Check(document.GetSelection() == std::vector<uint32>{second.GetId(), first.GetId()},
          "Selection order, uniqueness or primary normalization failed");
    Check(document.GetSelectedId(initialTab) == first.GetId(),
          "The primary selection is incorrect");
    document.ModifySelection(first.GetId(), SelectionMode::Toggle);
    Check(document.GetSelected().GetId() == second.GetId(), "Toggle did not update the primary");
    document.ModifySelection(first.GetId(), SelectionMode::Add);
    document.ModifySelection(std::nullopt, SelectionMode::Add);
    Check(document.GetSelection().size() == 2 && document.IsSelected(first.GetId()),
          "Additive selection cleared an existing selection");
    document.SetTransformSpace(TransformSpace::Local);
    document.SetSelectionPivotMode(SelectionPivot::Individual);
    const auto nextTab = document.NewTab();
    Check(document.GetSelection().empty() && document.GetTransformSpace() == TransformSpace::World,
          "A new tab inherited another scene's selection settings");
    document.Select(initialTab, first.GetId());
    document.SetSelection(initialTab, {first.GetId(), second.GetId()}, first.GetId());
    Check(document.GetSelection().empty() && document.GetActiveTabId() == nextTab,
          "Changing an inactive tab changed the active scene");
    document.ActivateTab(initialTab);
    Check(document.GetSelection().size() == 2 &&
              document.GetTransformSpace() == TransformSpace::Local &&
              document.GetSelectionPivotMode() == SelectionPivot::Individual,
          "Per-tab selection or manipulation settings were lost");
    document.GetScene().DestroyEntity(first);
    Check(document.GetSelection() == std::vector<uint32>{second.GetId()} &&
              document.GetSelected().GetId() == second.GetId(),
          "An externally deleted entity remained selected");
    Check(!document.IsDirty(), "Selection changes marked the scene dirty");
    Check(document.DeleteSelected() && document.GetSelection().empty() && document.IsDirty(),
          "Deleting a selection did not clear selection or mark the scene dirty");
    document.NewScene();
    auto reused = document.GetScene().CreateEntity("reused ID");
    Check(document.GetSelection().empty() && !document.IsSelected(reused.GetId()),
          "Replacing a scene retained its old entity IDs");
}

struct Pair {
    EditorDocument document;
    uint32 first;
    uint32 second;

    Pair() {
        auto a = document.GetScene().CreateEntity("a");
        auto b = document.GetScene().CreateEntity("b");
        first = a.GetId();
        second = b.GetId();
        a.GetComponent<TransformComponent>().position.x = -2;
        b.GetComponent<TransformComponent>().position.x = 2;
        document.SetSelection({first, second}, second);
    }
};

void GroupManipulation() {
    Pair pair;
    auto& scene = pair.document.GetScene();
    SelectionTransformSession session;
    Check(session.Begin(pair.document), "A selected group did not start a transaction");
    auto value = scene.GetWorldTransform(pair.second);
    value.position.x += 4;
    Check(session.Apply(scene, pair.second, value), "Group translation failed");
    Position(scene, pair.first, {2, 0, 0});
    Position(scene, pair.second, {6, 0, 0});
    value.position.x += 3;
    Check(session.Apply(scene, pair.second, value), "Repeated group preview failed");
    Position(scene, pair.first, {5, 0, 0});
    Check(session.HasChanged(), "The transaction did not report changed data");
    Check(session.Cancel(scene), "Cancel did not restore the transaction");
    Position(scene, pair.first, {-2, 0, 0});
    Position(scene, pair.second, {2, 0, 0});
    Check(!session.IsActive(), "A cancelled transaction remained active");

    for (auto pivot :
         {SelectionPivot::Median, SelectionPivot::Active, SelectionPivot::Individual}) {
        pair.document.SetSelectionPivotMode(pivot);
        Check(session.Begin(pair.document), "Rotation transaction did not start");
        value = scene.GetWorldTransform(pair.second);
        value.rotation.z = ToRadians(90);
        Check(session.Apply(scene, pair.second, value), "Group rotation failed");
        if (pivot == SelectionPivot::Median) {
            Position(scene, pair.first, {0, -2, 0});
            Position(scene, pair.second, {0, 2, 0});
        } else if (pivot == SelectionPivot::Active) {
            Position(scene, pair.first, {2, -4, 0});
            Position(scene, pair.second, {2, 0, 0});
        } else {
            Position(scene, pair.first, {-2, 0, 0});
            Position(scene, pair.second, {2, 0, 0});
        }
        Check(session.Cancel(scene), "Rotation cancel failed");
    }
    pair.document.SetSelectionPivotMode(SelectionPivot::Median);
    Check(session.Begin(pair.document), "Scale transaction did not start");
    value = scene.GetWorldTransform(pair.second);
    value.scale = {2, 2, 2};
    Check(session.Apply(scene, pair.second, value), "Uniform group scale failed");
    Position(scene, pair.first, {-4, 0, 0});
    Position(scene, pair.second, {4, 0, 0});
    Check(session.Cancel(scene), "Scale cancel failed");

    for (auto space : {TransformSpace::World, TransformSpace::Local}) {
        pair.document.SetTransformSpace(space);
        scene.GetComponent<TransformComponent>(pair.first).rotation.z = ToRadians(90);
        scene.GetComponent<TransformComponent>(pair.second).rotation.z = ToRadians(90);
        Check(session.Begin(pair.document), "Basis scale transaction did not start");
        value = scene.GetWorldTransform(pair.second);
        value.scale.x = 2;
        Check(session.Apply(scene, pair.second, value), "Axis-aligned basis scale failed");
        Position(scene, pair.first, {space == TransformSpace::World ? -4.f : -2.f, 0, 0});
        const auto scaled = scene.GetWorldTransform(pair.second).scale;
        Check(Near(space == TransformSpace::World ? scaled.y : scaled.x, 2),
              "World/local scale used the wrong orientation basis");
        Check(session.Cancel(scene), "Basis scale cancel failed");
    }
}

void HierarchyAndAtomicity() {
    EditorDocument document;
    auto parent = document.GetScene().CreateEntity("parent");
    auto child = document.GetScene().CreateEntity("child");
    parent.GetComponent<TransformComponent>().position.x = 4;
    child.GetComponent<TransformComponent>().position.x = 5;
    Check(document.GetScene().SetParent(child.GetId(), parent.GetId()), "Hierarchy setup failed");
    document.SetSelection({parent.GetId(), child.GetId()}, child.GetId());
    SelectionTransformSession session;
    Check(session.Begin(document), "A hierarchy selection did not start");
    auto target = document.GetScene().GetWorldTransform(child.GetId());
    target.position.x += 2;
    Check(session.Apply(document.GetScene(), child.GetId(), target),
          "Hierarchy translation failed");
    Position(document.GetScene(), parent.GetId(), {6, 0, 0});
    Position(document.GetScene(), child.GetId(), {7, 0, 0});
    Check(child.GetComponent<TransformComponent>().position.x == 1,
          "A child selected with its parent was transformed twice");
    Check(session.Cancel(document.GetScene()), "Hierarchy cancel failed");
    parent.GetComponent<TransformComponent>().scale = {2, 1, 1};
    child.GetComponent<TransformComponent>().rotation.z = ToRadians(45);
    document.Select(child.GetId());
    const auto original = child.GetComponent<TransformComponent>();
    Check(session.Begin(document), "A sheared world hierarchy did not start");
    target = document.GetScene().GetWorldTransform(child.GetId());
    target.position.y += 3;
    Check(session.Apply(document.GetScene(), child.GetId(), target),
          "Translation under a nonuniform parent should preserve local TRS");
    Check(child.GetComponent<TransformComponent>().rotation.z == original.rotation.z,
          "Translation changed an untouched local rotation");
    Check(session.Cancel(document.GetScene()), "Sheared hierarchy cancel failed");

    Pair pair;
    auto& scene = pair.document.GetScene();
    scene.GetComponent<TransformComponent>(pair.first).rotation.z = ToRadians(45);
    Check(session.Begin(pair.document), "Atomic preview did not start");
    target = scene.GetWorldTransform(pair.second);
    target.position.x += 1;
    Check(session.Apply(scene, pair.second, target), "Initial valid preview failed");
    target = scene.GetWorldTransform(pair.second);
    target.scale.x = 2;
    Check(!session.Apply(scene, pair.second, target), "A non-TRS group result was accepted");
    Position(scene, pair.first, {-1, 0, 0});
    Position(scene, pair.second, {3, 0, 0});
    target.scale.x = std::numeric_limits<float>::infinity();
    Check(!session.Apply(scene, pair.second, target), "A nonfinite preview was accepted");
    Check(session.Cancel(scene), "Failed preview cancellation failed");
    Check(session.Begin(pair.document), "Expired transaction setup failed");
    pair.document.NewScene();
    Check(!session.IsActive() && !session.Apply(pair.document.GetScene(), pair.second, target),
          "A transaction survived replacement of its scene");
}

void ReplacedStateWithRetainedToken() {
    EditorDocument document;
    auto original = document.CreateEntity("original");
    auto& scene = document.GetScene();
    const auto oldLifetime = scene.Lifetime().lock();
    SelectionTransformSession session;
    Check(session.Begin(document), "Retained-token transaction did not start");
    Scene replacement;
    auto fresh = replacement.CreateEntity("replacement");
    fresh.GetComponent<TransformComponent>().position.x = 99;
    Check(scene.ApplyState(replacement), "Replacement state did not apply");
    auto target = scene.GetWorldTransform(original.GetId());
    target.position.x = 200;
    Check(!session.Apply(scene, original.GetId(), target) && !session.Cancel(scene) &&
              !session.IsActive(),
          "A retained old lifetime allowed mutation of a replacement scene state");
    Position(scene, original.GetId(), {99, 0, 0});
    Check(oldLifetime != nullptr, "The lifetime fixture did not retain the previous token");
}

void InvalidPivotSafety() {
    EditorDocument document;
    auto valid = document.GetScene().CreateEntity("valid");
    valid.GetComponent<TransformComponent>().position.x = 5;
    auto invalid = document.GetScene().CreateEntity("invalid");
    invalid.GetComponent<TransformComponent>().position.x = std::numeric_limits<float>::quiet_NaN();
    document.SetSelection({valid.GetId(), invalid.GetId()}, invalid.GetId());
    for (auto mode : {SelectionPivot::Median, SelectionPivot::Active, SelectionPivot::Individual}) {
        document.SetSelectionPivotMode(mode);
        Check(GetSelectionPivot(document).x == 5 &&
                  std::isfinite(GetSelectionTransformPivot(document).rotation.x),
              "Invalid primary data escaped a paint-time pivot helper");
    }
    SelectionTransformSession session;
    Check(!session.Begin(document), "An invalid primary started a transform transaction");
    invalid.GetComponent<TransformComponent>() = {};
    Check(document.GetScene().SetParent(invalid.GetId(), valid.GetId()),
          "Singular hierarchy setup failed");
    valid.GetComponent<TransformComponent>().scale.x = 0;
    document.Select(invalid.GetId());
    Check(std::isfinite(GetSelectionPivot(document).x), "A singular parent broke pivot rendering");
    Check(session.Begin(document), "Singular transaction setup did not complete safely");
    auto target = document.GetScene().GetWorldTransform(invalid.GetId());
    target.position.y += 1;
    Check(!session.Apply(document.GetScene(), invalid.GetId(), target),
          "A singular parent accepted a noninvertible transform");
    Check(session.Cancel(document.GetScene()), "Singular cancellation failed");
}
} // namespace

int main() {
    try {
        SelectionState();
        GroupManipulation();
        HierarchyAndAtomicity();
        InvalidPivotSafety();
        ReplacedStateWithRetainedToken();
        std::cout << "Selection ordering, tabs, transforms, pivots, hierarchy and atomic "
                     "cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
