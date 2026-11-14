#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace LuxEditor;
using namespace LuxEngine;

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Mouse(LuxUI::Widget& view, LuxUI::Event::Type type, LuxUI::PointF point,
           LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    LuxUI::MouseEvent event(type, point, view.mapToGlobal(point.toPoint()), button, buttons,
                            modifiers);
    LuxUI::Application::sendEvent(&view, &event);
}

void Click(LuxUI::Widget& view, LuxUI::PointF point,
           LuxUI::Flags::KeyboardModifiers modifiers = LuxUI::Flags::NoModifier) {
    Mouse(view, LuxUI::Event::MouseButtonPress, point, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton, modifiers);
    Mouse(view, LuxUI::Event::MouseButtonRelease, point, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton, modifiers);
}

bool Near(Vec3 a, Vec3 b) {
    const auto delta = a - b;
    return std::sqrt(delta.Dot(delta)) < .0001f;
}

void Test(LuxUI::Application& app) {
    EditorDocument document;
    auto first = document.GetScene().CreateEntity("First");
    auto second = document.GetScene().CreateEntity("Second");
    first.AddComponent<MeshRendererComponent>();
    second.AddComponent<MeshRendererComponent>();
    first.GetComponent<TransformComponent>().position = {-3, 2, 0};
    second.GetComponent<TransformComponent>().position = {3, 2, 0};
    SelectionTransformSession session;
    int begins = 0, commits = 0, cancels = 0;
    SceneViewport3D view(
        nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
        [&](uint32 id, const TransformComponent& transform) {
            Check(session.Apply(document.GetScene(), id, transform), "Group preview was rejected");
        },
        [&] {
            ++begins;
            Check(session.Begin(document), "Gesture did not capture selection");
        },
        [&] {
            ++commits;
            session.End();
        });
    view.SetCancelEditCallback([&] {
        ++cancels;
        Check(session.Cancel(document.GetScene()), "Gesture rollback failed");
        session.End();
    });
    view.resize(640, 480);
    view.SetViewPreset(SceneViewport3D::ViewPreset::Front);
    view.show();
    app.processEvents();
    const auto camera = MakeViewportCamera3D(view.size(), {}, 0, 0, 22);
    const auto screen = [&](Vec3 point) {
        const auto result = ProjectViewportPoint(camera, view.size(), point);
        Check(result.has_value(), "Fixture was clipped");
        return *result;
    };
    Click(view, screen({-3, 2, 0}));
    Check(document.GetSelection() == std::vector<uint32>{first.GetId()},
          "Click did not replace selection");
    Click(view, screen({3, 2, 0}), LuxUI::Flags::ControlModifier);
    Check(document.GetSelection().size() == 2 && document.GetSelected().GetId() == second.GetId(),
          "Ctrl click did not add and activate an object");
    Click(view, screen({-3, 2, 0}), LuxUI::Flags::ControlModifier);
    Check(document.GetSelection() == std::vector<uint32>{second.GetId()},
          "Ctrl click did not toggle off");
    Click(view, screen({-3, 2, 0}), LuxUI::Flags::ShiftModifier);
    Check(document.GetSelection().size() == 2, "Shift click did not extend selection");
    document.Select(std::nullopt);
    Mouse(view, LuxUI::Event::MouseButtonPress, {210, 170}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {430, 260}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Check(document.GetSelection().empty(), "Marquee changed selection before release");
    Mouse(view, LuxUI::Event::MouseButtonRelease, {430, 260}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    Check(document.GetSelection().size() == 2, "Marquee did not select both projected objects");
    const auto originalFirst = first.GetComponent<TransformComponent>().position;
    const auto originalSecond = second.GetComponent<TransformComponent>().position;
    const LuxUI::PointF pivot = screen(GetSelectionPivot(document));
    const int beforeBegins = begins, beforeCommits = commits;
    view.SetTool(EditorTool::Move);
    Mouse(view, LuxUI::Event::MouseButtonPress, pivot, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, pivot + LuxUI::PointF(35, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, pivot + LuxUI::PointF(60, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, pivot + LuxUI::PointF(60, 0),
          LuxUI::Flags::LeftButton, LuxUI::Flags::NoButton);
    const auto firstDelta = first.GetComponent<TransformComponent>().position - originalFirst;
    const auto secondDelta = second.GetComponent<TransformComponent>().position - originalSecond;
    Check(firstDelta.x > 0 && Near(firstDelta, secondDelta),
          "Group translation changed relative spacing");
    Check(std::abs(firstDelta.x - std::round(firstDelta.x)) > .001f,
          "The 3D group gizmo quantized its translation to the grid");
    Check(begins == beforeBegins + 1 && commits == beforeCommits + 1,
          "Drag did not commit one gesture");
    const auto checkpointFirst = first.GetComponent<TransformComponent>().position;
    const auto checkpointSecond = second.GetComponent<TransformComponent>().position;
    const auto movedPivot = screen(GetSelectionPivot(document));
    Mouse(view, LuxUI::Event::MouseButtonPress, movedPivot, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, movedPivot + LuxUI::PointF(35, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    LuxUI::KeyEvent escape(LuxUI::Event::KeyPress, LuxUI::Flags::Key_Escape,
                           LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&view, &escape);
    Mouse(view, LuxUI::Event::MouseButtonRelease, movedPivot + LuxUI::PointF(35, 0),
          LuxUI::Flags::LeftButton, LuxUI::Flags::NoButton);
    Check(cancels == 1 && commits == beforeCommits + 1 &&
              Near(first.GetComponent<TransformComponent>().position, checkpointFirst) &&
              Near(second.GetComponent<TransformComponent>().position, checkpointSecond),
          "Esc failed to restore all objects or created a commit");
    Mouse(view, LuxUI::Event::MouseButtonPress, movedPivot, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, movedPivot + LuxUI::PointF(-25, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    LuxUI::FocusEvent focusOut(LuxUI::Event::FocusOut);
    LuxUI::Application::sendEvent(&view, &focusOut);
    Check(cancels == 2 && Near(first.GetComponent<TransformComponent>().position, checkpointFirst),
          "Focus loss did not cancel a gesture");
    Mouse(view, LuxUI::Event::MouseButtonPress, movedPivot, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, movedPivot + LuxUI::PointF(-25, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    view.SetTool(EditorTool::Rotate);
    Check(cancels == 3 &&
              Near(second.GetComponent<TransformComponent>().position, checkpointSecond),
          "Changing tools did not cancel the preview");
    const auto beforeMarqueeCancel = document.GetSelection();
    Mouse(view, LuxUI::Event::MouseButtonPress, {10, 10}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, {80, 80}, LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    LuxUI::Application::sendEvent(&view, &escape);
    Mouse(view, LuxUI::Event::MouseButtonRelease, {80, 80}, LuxUI::Flags::LeftButton,
          LuxUI::Flags::NoButton);
    Check(document.GetSelection() == beforeMarqueeCancel && cancels == 3,
          "Cancelling a selection rectangle lost the prior selection or affected history");

    view.SetTool(EditorTool::Move);
    Mouse(view, LuxUI::Event::MouseButtonPress, movedPivot, LuxUI::Flags::LeftButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, movedPivot + LuxUI::PointF(20, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, movedPivot + LuxUI::PointF(25, 0), LuxUI::Flags::NoButton,
          LuxUI::Flags::NoButton);
    Check(cancels == 4 && Near(first.GetComponent<TransformComponent>().position, checkpointFirst),
          "A missing OS button release left the transform gesture active");

    document.SetSelection({first.GetId(), second.GetId()}, second.GetId());
    second.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    document.SetTransformSpace(TransformSpace::Local);
    view.SetTool(EditorTool::Move);
    const auto localPivot = screen(GetSelectionPivot(document));
    const auto beforeLocalFirst = first.GetComponent<TransformComponent>().position;
    const auto beforeLocalSecond = second.GetComponent<TransformComponent>().position;
    Mouse(view, LuxUI::Event::MouseButtonPress, localPivot - LuxUI::PointF(0, 50),
          LuxUI::Flags::LeftButton, LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, localPivot - LuxUI::PointF(0, 95), LuxUI::Flags::NoButton,
          LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, localPivot - LuxUI::PointF(0, 95),
          LuxUI::Flags::LeftButton, LuxUI::Flags::NoButton);
    const auto localDelta = first.GetComponent<TransformComponent>().position - beforeLocalFirst;
    Check(localDelta.y > 0 && std::abs(localDelta.x) < .001f &&
              Near(second.GetComponent<TransformComponent>().position - beforeLocalSecond,
                   localDelta),
          "The rotated local X gizmo did not move the group on its displayed axis");

    view.SetTool(EditorTool::Rotate);
    const auto rotationPivot = screen(GetSelectionPivot(document));
    const auto beforeSeparation = second.GetComponent<TransformComponent>().position -
                                  first.GetComponent<TransformComponent>().position;
    Mouse(view, LuxUI::Event::MouseButtonPress, rotationPivot + LuxUI::PointF(52, -52),
          LuxUI::Flags::LeftButton, LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, rotationPivot + LuxUI::PointF(-52, -52),
          LuxUI::Flags::NoButton, LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, rotationPivot + LuxUI::PointF(-52, -52),
          LuxUI::Flags::LeftButton, LuxUI::Flags::NoButton);
    const auto separation = second.GetComponent<TransformComponent>().position -
                            first.GetComponent<TransformComponent>().position;
    Check(std::abs(separation.x) < .01f && std::abs(separation.y - beforeSeparation.x) < .01f,
          "A median-pivot rotation did not orbit the selected group around its displayed pivot");

    document.SetSelectionPivotMode(SelectionPivot::Individual);
    const auto individualFirst = first.GetComponent<TransformComponent>().position;
    const auto individualSecond = second.GetComponent<TransformComponent>().position;
    const auto individualPivot = screen(GetSelectionPivot(document));
    Mouse(view, LuxUI::Event::MouseButtonPress, individualPivot + LuxUI::PointF(52, -52),
          LuxUI::Flags::LeftButton, LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseMove, individualPivot + LuxUI::PointF(-52, -52),
          LuxUI::Flags::NoButton, LuxUI::Flags::LeftButton);
    Mouse(view, LuxUI::Event::MouseButtonRelease, individualPivot + LuxUI::PointF(-52, -52),
          LuxUI::Flags::LeftButton, LuxUI::Flags::NoButton);
    Check(Near(first.GetComponent<TransformComponent>().position, individualFirst) &&
              Near(second.GetComponent<TransformComponent>().position, individualSecond),
          "Individual-pivot rotation moved the entity origins");
    view.close();
}
} // namespace

int main(int argc, char** argv) {
    qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
    LuxUI::Application app(argc, argv);
    try {
        Test(app);
        std::cout << "3D multi-selection, marquee, group gesture and rollback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
