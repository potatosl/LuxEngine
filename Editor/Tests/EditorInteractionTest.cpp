#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void Mouse(LuxUI::Widget* target, LuxUI::Event::Type type, LuxUI::PointF point,
           LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons) {
    LuxUI::MouseEvent event(type, point, target->mapToGlobal(point.toPoint()), button, buttons,
                            LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(target, &event);
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxInteractionTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("EditorInteraction"));
    LuxUI::TemporaryDir settings;
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    try {
        LuxEditor::EditorWindow window;
        window.show();
        auto& document = window.GetDocument();
        LuxEngine::uint32 first = 0, second = 0;
        window.ExecuteRecordedEdit(LUX_UI_TEXT("Fixture"), [&] {
            auto a = document.CreateEntity("First");
            first = a.GetId();
            a.AddComponent<LuxEngine::SpriteRendererComponent>();
            a.GetComponent<LuxEngine::TransformComponent>().position.x = -2;
            auto b = document.CreateEntity("Second");
            second = b.GetId();
            b.AddComponent<LuxEngine::SpriteRendererComponent>();
            b.GetComponent<LuxEngine::TransformComponent>().position.x = 2;
        });
        auto* tree = window.findChild<LuxUI::TreeWidget*>(LUX_UI_TEXT("sceneOutliner"));
        tree->clearSelection();
        tree->topLevelItem(0)->setSelected(true);
        tree->topLevelItem(1)->setSelected(true);
        tree->setCurrentItem(tree->topLevelItem(1), 0, LuxUI::ItemSelectionModel::NoUpdate);
        Check(document.GetSelection() == std::vector<LuxEngine::uint32>{first, second},
              "Outliner did not update the document's complete selection");
        auto* undo = window.findChild<LuxUI::UndoStack*>();
        undo->clear();
        document.SetDirty(false);
        LuxUI::GroupBox* transformGroup = nullptr;
        for (auto* group : window.findChildren<LuxUI::GroupBox*>())
            if (group->property("componentTitle").toString() == LuxUI::Latin1String("Transform"))
                transformGroup = group;
        Check(transformGroup, "Transform Details are unavailable");
        transformGroup->findChildren<LuxUI::DoubleSpinBox*>().front()->setValue(5);
        Check(document.GetScene().GetWorldTransform(first).position.x == 5 &&
                  document.GetScene().GetWorldTransform(second).position.x == 5,
              "Transform Details did not edit the complete selection");
        undo->undo();
        Check(document.GetSelection().size() == 2 &&
                  document.GetScene().GetWorldTransform(first).position.x == -2,
              "Batch Details undo did not restore the selection");
        undo->clear();
        document.SetDirty(false);
        window.findChild<LuxUI::Action*>(LUX_UI_TEXT("view2D"))->trigger();
        for (auto* action : window.findChildren<LuxUI::Action*>())
            if (action->shortcut() == LuxUI::KeySequence(LuxUI::Flags::Key_W))
                action->trigger();
        app.processEvents();
        auto* view = static_cast<LuxEditor::SceneViewport2D*>(
            window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("sceneViewport2D")));
        Check(view && view->isVisible(), "2D viewport was not activated");
        const auto position = [&](LuxEngine::uint32 id) {
            return document.GetScene().GetWorldTransform(id).position.x;
        };
        const auto drag = [&] {
            double x, y, halfHeight;
            view->GetView(x, y, halfHeight);
            const double unit = view->height() / (2 * halfHeight);
            const double pivot = (position(first) + position(second)) * .5;
            LuxUI::PointF start(view->width() * .5 + (pivot - x) * unit,
                                view->height() * .5 + y * unit);
            LuxUI::PointF end = start + LuxUI::PointF(unit, 0);
            Mouse(view, LuxUI::Event::MouseButtonPress, start, LuxUI::Flags::LeftButton,
                  LuxUI::Flags::LeftButton);
            Mouse(view, LuxUI::Event::MouseMove, end, LuxUI::Flags::NoButton,
                  LuxUI::Flags::LeftButton);
            return end;
        };
        auto finish = drag();
        Check(std::abs(position(first) + 1) < .001 && std::abs(position(second) - 3) < .001,
              "Shared viewport callback did not transform every selected object");
        LuxUI::KeyEvent escape(LuxUI::Event::KeyPress, LuxUI::Flags::Key_Escape,
                               LuxUI::Flags::NoModifier);
        LuxUI::Application::sendEvent(view, &escape);
        Mouse(view, LuxUI::Event::MouseButtonRelease, finish, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
        Check(position(first) == -2 && position(second) == 2 && !document.IsDirty() &&
                  undo->count() == 0,
              "Escape did not restore data, dirty state and empty undo history");
        {
            double x, y, halfHeight;
            view->GetView(x, y, halfHeight);
            const double unit = view->height() / (2 * halfHeight);
            const LuxUI::PointF start(view->width() * .5 - x * unit,
                                      view->height() * .5 + y * unit);
            Mouse(view, LuxUI::Event::MouseButtonPress, start, LuxUI::Flags::LeftButton,
                  LuxUI::Flags::LeftButton);
            document.SetSelection({second});
            LuxUI::Application::sendEvent(view, &escape);
            Mouse(view, LuxUI::Event::MouseButtonRelease, start, LuxUI::Flags::LeftButton,
                  LuxUI::Flags::NoButton);
            Check(document.GetSelection() == std::vector<LuxEngine::uint32>{first, second} &&
                      !document.IsDirty() && undo->count() == 0,
                  "An unchanged canceled gesture did not restore its complete selection");
        }
        finish = drag();
        Mouse(view, LuxUI::Event::MouseButtonRelease, finish, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
        Check(undo->count() == 1 && document.GetSelection().size() == 2,
              "A gesture did not create exactly one command");
        undo->undo();
        Check(position(first) == -2 && position(second) == 2 &&
                  document.GetSelection().size() == 2 && !document.IsDirty(),
              "Multi-object undo did not restore selection and clean state");
        undo->redo();
        Check(position(first) == -1 && position(second) == 3, "Multi-object redo failed");
        finish = drag();
        LuxUI::FocusEvent focus(LuxUI::Event::FocusOut);
        LuxUI::Application::sendEvent(view, &focus);
        Check(position(first) == -1 && position(second) == 3 && undo->count() == 1,
              "Focus loss committed a partial gesture");
        finish = drag();
        window.findChild<LuxUI::Action*>(LUX_UI_TEXT("undoEdit"))->trigger();
        Check(position(first) == -1 && undo->index() == 1,
              "Undo during a gesture affected an older command");
        finish = drag();
        const auto original = document.GetActiveTabId();
        const auto other = window.NewSceneDocument();
        Check(document.GetScene(original)->GetWorldTransform(first).position.x == -1 &&
                  document.GetSelection(original).size() == 2,
              "Scene switch did not cancel the original gesture");
        Mouse(view, LuxUI::Event::MouseButtonRelease, finish, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
        Check(document.GetActiveTabId() == other && document.GetScene().GetEntityCount() == 0 &&
                  !document.IsDirty(),
              "Late mouse release changed the new scene");
        Check(window.SwitchSceneDocument(original, false), "Original scene did not activate");
        finish = drag();
        Check(window.MoveSelectionToScene(other), "Scene move during a gesture failed");
        Mouse(view, LuxUI::Event::MouseButtonRelease, finish, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
        Check(
            document.GetScene(original)->GetEntityCount() == 1 &&
                document.GetScene(original)->GetWorldTransform(first).position.x == -1 &&
                document.GetScene(other)->GetEntityCount() == 1 &&
                document.GetScene().GetWorldTransform(document.GetSelected().GetId()).position.x ==
                    3,
            "Canceled gesture retained the moved source entity or exported preview data");
        undo->undo();
        Check(document.GetActiveTabId() == original &&
                  document.GetScene(original)->GetEntityCount() == 2 &&
                  document.GetScene(other)->GetEntityCount() == 0 &&
                  document.GetSelection() == std::vector<LuxEngine::uint32>{first, second},
              "Scene move undo did not restore both scenes and the complete selection");
        for (const auto& tab : document.ListTabs())
            document.SetDirty(tab.id, false);
        window.close();
        std::cout << "Integrated selection, cancellation, dirty state, undo and scene-switch "
                     "isolation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
