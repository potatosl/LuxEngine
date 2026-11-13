#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/EntryPoint.hpp>
#include <RainyCreek/Catalog.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {
namespace rc = RainyCreek;
namespace fs = std::filesystem;
using Json = nlohmann::json;

void Check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("LuxRainyCatalog-" + std::to_string(stamp));
        Check(fs::create_directory(path_), "Could not create a catalog test directory");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const {
        return path_;
    }

private:
    fs::path path_;
};

void Write(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    output.close();
    Check(static_cast<bool>(output), "Could not write a catalog test fixture");
}

Json MakeFixture(const fs::path& root, const rc::Catalog& source) {
    Check(fs::create_directory(root), "Could not create the catalog fixture");
    fs::copy_file(source.ground, root / "image.png");
    fs::copy_file(source.day, root / "day.hdr");
    fs::copy_file(source.dusk, root / "dusk.hdr");
    Write(root / "mesh.obj", "v -0.5 0 0\nv 0.5 0 0\nv 0 1 0\n"
                             "vt 0 0\nvt 1 0\nvt 0.5 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
    Json catalog = {{"schema", "luxengine.rainy_creek.catalog.v1"},
                    {"textures", {{"ground", "image.png"}}},
                    {"environments", {{"day", "day.hdr"}, {"dusk", "dusk.hdr"}}},
                    {"models", Json::object()}};
    for (const auto* id : {"rock_a", "rock_b", "rock_c", "fern_a", "fern_b", "trunk", "pine"})
        catalog["models"][id] = {{"parts", Json::array({{{"mesh", "mesh.obj"},
                                                         {"albedo", "image.png"},
                                                         {"roughness", .7f},
                                                         {"alphaMask", false}}})}};
    Write(root / "catalog.json", catalog.dump());
    return catalog;
}

template <class Operation> void Rejects(Operation operation, const char* description) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected, description);
    LuxEngine::AssetManager::Clear();
}

void CheckCatalog(const rc::Catalog& catalog) {
    Check(catalog.models.size() == 7, "Catalog did not load the seven required models");
    for (const auto& [id, model] : catalog.models) {
        (void)id;
        Check(!model.parts.empty(), "A required model has no geometry");
        for (const float value : {model.minimum.x, model.minimum.y, model.minimum.z,
                                  model.maximum.x, model.maximum.y, model.maximum.z})
            Check(std::isfinite(value), "Model bounds are not finite");
        Check(model.minimum.x <= model.maximum.x && model.minimum.y <= model.maximum.y &&
                  model.minimum.z <= model.maximum.z,
              "Model bounds are inverted");
        for (const auto* parts : {&model.parts, &model.farParts})
            for (const auto& part : *parts)
                Check(fs::is_regular_file(part.mesh) && fs::is_regular_file(part.albedo) &&
                          std::isfinite(part.roughness) && part.roughness >= 0 &&
                          part.roughness <= 1,
                      "A model part has an invalid resource or material");
    }
}

void InvalidCatalogs(const fs::path& outer, const fs::path& root, const Json& valid) {
    const auto reject = [&](Json invalid) {
        Write(root / "catalog.json", invalid.dump());
        Rejects([&] { (void)rc::LoadCatalog(root); }, "Invalid catalog was accepted");
    };
    Rejects([&] { (void)rc::LoadCatalog(outer / "missing"); }, "Missing catalog was accepted");
    Write(root / "catalog.json", "{invalid json");
    Rejects([&] { (void)rc::LoadCatalog(root); }, "Malformed JSON was accepted");

    auto invalid = valid;
    invalid["schema"] = "unsupported.catalog.v99";
    reject(invalid);
    invalid = valid;
    invalid.erase("textures");
    reject(invalid);
    invalid = valid;
    invalid["models"].erase("pine");
    reject(invalid);
    invalid = valid;
    invalid["models"]["pine"]["parts"] = Json::array();
    reject(invalid);
    invalid = valid;
    invalid["models"]["pine"]["parts"][0]["mesh"] = "missing.obj";
    reject(invalid);
    invalid = valid;
    invalid["models"]["pine"]["parts"][0]["roughness"] = -0.1;
    reject(invalid);
    invalid["models"]["pine"]["parts"][0]["roughness"] = 1.1;
    reject(invalid);
    invalid["models"]["pine"]["parts"][0]["roughness"] = "nan";
    reject(invalid);

    Write(root / "invalid.obj", "This is not an OBJ mesh.\n");
    invalid = valid;
    invalid["models"]["pine"]["parts"][0]["mesh"] = "invalid.obj";
    reject(invalid);
    invalid = valid;
    invalid["textures"]["ground"] = rc::ToUtf8(root / "image.png");
    reject(invalid);
    Write(outer / "outside.png", "outside fixture");
    invalid = valid;
    invalid["textures"]["ground"] = "../outside.png";
    reject(invalid);

    std::error_code symlinkError;
    fs::create_symlink(outer / "outside.png", root / "escape.png", symlinkError);
    if (!symlinkError) {
        invalid = valid;
        invalid["textures"]["ground"] = "escape.png";
        reject(invalid);
    }

    Write(root / "catalog.json", valid.dump());
    const auto restored = rc::LoadCatalog(root);
    CheckCatalog(restored);
    rc::ValidateCatalog(restored);

    auto badImage = restored;
    Write(root / "invalid.png", "not an image");
    badImage.ground = root / "invalid.png";
    Rejects([&] { rc::ValidateCatalog(badImage); }, "Invalid image data was accepted");

    auto badHdr = restored;
    Write(root / "invalid.hdr", "unsupported HDR signature\n");
    badHdr.day = root / "invalid.hdr";
    Rejects([&] { rc::ValidateCatalog(badHdr); }, "Invalid HDR signature was accepted");
}

int RunCatalogTests(int argc, char** argv) {
    try {
        Check(argc == 2, "Usage: RainyCreekCatalogTests ASSET_ROOT");
        const auto catalog = rc::LoadCatalog(rc::FromUtf8(argv[1]));
        CheckCatalog(catalog);
        rc::ValidateCatalog(catalog);
        TemporaryDirectory temporary;
        const auto root = temporary.Path() / "catalog";
        const auto fixture = MakeFixture(root, catalog);
        const auto loadedFixture = rc::LoadCatalog(root);
        CheckCatalog(loadedFixture);
        rc::ValidateCatalog(loadedFixture);
        InvalidCatalogs(temporary.Path(), root, fixture);
        std::cout << "Rainy Creek catalog tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        LuxEngine::AssetManager::Clear();
        std::cerr << error.what() << '\n';
        return 1;
    }
}

} // namespace

LUX_ENGINE_MAIN(RunCatalogTests)
