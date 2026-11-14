#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/ModelImport.hpp>
#include <LuxEditor/Viewport/ModelViewportGeometry.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void WriteFixture(const std::filesystem::path& path) {
    std::ofstream output(path);
    output << R"(; FBX 7.4.0 project file, authored by LuxEditor tests
FBXHeaderExtension: {FBXHeaderVersion:1003 FBXVersion:7400}
GlobalSettings: {Version:1000 Properties70: {
 P:"UpAxis","int","Integer","",1 P:"UpAxisSign","int","Integer","",1
 P:"FrontAxis","int","Integer","",2 P:"FrontAxisSign","int","Integer","",1
 P:"CoordAxis","int","Integer","",0 P:"CoordAxisSign","int","Integer","",1
 P:"UnitScaleFactor","double","Number","",100
}}
Objects: {
Geometry:1001,"Geometry::Triangle","Mesh" {
 Vertices:*9 {a:0,0,0,1,0,0,0,1,0} PolygonVertexIndex:*3 {a:0,1,-3}
}
Model:2001,"Model::Triangle","Mesh" {Version:232 Properties70: {
 P:"Lcl Translation","Lcl Translation","","A",0,0,0
}}
Model:2002,"Model::Parent","Null" {Version:232 Properties70: {
 P:"Lcl Translation","Lcl Translation","","A",3,0,0
}}
AnimationStack:6001,"AnimStack::Move","" {Properties70: {
 P:"LocalStart","KTime","Time","",0 P:"LocalStop","KTime","Time","",46186158000
}}
AnimationLayer:6002,"AnimLayer::Base","" {}
AnimationCurveNode:6003,"AnimCurveNode::T","" {Properties70: {
 P:"d|X","Number","","A",0 P:"d|Y","Number","","A",0 P:"d|Z","Number","","A",0
}}
AnimationCurve:6004,"AnimCurve::X","" {
 Default:0 KeyVer:4008 KeyTime:*2 {a:0,46186158000} KeyValueFloat:*2 {a:0,2}
 KeyAttrFlags:*2 {a:4,4} KeyAttrDataFloat:*8 {a:0,0,0,0,0,0,0,0} KeyAttrRefCount:*2 {a:1,1}
}}
Connections: {
 C:"OO",1001,2001 C:"OO",2001,2002 C:"OO",2002,0
 C:"OO",6002,6001 C:"OO",6003,6002 C:"OP",6004,6003,"d|X"
 C:"OP",6003,2001,"Lcl Translation"
}
)";
    Check(static_cast<bool>(output), "could not write FBX viewport fixture");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName(LUX_UI_TEXT("LuxFbxViewportTests"));
    LuxUI::CoreApplication::setApplicationName(LUX_UI_TEXT("FbxViewport"));
    LuxUI::TemporaryDir files;
    Check(files.isValid(), "test directory is unavailable");
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope, files.path());
    LuxUI::Settings().clear();
    const auto directory = LuxEditor::PathFromUtf8(files.path().toUtf8().toStdString());
    const auto path = directory / "animated.fbx";
    WriteFixture(path);
    std::string error;
    auto model = LuxEngine::ModelAsset::LoadFbx(LuxEditor::PathUtf8(path), error);
    Check(model && model->animations.size() == 1 && model->submeshes.size() == 1,
          "FBX viewport fixture did not load");
    LuxEditor::EditorWindow window;
    LuxEditor::ModelImportResult imported;
    window.ExecuteRecordedEdit(LUX_UI_TEXT("Import fixture"), [&] {
        Check(LuxEditor::ImportModelAsset(window.GetDocument().GetScene(), *model, directory,
                                          imported, error),
              "FBX fixture import failed");
        window.GetDocument().Select(imported.root);
    });
    auto& scene = window.GetDocument().GetScene();
    const auto meshes = scene.GetComponentEntityIds<LuxEngine::MeshRendererComponent>();
    Check(meshes.size() == 1, "imported submesh is missing");
    LuxEngine::Entity mesh{meshes.front(), &scene};
    LuxEngine::Vec3 minimum, maximum;
    Check(LuxEditor::ModelViewportBounds(mesh, minimum, maximum) && std::abs(minimum.x) < .001 &&
              std::abs(maximum.x - 1) < .001,
          "FBX bounds flattened node transforms or ignored the selected submesh");
    const auto origin = scene.GetWorldTransform(mesh.GetId()).position;
    Check(
        LuxEditor::IntersectModelViewport({origin + LuxEngine::Vec3{.2f, .2f, 2}, {0, 0, -1}}, mesh)
            .has_value(),
        "FBX triangle picking missed the visible surface");
    Check(!LuxEditor::IntersectModelViewport({origin + LuxEngine::Vec3{.8f, .8f, 2}, {0, 0, -1}},
                                             mesh),
          "FBX picking selected empty space inside the mesh bounding box");
    window.show();
    app.processEvents();
    auto* open = window.findChild<LuxUI::Action*>(LUX_UI_TEXT("openAnimationWorkspace"));
    Check(open, "animation workspace command is missing");
    open->trigger();
    app.processEvents();
    auto* mode = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("animationSourceMode"));
    auto* clip = window.findChild<LuxUI::ComboBox*>(LUX_UI_TEXT("modelAnimationClip"));
    auto* time = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("modelAnimationTime"));
    auto* speed = window.findChild<LuxUI::DoubleSpinBox*>(LUX_UI_TEXT("modelAnimationSpeed"));
    auto* loop = window.findChild<LuxUI::CheckBox*>(LUX_UI_TEXT("modelAnimationLoop"));
    auto* history = window.findChild<LuxUI::UndoStack*>();
    Check(mode && mode->currentIndex() == 1 && clip && clip->count() == 2 && time && speed &&
              loop && history,
          "FBX clip controls were not separated into the animation workspace");
    Check(window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("modelAnimationWorkspace"))->isVisible() &&
              !window.findChild<LuxUI::Widget*>(LUX_UI_TEXT("animationSequence"))->isVisible(),
          "transform tracks are still mixed with FBX clip controls");
    const int before = history->index();
    time->setValue(.5);
    Check(std::abs(scene.GetWorldTransform(mesh.GetId()).position.x - origin.x - 1) < .01,
          "FBX clip scrubbing did not update node transforms");
    Check(history->index() == before + 1, "FBX time scrub was not recorded as one undo edit");
    speed->setValue(.5);
    LuxUI::MetaObject::invokeMethod(speed, "editingFinished");
    loop->setChecked(false);
    for (const auto id : scene.GetComponentEntityIds<LuxEngine::ModelAnimationComponent>()) {
        const auto& animation = scene.GetComponent<LuxEngine::ModelAnimationComponent>(id);
        Check(animation.time == .5 && animation.speed == .5 && !animation.loop,
              "FBX instance nodes did not receive consistent clip playback settings");
    }
    clip->setCurrentIndex(0);
    Check(scene.GetComponent<LuxEngine::ModelAnimationComponent>(imported.root).clip == -1 &&
              std::abs(scene.GetWorldTransform(mesh.GetId()).position.x - origin.x) < .01,
          "bind pose selection did not restore the model's default transforms");
    window.GetDocument().SetDirty(false);
    window.close();
    return 0;
}
