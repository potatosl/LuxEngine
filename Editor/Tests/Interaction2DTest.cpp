#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace LuxEditor;
using namespace LuxEngine;

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Mouse(LuxUI::Widget& widget, LuxUI::Event::Type type, LuxUI::PointF position,
           LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    LuxUI::MouseEvent event(type, position, widget.mapToGlobal(position.toPoint()), button, buttons,
                            modifiers);
    LuxUI::Application::sendEvent(&widget, &event);
}

void Click(LuxUI::Widget& widget, LuxUI::PointF position,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    Mouse(widget, LuxUI::Event::MouseButtonPress, position, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton, modifiers);
    Mouse(widget, LuxUI::Event::MouseButtonRelease, position, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton, modifiers);
}

void Box(LuxUI::Widget& widget, LuxUI::PointF from, LuxUI::PointF to,
         LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    Mouse(widget, LuxUI::Event::MouseButtonPress, from, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton, modifiers);
    Mouse(widget, LuxUI::Event::MouseMove, to, LuxUI::Flags::NoButton, LuxUI::Flags::LeftButton,
          modifiers);
    Mouse(widget, LuxUI::Event::MouseButtonRelease, to, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton, modifiers);
}

void Escape(LuxUI::Widget& widget) {
    LuxUI::KeyEvent overrideEvent(LuxUI::Event::ShortcutOverride, LuxUI::Flags::Key_Escape,
                                  LuxUI::Flags::NoModifier);
    overrideEvent.setAccepted(false);
    LuxUI::Application::sendEvent(&widget, &overrideEvent);
    Check(overrideEvent.isAccepted(), "An in-progress 2D gesture did not claim Escape");
    LuxUI::KeyEvent key(LuxUI::Event::KeyPress, LuxUI::Flags::Key_Escape, LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&widget, &key);
}

void FreeMovement(LuxUI::Application& app) {
    EditorDocument document;
    auto entity = document.GetScene().CreateEntity("Fractional position");
    entity.GetComponent<TransformComponent>().position = {.125f, .375f, 0};
    entity.AddComponent<SpriteRendererComponent>();
    document.Select(entity.GetId());
    SceneViewport2D view(
        nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
        [&](uint32 id, const TransformComponent& value) {
            document.GetScene().SetWorldTransform(id, value);
        },
        [](uint32, uint32, uint32, uint8_t) {}, [] {}, [] {});
    view.resize(640, 480);
    view.SetTool(EditorTool::Move);
    view.show();
    app.processEvents();
    Mouse(view, LuxUI::Event::MouseButtonPress, {329, 213}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {342, 206}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, {342, 206}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    const auto position = entity.GetComponent<TransformComponent>().position;
    Check(std::abs(position.x - (.125f + 13.f / 72.f)) < .001f &&
              std::abs(position.y - (.375f + 7.f / 72.f)) < .001f,
          "The 2D position gizmo quantized fractional mouse movement to the grid");
}

void SelectionAndTransform(LuxUI::Application& app) {
    EditorDocument document;
    auto left = document.GetScene().CreateEntity("Left");
    auto right = document.GetScene().CreateEntity("Right");
    left.GetComponent<TransformComponent>().position.x = -2;
    right.GetComponent<TransformComponent>().position.x = 2;
    left.AddComponent<SpriteRendererComponent>();
    right.AddComponent<SpriteRendererComponent>();
    document.SetDirty(false);
    SelectionTransformSession session;
    Scene* editScene = nullptr;
    int begun = 0, committed = 0, cancelled = 0;
    SceneViewport2D view(
        nullptr, document,
        [&](std::optional<uint32> id) {
            Check(document.GetSelectedId(document.GetActiveTabId()) == id,
                  "2D selection notification disagreed with the shared document");
        },
        [&](uint32 id, const TransformComponent& value) {
            Check(session.Apply(document.GetScene(), id, value), "2D group transform failed");
        },
        [](uint32, uint32, uint32, uint8_t) {},
        [&] {
            ++begun;
            editScene = &document.GetScene();
            Check(session.Begin(document), "2D gesture could not capture its selection");
        },
        [&] {
            ++committed;
            session.End();
        });
    view.SetCancelEditCallback([&] {
        ++cancelled;
        Check(editScene && session.Cancel(*editScene), "2D gesture cancellation failed");
    });
    view.resize(640, 480);
    view.show();
    app.processEvents();
    view.setFocus();
    Click(view, {176, 240});
    Check(document.GetSelection() == std::vector<uint32>{left.GetId()},
          "2D click did not select the front object");
    const int beforeModifiers = begun;
    Click(view, {464, 240}, LuxUI::Flags::ShiftModifier);
    Check(document.GetSelection().size() == 2 && begun == beforeModifiers,
          "Shift selection started a transform or lost the previous selection");
    Click(view, {176, 240}, LuxUI::Flags::ControlModifier);
    Check(document.GetSelection() == std::vector<uint32>{right.GetId()},
          "Ctrl selection did not toggle an object");
    Box(view, {100, 180}, {510, 300});
    Check(document.GetSelection().size() == 2, "2D marquee did not select both objects");
    Check(!document.IsDirty(), "Selecting objects marked the scene as edited");
    Box(view, {100, 180}, {510, 300}, LuxUI::Flags::ControlModifier);
    Check(document.GetSelection().empty(), "Ctrl marquee did not toggle the existing selection");
    document.SetSelection({left.GetId(), right.GetId()}, left.GetId());
    Click(view, {100, 100}, LuxUI::Flags::ControlModifier);
    Check(document.GetSelectedId(document.GetActiveTabId()) == left.GetId(),
          "An empty additive click silently changed the active object");
    document.SetSelection({left.GetId(), right.GetId()}, right.GetId());
    Mouse(view, LuxUI::Event::MouseButtonPress, {100, 180}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {510, 300}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Escape(view);
    Check(document.GetSelection().size() == 2,
          "Cancelling a marquee changed the previous selection");
    const int beforeMove = committed;
    view.SetTool(EditorTool::Move);
    Mouse(view, LuxUI::Event::MouseButtonPress, {464, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {536, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    Check(committed == beforeMove + 1, "A 2D multi-object drag committed more than one gesture");
    Check(std::abs(left.GetComponent<TransformComponent>().position.x + 1) < .001f &&
              std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "A 2D group move did not preserve relative positions");
    Mouse(view, LuxUI::Event::MouseButtonPress, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Escape(view);
    Check(cancelled == 1 && committed == beforeMove + 1 &&
              std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "Escape committed a 2D drag instead of restoring it");
    Mouse(view, LuxUI::Event::MouseButtonRelease, {572, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    Check(committed == beforeMove + 1, "Mouse release committed an already cancelled gesture");
    Mouse(view, LuxUI::Event::MouseButtonPress, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    LuxUI::FocusEvent focusLost(LuxUI::Event::FocusOut);
    LuxUI::Application::sendEvent(&view, &focusLost);
    Check(cancelled == 2 &&
              std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "Focus loss committed or retained a 2D transform preview");
    Mouse(view, LuxUI::Event::MouseButtonPress, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    view.SetTool(EditorTool::Rotate);
    Check(cancelled == 3 &&
              std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "Switching the 2D tool did not cancel its preview");
    // Identical preview events must not feed an already-orbited active position back into
    // the shared session as a second translation.
    Mouse(view, LuxUI::Event::MouseButtonPress, {392, 186}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {446, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    const auto rotated = right.GetComponent<TransformComponent>().position;
    Mouse(view, LuxUI::Event::MouseMove, {446, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Check(std::abs(rotated.x - 1) < .001f && std::abs(rotated.y + 2) < .001f &&
              std::abs(right.GetComponent<TransformComponent>().position.x - rotated.x) < .001f &&
              std::abs(right.GetComponent<TransformComponent>().position.y - rotated.y) < .001f,
          "A repeated 2D common-pivot rotation accumulated an extra translation");
    Mouse(view, LuxUI::Event::MouseButtonRelease, {446, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    left.GetComponent<TransformComponent>() = {};
    right.GetComponent<TransformComponent>() = {};
    left.GetComponent<TransformComponent>().position.x = -1;
    right.GetComponent<TransformComponent>().position.x = 3;
    view.SetTool(EditorTool::Scale);
    Mouse(view, LuxUI::Event::MouseButtonPress, {470, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {500, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    const auto scaled = right.GetComponent<TransformComponent>().position;
    Mouse(view, LuxUI::Event::MouseMove, {500, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Check(scaled.x > 3 &&
              std::abs(right.GetComponent<TransformComponent>().position.x - scaled.x) < .001f,
          "A repeated 2D common-pivot scale accumulated an extra translation");
    Escape(view);
    Check(std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "Cancelling a group scale did not restore the original position");
    view.SetTool(EditorTool::Move);
    Mouse(view, LuxUI::Event::MouseButtonPress, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::NoButton);
    Check(cancelled == 5 &&
              std::abs(right.GetComponent<TransformComponent>().position.x - 3) < .001f,
          "A lost mouse release left the 2D transform gesture active");
    Mouse(view, LuxUI::Event::MouseButtonPress, {536, 240}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {572, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    document.NewTab();
    Mouse(view, LuxUI::Event::MouseMove, {600, 240}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Check(cancelled == 6 && document.GetScene().GetEntityCount() == 0,
          "A stale 2D gesture wrote into the replacement scene");
}

void PaintingCancel(LuxUI::Application& app) {
    EditorDocument document;
    auto map = document.GetScene().CreateEntity("Map");
    const uint32 id = map.GetId();
    map.AddComponent<TilemapComponent>().Resize(4, 4);
    document.Select(id);
    int committed = 0;
    SceneViewport2D view(
        nullptr, document, [&](std::optional<uint32> selected) { document.Select(selected); },
        [](uint32, const TransformComponent&) {},
        [&](uint32 entity, uint32 x, uint32 y, uint8_t tile) {
            document.GetScene().GetComponent<TilemapComponent>(entity).Set(x, y, tile);
        },
        [] {}, [&] { ++committed; });
    view.resize(640, 480);
    view.SetTool(EditorTool::TileBrush);
    view.show();
    app.processEvents();
    Mouse(view, LuxUI::Event::MouseButtonPress, {340, 220}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Check(document.GetScene().GetComponent<TilemapComponent>(id).Get(0, 0) == 1,
          "Tile brush did not paint its preview");
    Escape(view);
    Check(document.GetScene().GetComponent<TilemapComponent>(id).Get(0, 0) == 0 && committed == 0,
          "Cancelling tile painting retained cells or committed an undo record");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    try {
        FreeMovement(app);
        SelectionAndTransform(app);
        PaintingCancel(app);
        std::cout << "2D selection, marquee, group transforms and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
