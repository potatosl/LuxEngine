#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    using namespace LuxEngine;
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxEngineTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("LuxEditorScriptFieldsTests"));
    LuxUI::TemporaryDir settings, files;
    if (!settings.isValid() || !files.isValid())
        return 1;
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    try {
        Scene scene;
        auto entity = scene.CreateEntity("Script fixture");
        ScriptComponent script{"Game.Player", true};
        script.fields = {{"Speed", 2.5f},
                         {"Health", int32{10}},
                         {"Title", std::string("원본")},
                         {"Enabled", true},
                         {"Offset", Vec3{1, 2, 3}},
                         {"Target", ScriptEntityReference{entity.GetId()}}};
        entity.AddComponent<ScriptComponent>(script);
        const auto path = files.filePath(LUX_UI_TEXT("fields.lux"));
        Check(scene.Serialize(path.toUtf8().toStdString()), "Script UI fixture did not serialize");
        LuxUI::Settings().setValue(LUX_UI_TEXT("lastScene"), path);
        LuxEditor::EditorWindow window;
        window.show();
        app.processEvents();
        auto* outliner = window.findChild<LuxUI::TreeWidget*>(LUX_UI_TEXT("sceneOutliner"));
        Check(outliner && outliner->topLevelItemCount() == 1, "Script UI fixture did not open");
        outliner->setCurrentItem(outliner->topLevelItem(0));
        const auto selected = [&] { return window.GetDocument().GetSelected(); };
        auto* undo = window.findChild<LuxUI::UndoStack*>();
        auto* health = window.findChild<LuxUI::SpinBox*>(LUX_UI_TEXT("scriptField_Health"));
        Check(undo && health, "Typed script inspector fields were not created");
        health->setValue(40);
        Check(std::get<int32>(selected().GetComponent<ScriptComponent>().fields.at("Health")) == 40,
              "Script inspector did not update the component");
        undo->undo();
        Check(std::get<int32>(selected().GetComponent<ScriptComponent>().fields.at("Health")) == 10,
              "Script field undo failed");
        undo->redo();
        auto* speed = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("scriptField_Speed_0"));
        Check(speed, "Float script field editor was not created");
        speed->setValue(7.5);
        Check(std::get<float>(selected().GetComponent<ScriptComponent>().fields.at("Speed")) ==
                  7.5f,
              "Float script field edit failed");
        auto* offset = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("scriptField_Offset_1"));
        Check(offset, "Vector script field editor was not created");
        offset->setValue(9);
        const auto vector =
            std::get<Vec3>(selected().GetComponent<ScriptComponent>().fields.at("Offset"));
        Check(vector.x == 1 && vector.y == 9 && vector.z == 3,
              "Vector field edit changed other axes");
        auto* target = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("scriptField_Target"));
        Check(target, "Entity reference editor was not created");
        target->setCurrentIndex(0);
        Check(std::get<ScriptEntityReference>(
                  selected().GetComponent<ScriptComponent>().fields.at("Target"))
                      .id == 0,
              "Entity reference editor did not clear the reference");
        Check(window.GetDocument().Save(), "Edited script fields did not save");
        Scene reopened;
        Check(reopened.Deserialize(path.toUtf8().toStdString()),
              "Edited script fields did not reopen");
        const auto& saved = reopened.GetComponent<ScriptComponent>(entity.GetId());
        Check(std::get<int32>(saved.fields.at("Health")) == 40 &&
                  std::get<float>(saved.fields.at("Speed")) == 7.5f &&
                  std::get<std::string>(saved.fields.at("Title")) == "원본",
              "Script field save/reopen changed authored data");
        std::cout << "Script field inspector, undo, entity references and persistence passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
