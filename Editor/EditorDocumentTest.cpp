#include "Editor/EditorDocument.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("lux-editor-document-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    TempDirectory() { std::filesystem::create_directories(path); }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void TestDocument() {
    using namespace LuxEngine;

    TempDirectory files;
    const std::string savedPath = (files.path / "scene.lux").string();
    const std::string invalidPath = (files.path / "invalid.lux").string();

    EditorDocument document;
    Check(!document.IsDirty() && document.GetPath().empty(), "new document state");
    Check(document.GetScene().GetEntityCount() == 0 && !document.GetSelected(), "new scene is empty");

    auto hero = document.CreateEntity("Hero");
    Check(hero.IsValid() && document.GetSelected() == hero, "created entity is selected");
    Check(document.IsDirty() && document.GetScene().GetEntityCount() == 1, "creation marks document dirty");
    Check(!document.Save() && document.IsDirty(), "save without a path fails safely");
    hero.GetComponent<TransformComponent>().position = {3.0f, 4.0f, 0.0f};
    Check(document.SaveAs(savedPath), "save as succeeds");
    Check(!document.IsDirty() && document.GetPath() == savedPath, "save as records path and clears dirty state");

    hero.GetComponent<TagComponent>().tag = "Renamed";
    document.MarkDirty();
    Check(document.IsDirty(), "component edit marks document dirty");
    Check(document.Save() && !document.IsDirty(), "save updates current path");

    auto transient = document.CreateEntity("Transient");
    Check(document.GetScene().GetEntityCount() == 2 && document.GetSelected() == transient, "second entity is selected");
    document.Select(hero.GetId());
    Check(document.GetSelected() == hero, "entity selection by ID");
    Check(document.DeleteSelected(), "selected entity is deleted");
    Check(!hero.IsValid() && !document.GetSelected(), "deleted entity clears selection");
    Check(document.GetScene().GetEntityCount() == 1 && document.IsDirty(), "deletion marks document dirty");
    document.Select(999999u);
    Check(!document.GetSelected() && !document.DeleteSelected(), "invalid selection cannot delete an entity");

    std::ofstream(invalidPath) << "invalid scene";
    document.Select(transient.GetId());
    Check(!document.Open(invalidPath), "invalid scene fails to open");
    Check(document.IsDirty() && document.GetPath() == savedPath, "failed open preserves document state");
    Check(document.GetScene().GetEntityCount() == 1 && document.GetSelected() == transient,
          "failed open preserves scene and selection");
    Check(!document.Open((files.path / "missing.lux").string()), "missing scene fails to open");
    Check(document.GetSelected() == transient, "missing scene preserves selection");

    Check(document.Open(savedPath), "saved scene reopens");
    Check(!document.IsDirty() && document.GetPath() == savedPath, "open clears dirty state");
    Check(document.GetScene().GetEntityCount() == 1 && !document.GetSelected(), "open replaces scene and clears selection");
    Check(!transient.IsValid(), "old entity handles expire after open");
    auto entities = document.GetScene().GetEntities();
    Check(entities.size() == 1 && entities[0].GetComponent<TagComponent>().tag == "Renamed",
          "saved entity name survives roundtrip");
    Check(entities[0].GetComponent<TransformComponent>().position.x == 3.0f &&
          entities[0].GetComponent<TransformComponent>().position.y == 4.0f,
          "saved transform survives roundtrip");

    document.Select(entities[0].GetId());
    document.NewScene();
    Check(document.GetScene().GetEntityCount() == 0 && !document.GetSelected(), "new scene clears entities and selection");
    Check(document.GetPath().empty() && !document.IsDirty(), "new scene clears path and dirty state");
}

} // namespace

int main() {
    try {
        TestDocument();
        std::cout << "Editor document checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor document checks failed: " << error.what() << '\n';
        return 1;
    }
}
