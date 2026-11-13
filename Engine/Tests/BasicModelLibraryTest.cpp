#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace LuxEngine;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string Bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    Check(input.is_open(), "Package fixture file is unavailable");
    return {std::istreambuf_iterator<char>(input), {}};
}

fs::path FixtureDirectory() {
    auto name = fs::path(u8"Lux 기본 모델 ").native();
    name += fs::path(std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
                .native();
    return fs::temp_directory_path() / fs::path(name);
}

struct Fixture {
    fs::path root = FixtureDirectory();
    fs::path source = BasicModelLibrary::FindLibraryDirectory();
    fs::path project = root / "project";

    Fixture() {
        Check(!source.empty(), "Installed basic model package was not found");
        fs::create_directories(project);
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void Catalog() {
    const auto models = BasicModelLibrary::GetModels();
    Check(models.size() == 10, "Basic model catalog must expose the ten packaged models");
    std::set<std::string> ids;
    for (const auto& model : models) {
        Check(ids.insert(model.id).second && model.name[0] != '\0' &&
                  fs::path(model.fileName).extension() == ".fbx" &&
                  BasicModelLibrary::FindModel(model.id) == &model,
              "Catalog model identity and file mapping are invalid");
    }
    Check(!BasicModelLibrary::FindModel("../shape-cube") && !BasicModelLibrary::FindModel("sphere"),
          "Unknown models must not silently fall back to a cube");
}

void InstallationAndReuse() {
    Fixture fixture;
    BasicModelInstallation first, second, conflicting;
    std::string error;
    Check(BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project, first, error) &&
              error.empty(),
          "Basic model package did not install into a project with Unicode and spaces");
    Check(first.directory ==
                  fs::canonical(fixture.project) / "Assets/Models/Primitives/KenneyPrototype" &&
              first.modelPath.filename() == "shape-cube.fbx" &&
              first.baseColorTexture == first.directory / "Textures/colormap.png",
          "Installed paths must remain in the project's model content directory");
    size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(fixture.source)) {
        if (!entry.is_regular_file())
            continue;
        const auto relative = entry.path().lexically_relative(fixture.source);
        Check(Bytes(entry.path()) == Bytes(first.directory / relative),
              "The original model, texture, license or provenance bytes changed during import");
        ++files;
    }
    Check(files == 13, "Complete model package must retain its thirteen files");
    Check(BasicModelLibrary::InstallIntoProject("shape-cylinder", fixture.project, second, error) &&
              second.directory == first.directory &&
              second.modelPath.filename() == "shape-cylinder.fbx",
          "Adding another primitive must reuse the identical installed package");
    BasicModelInstallation identified;
    Check(BasicModelLibrary::IdentifyInstalledModel(first.modelPath, identified) &&
              identified.baseColorTexture == first.baseColorTexture,
          "Recognized package files must retain the original palette texture");
    std::ofstream(first.modelPath, std::ios::binary) << "user-edited model";
    Check(!BasicModelLibrary::IdentifyInstalledModel(first.modelPath, identified),
          "Modified package files must not be mistaken for original bundled content");
    Check(
        BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project, conflicting, error) &&
            conflicting.directory.filename() == "KenneyPrototype-2" &&
            Bytes(first.modelPath) == "user-edited model" &&
            Bytes(conflicting.modelPath) == Bytes(fixture.source / "shape-cube.fbx"),
        "Conflicting user content must survive while a separate original package is installed");
    Check(BasicModelLibrary::InstallIntoProject("floor-square", fixture.project, second, error) &&
              second.directory == conflicting.directory,
          "A matching numbered package must be reused instead of copied repeatedly");
    for (const auto& entry : fs::directory_iterator(conflicting.directory.parent_path()))
        Check(!entry.path().filename().string().starts_with(".KenneyPrototype-install-"),
              "A successful package publication must not leave staging directories");
}

void InvalidInputs() {
    Fixture fixture;
    BasicModelInstallation installation;
    std::string error;
    Check(!BasicModelLibrary::InstallIntoProject("missing", fixture.project, installation, error) &&
              !error.empty() && installation.modelPath.empty() &&
              !fs::exists(fixture.project / "Assets"),
          "Unknown model IDs must fail without changing project content");
    Check(!BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project / "..", installation,
                                                 error) &&
              !fs::exists(fixture.root / "Assets"),
          "Parent traversal in the destination must not escape the requested project");
    const auto incomplete = fixture.root / "incomplete";
    fs::copy(fixture.source, incomplete, fs::copy_options::recursive);
    fs::remove(incomplete / "License.txt");
    Check(!BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project, installation, error,
                                                 incomplete) &&
              !error.empty() && !fs::exists(fixture.project / "Assets"),
          "An incomplete licensed package must fail before any project content is created");
}

void SymlinkBoundaries() {
    Fixture fixture;
    const auto outside = fixture.root / "outside";
    fs::create_directories(outside);
    std::error_code error;
    fs::create_directory_symlink(outside, fixture.project / "Assets", error);
    if (error)
        return; // Some Windows accounts do not have symlink creation privileges.
    BasicModelInstallation installation;
    std::string message;
    Check(!BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project, installation,
                                                 message) &&
              !message.empty() && fs::is_empty(outside),
          "A project content symlink must not redirect installed models outside the project");
    fs::remove(fixture.project / "Assets");
    const auto source = fixture.root / "linked-source";
    fs::copy(fixture.source, source, fs::copy_options::recursive);
    fs::remove(source / "License.txt");
    const auto external = outside / "License.txt";
    std::ofstream(external) << "external content";
    fs::create_symlink(external, source / "License.txt", error);
    if (error)
        return;
    Check(!BasicModelLibrary::InstallIntoProject("shape-cube", fixture.project, installation,
                                                 message, source) &&
              !fs::exists(fixture.project / "Assets") && Bytes(external) == "external content",
          "Package source symlinks must be rejected without reading external package content");
}

void ConcurrentInstallation() {
    Fixture fixture;
    std::array<BasicModelInstallation, 4> installations;
    std::array<std::string, 4> errors;
    std::array<bool, 4> installed{};
    {
        std::array<std::jthread, 4> workers;
        for (size_t index = 0; index < workers.size(); ++index)
            workers[index] = std::jthread([&, index] {
                installed[index] = BasicModelLibrary::InstallIntoProject(
                    "shape-cube", fixture.project, installations[index], errors[index]);
            });
    }
    for (size_t index = 0; index < installed.size(); ++index)
        Check(installed[index] && errors[index].empty() &&
                  installations[index].directory == installations.front().directory,
              "Concurrent library callers must share a complete package without collisions");
}
} // namespace

int main() {
    try {
        Catalog();
        InstallationAndReuse();
        InvalidInputs();
        SymlinkBoundaries();
        ConcurrentInstallation();
        std::cout
            << "Basic model catalog, licensed installation, reuse and conflict boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
