#include <LuxEngine/Assets/AssetManager.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace LuxEngine;
namespace fs = std::filesystem;

void Check(bool value, const char* description) {
    if (!value)
        throw std::runtime_error(description);
}

bool Near(float a, float b, float tolerance = .0001f) {
    return std::abs(a - b) <= tolerance;
}

std::string Utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

fs::path Path(std::string_view encoded) {
    return fs::path(
        std::u8string(reinterpret_cast<const char8_t*>(encoded.data()), encoded.size()));
}

class Files {
public:
    Files() {
        root_ = fs::temp_directory_path() /
                ("lux-fbx-import-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root_);
    }

    ~Files() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path Path(const fs::path& relative) const {
        return root_ / relative;
    }

    std::string Write(const fs::path& relative, const std::string& contents) const {
        const auto path = Path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        Check(static_cast<bool>(output), "FBX fixture could not be written");
        return Utf8(path);
    }

private:
    fs::path root_;
};

std::string Array(const std::string& name, const std::string& values) {
    const auto count = values.empty() ? 0 : std::count(values.begin(), values.end(), ',') + 1;
    return name + ": *" + std::to_string(count) + " { a: " + values + " }\n";
}

std::string Geometry(const std::string& positions = "0,0,0,1,0,0,1,1,0,0,1,0",
                     const std::string& indices = "0,1,2,-4", bool uv = true,
                     const std::string& normals = "") {
    auto result = std::string("Geometry: 1001, \"Geometry::Fixture\", \"Mesh\" {\n") +
                  "GeometryVersion: 124\n" + Array("Vertices", positions) +
                  Array("PolygonVertexIndex", indices);
    if (!normals.empty())
        result += "LayerElementNormal: 0 { Version: 101 Name: \"\" "
                  "MappingInformationType: \"AllSame\" ReferenceInformationType: \"Direct\"\n" +
                  Array("Normals", normals) + "}\n";
    if (uv)
        result += "LayerElementUV: 0 { Version: 101 Name: \"UVMap\" "
                  "MappingInformationType: \"ByPolygonVertex\" "
                  "ReferenceInformationType: \"IndexToDirect\"\n" +
                  Array("UV", "0,0,1,0,1,1,0,1") + Array("UVIndex", "0,1,2,3") + "}\n";
    return result + "}\n";
}

std::string Model(int id = 2001, const std::string& translation = "0,0,0",
                  const std::string& scale = "1,1,1", const std::string& geometry = "0,0,0") {
    return "Model: " + std::to_string(id) +
           ", \"Model::Fixture\", \"Mesh\" {\n"
           "Version: 232\n Properties70: {\n"
           "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", " +
           translation + "\nP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\", " + scale +
           "\nP: \"GeometricTranslation\", \"Vector3D\", \"Vector\", \"\", " + geometry +
           "\n}\n}\n";
}

std::string Ascii(const std::string& objects,
                  const std::string& connections = "C: \"OO\", 1001, 2001\n"
                                                   "C: \"OO\", 2001, 0\n",
                  int up = 1, int front = 2, int frontSign = 1, int unit = 100) {
    return "; FBX 7.4.0 project file, authored by the LuxEngine test fixture\n"
           "FBXHeaderExtension: { FBXHeaderVersion: 1003 FBXVersion: 7400 }\n"
           "GlobalSettings: { Version: 1000 Properties70: {\n"
           "P: \"UpAxis\", \"int\", \"Integer\", \"\", " +
           std::to_string(up) +
           "\nP: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
           "P: \"FrontAxis\", \"int\", \"Integer\", \"\", " +
           std::to_string(front) + "\nP: \"FrontAxisSign\", \"int\", \"Integer\", \"\", " +
           std::to_string(frontSign) +
           "\nP: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
           "P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
           "P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", " +
           std::to_string(unit) + "\n}}\nObjects: {\n" + objects + "}\nConnections: {\n" +
           connections + "}\n";
}

void CheckMesh(const RawMeshData& mesh) {
    Check(!mesh.vertices.empty() && !mesh.indices.empty() && mesh.vertices.size() % 3 == 0 &&
              mesh.indices.size() % 3 == 0 && mesh.normals.size() == mesh.vertices.size() &&
              mesh.texCoords.size() == mesh.vertices.size() / 3 * 2,
          "Imported FBX arrays do not form a complete renderer mesh");
    const auto finite = [](float value) { return std::isfinite(value); };
    Check(std::all_of(mesh.vertices.begin(), mesh.vertices.end(), finite) &&
              std::all_of(mesh.normals.begin(), mesh.normals.end(), finite) &&
              std::all_of(mesh.texCoords.begin(), mesh.texCoords.end(), finite),
          "Imported FBX attributes contain non-finite values");
    for (const auto index : mesh.indices)
        Check(index < mesh.vertices.size() / 3, "Imported FBX has an invalid index");
    for (size_t index = 0; index < mesh.normals.size(); index += 3)
        Check(
            Near(std::hypot(mesh.normals[index], mesh.normals[index + 1], mesh.normals[index + 2]),
                 1, .001f),
            "Imported FBX normals are not normalized");
    for (size_t index = 0; index < mesh.vertices.size(); index += 3)
        Check(mesh.vertices[index] >= mesh.boundsMin.x &&
                  mesh.vertices[index] <= mesh.boundsMax.x &&
                  mesh.vertices[index + 1] >= mesh.boundsMin.y &&
                  mesh.vertices[index + 1] <= mesh.boundsMax.y &&
                  mesh.vertices[index + 2] >= mesh.boundsMin.z &&
                  mesh.vertices[index + 2] <= mesh.boundsMax.z,
              "Imported FBX bounds do not enclose its positions");
}

RawMeshData Load(const std::string& path) {
    RawMeshData mesh;
    if (!AssetManager::LoadMesh(path, mesh, true))
        throw std::runtime_error("FBX fixture load failed: " + AssetManager::GetAssetError(path));
    CheckMesh(mesh);
    return mesh;
}

void AsciiGeometryAndInstances(const Files& files) {
    const auto path =
        files.Write(fs::path(u8"한글 경로") / fs::path(u8"모델.FBX"), Ascii(Geometry() + Model()));
    auto mesh = Load(path);
    bool hasUvs = false;
    Check(AssetManager::ValidateMeshFile(path, &hasUvs) && hasUvs && mesh.hasTexCoords &&
              mesh.indices.size() == 6 && Near(mesh.boundsMin.x, 0) && Near(mesh.boundsMax.x, 1) &&
              Near(mesh.boundsMax.y, 1),
          "ASCII FBX, uppercase extension, Unicode path, UV or bounds import failed");
    auto objects = Geometry() + Model(2001, "2,0,0", "1,1,1", "3,0,0") +
                   "Model: 3001, \"Model::Parent\", \"Null\" { Version: 232 Properties70: {"
                   "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", 10,0,0 }}\n" +
                   Model(2002, "-5,0,0");
    mesh = Load(files.Write("instances.fbx", Ascii(objects, "C: \"OO\",1001,2001\n"
                                                            "C: \"OO\",2001,3001\n"
                                                            "C: \"OO\",3001,0\n"
                                                            "C: \"OO\",1001,2002\n"
                                                            "C: \"OO\",2002,0\n")));
    Check(mesh.indices.size() == 12 && Near(mesh.boundsMin.x, -5) && Near(mesh.boundsMax.x, 16),
          "FBX instances, hierarchy or geometric transforms were lost during baking");
    mesh = Load(files.Write("z-up-centimeters.fbx",
                            Ascii(Geometry("0,0,0,100,0,0,0,0,100", "0,1,-3", false) + Model(),
                                  "C: \"OO\",1001,2001\nC: \"OO\",2001,0\n", 2, 1, -1, 1)));
    Check(Near(mesh.boundsMax.x, 1) && Near(mesh.boundsMax.y, 1) && Near(mesh.boundsMin.z, 0) &&
              Near(mesh.boundsMax.z, 0) && !mesh.hasTexCoords,
          "FBX axis or centimeter-to-meter conversion failed");
}

void TextureCoordinateOrigin(const Files& files) {
    auto geometry = Geometry("0,0,0,1,0,0,1,1,0,0,1,0", "0,1,2,-4", false);
    geometry.insert(geometry.rfind('}'),
                    "LayerElementUV: 0 { Version: 101 Name: \"UVMap\" "
                    "MappingInformationType: \"ByPolygonVertex\" "
                    "ReferenceInformationType: \"IndexToDirect\"\n" +
                        Array("UV", "0.125,0.125,0.875,0.375,0.625,0.875,0.375,0.625") +
                        Array("UVIndex", "0,1,2,3") + "}\n");
    const auto mesh = Load(files.Write("asymmetric-uv.fbx", Ascii(geometry + Model())));
    constexpr std::array<Vec2, 4> source{
        {{.125f, .125f}, {.875f, .375f}, {.625f, .875f}, {.375f, .625f}}};
    Check(mesh.hasTexCoords, "A complete FBX UV layer was not preserved");
    for (size_t index = 0; index < mesh.vertices.size() / 3; ++index) {
        const auto x = mesh.vertices[index * 3];
        const auto y = mesh.vertices[index * 3 + 1];
        const size_t corner = x == 0 ? (y == 0 ? 0 : 3) : (y == 0 ? 1 : 2);
        Check(mesh.texCoords[index * 2] == source[corner].x &&
                  mesh.texCoords[index * 2 + 1] == 1.0f - source[corner].y,
              "FBX UV normalization must preserve U and map V to exactly 1 minus source V");
    }
}

void NormalsSeamsAndPolygons(const Files& files) {
    auto mesh = Load(
        files.Write("mirrored.fbx", Ascii(Geometry("0,0,0,1,0,0,0,1,1", "0,1,-3", false, "0,-1,1") +
                                          Model(2001, "0,0,0", "-2,3,4"))));
    Check(Near(mesh.normals[1], -.8f) && Near(mesh.normals[2], .6f),
          "FBX normals did not use the inverse-transpose of non-uniform scale");
    const auto position = [&](uint32 index) {
        return Vec3{mesh.vertices[index * 3], mesh.vertices[index * 3 + 1],
                    mesh.vertices[index * 3 + 2]};
    };
    const auto faceNormal = (position(mesh.indices[1]) - position(mesh.indices[0]))
                                .Cross(position(mesh.indices[2]) - position(mesh.indices[0]))
                                .Normalized();
    Check(faceNormal.y * mesh.normals[1] + faceNormal.z * mesh.normals[2] > .99f,
          "A mirrored FBX transform inverted front faces relative to their normals");
    auto geometry = Geometry("0,0,0,1,0,0,1,1,0,0,1,0", "0,1,-3,0,2,-4", false, "0,0,1");
    geometry.insert(geometry.rfind('}'), "LayerElementUV: 0 { Version: 101 Name: \"UVMap\" "
                                         "MappingInformationType: \"ByPolygonVertex\" "
                                         "ReferenceInformationType: \"IndexToDirect\"\n" +
                                             Array("UV", "0,0,1,0,1,1,0.25,0.25,0,1") +
                                             Array("UVIndex", "0,1,2,3,2,4") + "}\n");
    mesh = Load(files.Write("uv-seams.fbx", Ascii(geometry + Model())));
    Check(mesh.hasTexCoords && mesh.vertices.size() / 3 == 5,
          "FBX UV seams were collapsed into a shared position vertex");
    mesh = Load(files.Write(
        "concave.fbx",
        Ascii(Geometry("0,0,0,2,0,0,2,2,0,1,1,0,0,2,0", "0,1,2,3,-5", false) + Model())));
    double area = 0;
    for (size_t index = 0; index < mesh.indices.size(); index += 3) {
        const auto a = position(mesh.indices[index]);
        const auto b = position(mesh.indices[index + 1]);
        const auto c = position(mesh.indices[index + 2]);
        area += std::abs((b - a).Cross(c - a).z) * .5;
    }
    Check(mesh.indices.size() == 9 && std::abs(area - 3) < .0001,
          "Concave FBX n-gons were not triangulated without overlap");
}

using Property =
    std::variant<int32_t, int64_t, std::string, std::vector<int32_t>, std::vector<double>>;

struct BinaryNode {
    std::string name;
    std::vector<Property> properties;
    std::vector<BinaryNode> children;
};

void Integer(std::string& output, uint64_t value, size_t bytes) {
    for (size_t index = 0; index < bytes; ++index)
        output.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
}

std::string Encode(const Property& property) {
    std::string result;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
                result.push_back(std::is_same_v<T, int32_t> ? 'I' : 'L');
                Integer(result, static_cast<uint64_t>(value), sizeof(T));
            } else if constexpr (std::is_same_v<T, std::string>) {
                result.push_back('S');
                Integer(result, value.size(), 4);
                result += value;
            } else {
                using E = typename T::value_type;
                result.push_back(std::is_same_v<E, double> ? 'd' : 'i');
                Integer(result, value.size(), 4);
                Integer(result, 0, 4); // Uncompressed arrays keep the fixture self-contained.
                Integer(result, value.size() * sizeof(E), 4);
                for (const auto element : value) {
                    if constexpr (std::is_same_v<E, double>)
                        Integer(result, std::bit_cast<uint64_t>(element), sizeof(E));
                    else
                        Integer(result, static_cast<uint32_t>(element), sizeof(E));
                }
            }
        },
        property);
    return result;
}

void EncodeNode(std::string& output, const BinaryNode& node, uint32_t version) {
    const size_t integerSize = version >= 7500 ? 8 : 4;
    const size_t headerSize = integerSize * 3 + 1;
    const auto start = output.size();
    std::string properties;
    for (const auto& property : node.properties)
        properties += Encode(property);
    output.resize(start + headerSize, '\0');
    output += node.name;
    output += properties;
    for (const auto& child : node.children)
        EncodeNode(output, child, version);
    if (!node.children.empty())
        output.append(headerSize, '\0');
    std::string header;
    Integer(header, output.size(), integerSize);
    Integer(header, node.properties.size(), integerSize);
    Integer(header, properties.size(), integerSize);
    header.push_back(static_cast<char>(node.name.size()));
    output.replace(start, headerSize, header);
}

std::string Binary(uint32_t version,
                   std::vector<double> positions = {0, 0, 0, 100, 0, 0, 0, 100, 0}) {
    std::string output("Kaydara FBX Binary  \0\x1a\0", 23);
    Integer(output, version, 4);
    const std::vector<BinaryNode> nodes{
        {"FBXHeaderExtension",
         {},
         {{"FBXHeaderVersion", {int32_t{1003}}, {}},
          {"FBXVersion", {static_cast<int32_t>(version)}, {}}}},
        {"Objects",
         {},
         {{"Geometry",
           {int64_t{1001}, std::string("Geometry::Binary"), std::string("Mesh")},
           {{"GeometryVersion", {int32_t{124}}, {}},
            {"Vertices", {std::move(positions)}, {}},
            {"PolygonVertexIndex", {std::vector<int32_t>{0, 1, -3}}, {}}}},
          {"Model", {int64_t{2001}, std::string("Model::Binary"), std::string("Mesh")}, {}}}},
        {"Connections",
         {},
         {{"C", {std::string("OO"), int64_t{1001}, int64_t{2001}}, {}},
          {"C", {std::string("OO"), int64_t{2001}, int64_t{0}}, {}}}}};
    for (const auto& node : nodes)
        EncodeNode(output, node, version);
    output.append(version >= 7500 ? 25 : 13, '\0');
    return output;
}

void BinaryVersions(const Files& files) {
    for (const auto version : {7400u, 7700u}) {
        const auto mesh =
            Load(files.Write("binary-" + std::to_string(version) + ".fbx", Binary(version)));
        Check(mesh.indices.size() == 3 && Near(mesh.boundsMax.x, 1) && Near(mesh.boundsMax.y, 1) &&
                  !mesh.hasTexCoords,
              "Binary FBX version, generated normals or default centimeter conversion failed");
        Check(std::all_of(mesh.texCoords.begin(), mesh.texCoords.end(),
                          [](float value) { return value == 0; }),
              "A missing FBX UV layer must keep the zero-coordinate fallback");
    }
}

void Rejected(const Files& files, const std::string& name, const std::string& source,
              const char* expected = nullptr) {
    const auto path = files.Write(name, source);
    RawMeshData mesh;
    mesh.vertices = {1, 2, 3};
    bool uv = true;
    Check(!AssetManager::LoadMesh(path, mesh, true) && mesh.vertices.empty() &&
              mesh.indices.empty() && !AssetManager::ValidateMeshFile(path, &uv) && !uv,
          "An invalid FBX left partial output or passed validation");
    const auto message = AssetManager::GetAssetError(path);
    Check(!message.empty(), "Invalid FBX diagnostics were not preserved");
    if (expected && message.find(expected) == std::string::npos)
        throw std::runtime_error("FBX fixture failed for the wrong reason: " + message);
}

void ValidationAndReload(const Files& files) {
    Rejected(files, "empty.fbx", "");
    Rejected(files, "wrong-format.fbx", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    auto truncated = Binary(7700);
    truncated.resize(truncated.size() / 2);
    Rejected(files, "truncated.fbx", truncated);
    Rejected(files, "bad-index.fbx",
             Ascii(Geometry("0,0,0,1,0,0,0,1,0", "0,1,-99", false) + Model()));
    Rejected(files, "overflow.fbx", Binary(7700, {0, 0, 0, 1e300, 0, 0, 0, 1, 0}));
    Rejected(files, "nan.fbx",
             Binary(7700, {0, 0, 0, std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 1, 0}));
    Rejected(files, "singular.fbx", Ascii(Geometry() + Model(2001, "0,0,0", "1,1,0")));
    std::ostringstream positions, polygon;
    positions.imbue(std::locale::classic());
    polygon.imbue(std::locale::classic());
    for (int index = 0; index < 1025; ++index) {
        if (index) {
            positions << ',';
            polygon << ',';
        }
        const auto angle = index * (6.283185307179586 / 1025);
        positions << std::cos(angle) << ',' << std::sin(angle) << ",0";
        polygon << (index == 1024 ? -1025 : index);
    }
    Rejected(files, "oversized-polygon.fbx",
             Ascii(Geometry(positions.str(), polygon.str(), false) + Model()), "한도");
    auto deepObjects = Geometry() + Model();
    std::string deepConnections = "C: \"OO\",1001,2001\nC: \"OO\",2001,3000\n";
    for (int index = 0; index < 270; ++index) {
        deepObjects += Model(3000 + index);
        deepConnections += "C: \"OO\"," + std::to_string(3000 + index) + ',' +
                           std::to_string(index == 269 ? 0 : 3001 + index) + '\n';
    }
    Rejected(files, "too-deep.fbx", Ascii(deepObjects, deepConnections));
    Rejected(files, "skin.fbx",
             Ascii(Geometry() + Model() +
                       "Deformer: 3001, \"Deformer::Skin\", \"Skin\" { Version: 101 }\n",
                   "C: \"OO\",1001,2001\nC: \"OO\",2001,0\nC: \"OO\",3001,1001\n"),
             "스킨");
    Rejected(files, "morph.fbx",
             Ascii(Geometry() + Model() +
                       "Deformer: 3001, \"Deformer::Blend\", \"BlendShape\" { Version: 100 }\n",
                   "C: \"OO\",1001,2001\nC: \"OO\",2001,0\nC: \"OO\",3001,1001\n"),
             "모프");
    const auto large = files.Write("oversized.fbx", "x");
    fs::resize_file(files.Path("oversized.fbx"), 64ull * 1024 * 1024 + 1);
    RawMeshData mesh;
    Check(!AssetManager::LoadMesh(large, mesh, true),
          "An oversized FBX was read before its size limit");
    const auto path = files.Write("reload.fbx", Ascii(Geometry() + Model()));
    const auto original = Load(path);
    const auto revision = AssetManager::GetMeshRevision(path);
    files.Write("reload.fbx", "not an FBX");
    std::string error;
    Check(!AssetManager::ReloadMesh(path, error) && !error.empty() &&
              AssetManager::GetMeshRevision(path) == revision &&
              AssetManager::LoadMesh(path, mesh) && mesh.vertices == original.vertices &&
              mesh.indices == original.indices && mesh.revision == original.revision,
          "A failed FBX reload replaced the last valid cached mesh or revision");
    files.Write("reload.fbx",
                Ascii(Geometry("0,0,0,2,0,0,2,1,0,0,1,0", "0,1,2,-4", false) + Model()));
    Check(!AssetManager::ReloadMesh(path, error, true) && !error.empty() &&
              AssetManager::GetMeshRevision(path) == revision &&
              AssetManager::LoadMesh(path, mesh) && mesh.hasTexCoords,
          "UV-required FBX reload did not preserve the last valid textured mesh");
    Check(AssetManager::ReloadMesh(path, error) && error.empty() &&
              AssetManager::GetMeshRevision(path) > revision &&
              AssetManager::LoadMesh(path, mesh) && Near(mesh.boundsMax.x, 2) && !mesh.hasTexCoords,
          "A valid FBX reload did not publish geometry, bounds and revision together");
}

void PrimitiveAssets(const fs::path& directory) {
    if (directory.empty())
        return;
    size_t count = 0;
    for (const auto& file : fs::directory_iterator(directory))
        if (file.path().extension() == ".fbx") {
            const auto mesh = Load(Utf8(file.path()));
            Check(mesh.hasTexCoords, "A supplied primitive FBX has no complete UV coordinates");
            ++count;
        }
    Check(count >= 8, "The supplied primitive FBX collection is incomplete");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Files files;
        AssetManager::Clear();
        AsciiGeometryAndInstances(files);
        TextureCoordinateOrigin(files);
        NormalsSeamsAndPolygons(files);
        BinaryVersions(files);
        ValidationAndReload(files);
        if (argc > 1)
            PrimitiveAssets(Path(argv[1]));
#ifdef LUX_FBX_TEST_ASSET_DIR
        else
            PrimitiveAssets(Path(LUX_FBX_TEST_ASSET_DIR));
#endif
        AssetManager::Clear();
        std::cout
            << "Static FBX geometry, transforms, validation, reload and primitive assets passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
