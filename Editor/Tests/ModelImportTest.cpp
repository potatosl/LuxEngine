#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/ModelImport.hpp>
#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace LuxEngine;
using namespace LuxEditor;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float a, float b) {
    return std::abs(a - b) < .001f;
}

uint32 FindNode(const Scene& scene, const ModelImportResult& imported, uint32 index) {
    for (const auto id : imported.entities)
        if (const auto* source = scene.TryGetComponent<ModelAnimationComponent>(id))
            if (source->nodeIndex == index && source->applyNodeTransform)
                return id;
    throw std::runtime_error("Imported source node is missing");
}

void Service(const fs::path& root, const BasicModelInstallation& installed) {
    std::string error;
    const auto source = ModelAsset::LoadFbx(PathUtf8(installed.modelPath), error);
    Check(source && !source->nodes.empty() && !source->submeshes.empty(), error.c_str());
    ModelAsset synthetic = *source;
    const auto first = static_cast<uint32>(synthetic.nodes.size());
    ModelNode parent;
    parent.name = "Two materials";
    parent.parent = 0;
    parent.localTransform.position = {3, 4, 5};
    parent.localTransform.rotation = {0, .4f, 0};
    parent.localTransform.scale = {2, 2, 2};
    for (int slot = 0; slot < 2; ++slot) {
        auto mesh = source->submeshes.front();
        mesh.name = "Part " + std::to_string(slot);
        mesh.node = first;
        mesh.material = static_cast<int32>(synthetic.materials.size());
        ModelMaterial material;
        material.name = mesh.name;
        material.baseColor = {.2f, .4f, .6f, .75f};
        material.metallic = .3f;
        material.roughness = .8f;
        material.doubleSided = true;
        material.baseColorTexture = static_cast<int32>(synthetic.textures.size());
        ModelTexture texture;
        texture.name = slot ? "Missing texture" : "Embedded palette";
        if (slot) {
            texture.relativePath = "missing-image.png";
        } else {
            std::ifstream input(installed.baseColorTexture, std::ios::binary);
            texture.embedded = std::vector<uint8>(std::istreambuf_iterator<char>(input), {});
            Check(!texture.embedded.empty(), "CC0 palette fixture could not be read");
        }
        synthetic.textures.push_back(std::move(texture));
        synthetic.materials.push_back(std::move(material));
        parent.submeshes.push_back(static_cast<uint32>(synthetic.submeshes.size()));
        synthetic.submeshes.push_back(std::move(mesh));
    }
    synthetic.nodes.push_back(parent);
    ModelNode leaf;
    leaf.name = "Obsolete child";
    leaf.parent = static_cast<int32>(first);
    leaf.localTransform.position = {0, 2, 0};
    synthetic.nodes.push_back(leaf);
    Scene scene;
    const auto sentinel = scene.CreateEntity("Unrelated object").GetId();
    ModelImportResult imported;
    Check(ImportModelAsset(scene, synthetic, root, imported, error), error.c_str());
    Check(scene.IsAlive(sentinel) && imported.entities.size() == synthetic.nodes.size() + 3 &&
              scene.GetParent(imported.root) == 0 &&
              FindImportedModelRoot(scene, FindNode(scene, imported, first)) == imported.root,
          "FBX import did not preserve the existing scene or imported hierarchy");
    const auto parentId = FindNode(scene, imported, first);
    const auto leafId = FindNode(scene, imported, first + 1);
    const auto transform = scene.GetComponent<TransformComponent>(parentId);
    Check(Near(transform.position.x, 3) && Near(transform.rotation.y, .4f) &&
              Near(transform.scale.x, 2) && scene.GetParent(leafId) == parentId,
          "FBX node-local translation, rotation, scale or parenting was lost");
    const auto parts = scene.GetChildren(parentId);
    Check(parts.size() == 3, "FBX material slots were flattened into one renderer");
    bool extracted = false;
    for (const auto id : parts) {
        const auto* mesh = scene.TryGetComponent<MeshRendererComponent>(id);
        if (!mesh)
            continue;
        const auto local = scene.GetComponent<TransformComponent>(id);
        Check(Near(local.position.x, 0) && Near(local.scale.x, 1) &&
                  mesh->meshPath == source->sourcePath && mesh->modelSubmesh >= 0 &&
                  Near(mesh->material.albedoColor.w, .75f) && mesh->material.doubleSided &&
                  Near(mesh->material.metallic, .3f) && Near(mesh->material.roughness, .8f),
              "FBX submesh geometry was transformed twice or material properties were lost");
        if (!mesh->material.baseColorTexture.empty()) {
            extracted = fs::is_regular_file(PathFromUtf8(mesh->material.baseColorTexture));
            Check(PathFromUtf8(mesh->material.baseColorTexture).parent_path() ==
                      fs::canonical(root) / "Assets" / "ImportedTextures",
                  "Embedded FBX texture was extracted outside the project asset folder");
        }
    }
    Check(extracted && !imported.warnings.empty(),
          "Embedded texture extraction or missing texture diagnostics failed");
    std::string before, after;
    Check(scene.SerializeToString(before), "Imported FBX scene could not be serialized");
    synthetic.nodes[first].parent = static_cast<int32>(first + 1);
    ModelImportResult invalid;
    Check(!ImportModelAsset(scene, synthetic, root, invalid, error) && invalid.root == 0 &&
              scene.SerializeToString(after) && before == after,
          "Invalid FBX hierarchy partially modified the destination scene");

    // Source changes remove the two synthetic nodes. Authored children and a separately
    // imported model beneath obsolete nodes must survive; retained nodes keep their IDs.
    const auto retainedNode = FindNode(scene, imported, 0);
    scene.GetComponent<TransformComponent>(imported.root).position = {8, 0, 0};
    auto custom = scene.CreateEntity("Authored child");
    Check(scene.SetParent(custom.GetId(), leafId, false), "Custom child parenting failed");
    const auto oldWorld = scene.GetWorldTransform(custom.GetId());
    auto nested = scene.CreateEntity("Independent model");
    auto& nestedSource = nested.AddComponent<ModelAnimationComponent>();
    nestedSource.modelPath = source->sourcePath;
    nestedSource.applyNodeTransform = false;
    Check(scene.SetParent(nested.GetId(), leafId, false), "Nested model parenting failed");
    auto nestedChild = scene.CreateEntity("Independent child");
    Check(scene.SetParent(nestedChild.GetId(), nested.GetId(), false),
          "Nested model descendant parenting failed");
    Check(FindImportedModelRoot(scene, nested.GetId()) == nested.GetId(),
          "A nested instance of the same FBX source was merged with its parent model");
    ModelImportResult refreshed;
    Check(ReimportModelAsset(scene, parentId, root, refreshed, error), error.c_str());
    Check(refreshed.root == imported.root && FindNode(scene, refreshed, 0) == retainedNode &&
              scene.IsAlive(sentinel) && !scene.IsAlive(parentId) && !scene.IsAlive(leafId) &&
              scene.IsAlive(custom.GetId()) && scene.IsAlive(nested.GetId()) &&
              scene.IsAlive(nestedChild.GetId()) &&
              scene.GetParent(custom.GetId()) == refreshed.root &&
              scene.GetParent(nested.GetId()) == refreshed.root &&
              Near(scene.GetComponent<TransformComponent>(refreshed.root).position.x, 8) &&
              Near(scene.GetWorldTransform(custom.GetId()).position.x, oldWorld.position.x),
          "Reimport lost IDs, wrapper placement, authored children or an independent model");
    Check(scene.SerializeToString(after), "Reimport result did not serialize");
    Scene reopened;
    Check(reopened.DeserializeFromString(after) && reopened.IsAlive(retainedNode) &&
              reopened.GetComponent<ModelAnimationComponent>(retainedNode).nodeIndex == 0,
          "FBX source links did not survive scene serialization");
}

void Editor(const fs::path& root, const BasicModelInstallation& installed) {
    std::string error;
    const auto project =
        EditorProject::Create(root / "EditorProject", "FBX", ProjectTemplate::Empty, error);
    Check(project.has_value(), error.c_str());
    const auto file = project->GetFile().parent_path() / "Assets" / "structured.fbx";
    fs::create_directories(file.parent_path());
    fs::copy_file(installed.modelPath, file);
    const auto model = ModelAsset::LoadFbx(PathUtf8(file), error);
    Check(model != nullptr, error.c_str());
    LuxUI::Settings().clear();
    LuxUI::Settings().setValue(LUX_UI_TEXT("lastProject"),
                               LuxUI::String::fromUtf8(PathUtf8(project->GetFile())));
    EditorWindow window;
    window.show();
    LuxUI::Application::processEvents();
    auto* browser = window.findChild<LuxUI::ListWidget*>(LUX_UI_TEXT("assetGrid"));
    auto* undo = window.findChild<LuxUI::UndoStack*>();
    Check(browser && undo, "FBX Content Browser or undo stack is unavailable");
    const auto files =
        browser->findItems(LUX_UI_TEXT("structured.fbx"), LuxUI::Flags::MatchContains);
    Check(files.size() == 1, "FBX model is missing from the Content Browser");
    browser->setCurrentItem(files.front());
    browser->itemDoubleClicked(files.front());
    auto& document = window.GetDocument();
    const auto wrapper = document.GetSelected().GetId();
    Check(undo->count() == 1 && document.GetScene().GetEntityCount() == model->nodes.size() + 1 &&
              FindImportedModelRoot(document.GetScene(), wrapper) == wrapper,
          "Content Browser FBX placement did not create a hierarchy in one undo command");
    undo->undo();
    Check(document.GetScene().GetEntityCount() == 0 && fs::exists(file),
          "Undo did not remove the full FBX hierarchy while retaining source assets");
    undo->redo();
    Check(document.GetSelected().GetId() == wrapper &&
              document.GetScene().GetEntityCount() == model->nodes.size() + 1,
          "Redo did not restore the FBX hierarchy and selection");
    for (const auto& tab : document.ListTabs())
        document.SetDirty(tab.id, false);
    window.close();
    LuxUI::Application::processEvents();
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application application(argc, argv);
    LuxUI::Application::setOrganizationName(LUX_UI_TEXT("LuxModelImportTests"));
    LuxUI::Application::setApplicationName(LUX_UI_TEXT("ModelImport"));
    LuxUI::TemporaryDir settings;
    LuxUI::TemporaryDir files;
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    try {
        Check(settings.isValid() && files.isValid(), "Temporary FBX test directories failed");
        const auto root = PathFromUtf8(files.path().toUtf8().toStdString());
        BasicModelInstallation installed;
        std::string error;
        Check(BasicModelLibrary::InstallIntoProject("shape-cube", root, installed, error),
              error.c_str());
        Service(root, installed);
        Editor(root, installed);
        std::cout
            << "FBX hierarchy, material slots, textures, reimport, persistence and undo passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
