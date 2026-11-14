#include <LuxEngine/Assets/ModelAsset.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace LuxEngine;
namespace fs = std::filesystem;

namespace {

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float left, float right) {
    return std::abs(left - right) < 0.001f;
}

std::string Fixture(bool skinned) {
    const std::string skin = skinned ? R"(
Model: 3001,"Model::Bone","LimbNode" {
 Version:232 Properties70: { P:"Lcl Translation","Lcl Translation","","A",0,0,0 }
}
Deformer: 4001,"Deformer::Skin","Skin" { Version:101 Link_DeformAcuracy:50 }
Deformer: 4002,"SubDeformer::Bone","Cluster" {
 Version:100
 Indexes:*3 {a:0,1,2} Weights:*3 {a:1,1,1}
 Transform:*16 {a:1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}
 TransformLink:*16 {a:1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}
}
)"
                                     : "";
    return R"(; FBX 7.4.0 project file, authored by LuxEngine tests
FBXHeaderExtension: {FBXHeaderVersion:1003 FBXVersion:7400}
GlobalSettings: {Version:1000 Properties70: {
 P:"UpAxis","int","Integer","",1
 P:"UpAxisSign","int","Integer","",1
 P:"FrontAxis","int","Integer","",2
 P:"FrontAxisSign","int","Integer","",1
 P:"CoordAxis","int","Integer","",0
 P:"CoordAxisSign","int","Integer","",1
 P:"UnitScaleFactor","double","Number","",100
}}
Objects: {
Geometry:1001,"Geometry::Triangle","Mesh" {
 Vertices:*9 {a:0,0,0,1,0,0,0,1,0}
 PolygonVertexIndex:*3 {a:0,1,-3}
 LayerElementUV:0 { Version:101 Name:"UVMap"
 MappingInformationType:"ByPolygonVertex" ReferenceInformationType:"Direct"
 UV:*6 {a:0,0,1,0,0,1} }
 LayerElementMaterial:0 {Version:101 MappingInformationType:"AllSame"
 ReferenceInformationType:"IndexToDirect" Materials:*1 {a:0}}
}
Model:2001,"Model::Triangle","Mesh" {
 Version:232 Properties70: {
 P:"Lcl Translation","Lcl Translation","","A",0,0,0
 P:"GeometricTranslation","Vector3D","Vector","",0,0,0
 }}
Model:2002,"Model::Parent","Null" {
 Version:232 Properties70: { P:"Lcl Translation","Lcl Translation","","A",3,0,0 }
}
Material:5001,"Material::Copper","" { Version:102 ShadingModel:"phong"
 Properties70: {P:"DiffuseColor","Color","","A",0.2,0.4,0.6}}
AnimationStack:6001,"AnimStack::Move","" {Properties70: {
 P:"LocalStart","KTime","Time","",0
 P:"LocalStop","KTime","Time","",46186158000}}
AnimationLayer:6002,"AnimLayer::Base","" { }
AnimationCurveNode:6003,"AnimCurveNode::T","" {Properties70: {
 P:"d|X","Number","","A",0 P:"d|Y","Number","","A",0 P:"d|Z","Number","","A",0}}
AnimationCurve:6004,"AnimCurve::X","" {
 Default:0 KeyVer:4008
 KeyTime:*2 {a:0,46186158000} KeyValueFloat:*2 {a:0,2}
 KeyAttrFlags:*2 {a:4,4} KeyAttrDataFloat:*8 {a:0,0,0,0,0,0,0,0}
 KeyAttrRefCount:*2 {a:1,1}}
)" + skin + R"(
}
Connections: {
 C:"OO",1001,2001
 C:"OO",2001,2002
 C:"OO",2002,0
 C:"OO",5001,2001
 C:"OO",6002,6001
 C:"OO",6003,6002
 C:"OP",6004,6003,"d|X"
)" +
           (skinned ? R"(
 C:"OO",4001,1001
 C:"OO",4002,4001
 C:"OO",3001,4002
 C:"OO",3001,0
 C:"OP",6003,3001,"Lcl Translation"
)"
                    : R"( C:"OP",6003,2001,"Lcl Translation"
)") + "}\n";
}

void Write(const fs::path& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary);
    file << data;
    Check(static_cast<bool>(file), "Could not write FBX fixture");
}

void Replace(std::string& data, const std::string& before, const std::string& after) {
    const auto index = data.find(before);
    Check(index != std::string::npos, "FBX fixture replacement marker was not found");
    data.replace(index, before.size(), after);
}

void SubmeshesAndTextures(const fs::path& directory) {
    auto text = Fixture(false);
    Replace(text, "Vertices:*9 {a:0,0,0,1,0,0,0,1,0}", "Vertices:*12 {a:0,0,0,1,0,0,1,1,0,0,1,0}");
    Replace(text, "PolygonVertexIndex:*3 {a:0,1,-3}", "PolygonVertexIndex:*6 {a:0,1,-3,0,2,-4}");
    Replace(text, "UV:*6 {a:0,0,1,0,0,1}", "UV:*12 {a:0,0,1,0,1,1,0,0,1,1,0,1}");
    Replace(text, "MappingInformationType:\"AllSame\"", "MappingInformationType:\"ByPolygon\"");
    Replace(text, "Materials:*1 {a:0}", "Materials:*2 {a:0,1}");
    Replace(text, "AnimationStack:6001", R"(
Material:5002,"Material::Other","" {Version:102 ShadingModel:"phong"}
Texture:7001,"Texture::Color","" {
 Type:"TextureVideoClip" Version:202 TextureName:"Texture::Color"
 FileName:"Textures/color.png" RelativeFilename:"Textures/color.png"
}
Video:7002,"Video::Color","Clip" {
 Type:"Clip" Filename:"Textures/color.png" RelativeFilename:"Textures/color.png"
 Content: , "AQIDBA=="
}
AnimationStack:6001)");
    Replace(text, "C:\"OO\",5001,2001", R"(C:"OO",5001,2001
 C:"OO",5002,2001
 C:"OO",7002,7001
 C:"OP",7001,5001,"DiffuseColor")");
    const auto path = directory / "submeshes.fbx";
    Write(path, text);
    std::string error;
    const auto model = ModelAsset::LoadFbx(path.string(), error);
    Check(model != nullptr, error.c_str());
    Check(model->submeshes.size() == 2 && model->materials.size() == 2 &&
              model->submeshes[0].material != model->submeshes[1].material &&
              model->submeshes[0].indices.size() == 3 && model->submeshes[1].indices.size() == 3,
          "FBX face material slots were not split into independent submeshes");
    Check(model->textures.size() == 1 && model->materials[0].baseColorTexture == 0 &&
              model->textures[0].relativePath == "Textures/color.png",
          "FBX texture references were not linked to the material");
    Check(model->textures[0].embedded == std::vector<uint8>({1, 2, 3, 4}),
          "FBX encoded embedded texture bytes were not preserved exactly");
}

void MorphTargets(const fs::path& directory) {
    auto text = Fixture(false);
    Replace(text, "AnimationStack:6001", R"(
Geometry:8001,"Geometry::Raised","Shape" {
 Version:100 Indexes:*1 {a:0} Vertices:*3 {a:0,0,2} Normals:*3 {a:0,0,0}}
Deformer:8002,"Deformer::Morph","BlendShape" {Version:100}
Deformer:8003,"SubDeformer::Raised","BlendShapeChannel" {
 Version:100 DeformPercent:0 FullWeights:*1 {a:100}}
AnimationStack:6001)");
    Replace(text,
            "P:\"d|X\",\"Number\",\"\",\"A\",0 P:\"d|Y\",\"Number\",\"\",\"A\",0 "
            "P:\"d|Z\",\"Number\",\"\",\"A\",0",
            "P:\"d|DeformPercent\",\"Number\",\"\",\"A\",0");
    Replace(text, "KeyValueFloat:*2 {a:0,2}", "KeyValueFloat:*2 {a:0,100}");
    Replace(text, "C:\"OP\",6004,6003,\"d|X\"", "C:\"OP\",6004,6003,\"d|DeformPercent\"");
    Replace(text, "C:\"OP\",6003,2001,\"Lcl Translation\"", R"(
 C:"OO",8001,8003
 C:"OO",8003,8002
 C:"OO",8002,1001
 C:"OP",6003,8003,"DeformPercent")");
    const auto path = directory / "morph.fbx";
    Write(path, text);
    std::string error;
    const auto model = ModelAsset::LoadFbx(path.string(), error);
    Check(model != nullptr && model->submeshes[0].morphTargets, error.c_str());
    ModelPose rest, animated;
    Check(model->Evaluate(0, 0, rest, error) && model->Evaluate(.5, 0, animated, error),
          error.c_str());
    Check(Near(animated.submeshes[0][0].position.z - rest.submeshes[0][0].position.z, 1),
          "FBX CPU morph targets were not evaluated with their animated channel");
}

void CoordinateConversion(const fs::path& directory) {
    auto text = Fixture(false);
    Replace(text, "P:\"GeometricTranslation\",\"Vector3D\",\"Vector\",\"\",0,0,0", R"(
 P:"GeometricTranslation","Vector3D","Vector","",0,0,0
 P:"GeometricScaling","Vector3D","Vector","",-1,1,1)");
    const auto mirrored = directory / "mirrored.fbx";
    Write(mirrored, text);
    std::string error;
    const auto model = ModelAsset::LoadFbx(mirrored.string(), error);
    Check(model != nullptr, error.c_str());
    const auto& mesh = model->submeshes[0];
    const auto& a = mesh.vertices[mesh.indices[0]];
    const auto& b = mesh.vertices[mesh.indices[1]];
    const auto& c = mesh.vertices[mesh.indices[2]];
    const double x1 = b.position.x - a.position.x, y1 = b.position.y - a.position.y;
    const double x2 = c.position.x - a.position.x, y2 = c.position.y - a.position.y;
    Check((x1 * y2 - y1 * x2) * a.normal.z > 0,
          "FBX mirrored geometric transform inverted triangle winding relative to normals");
    text = Fixture(false);
    Replace(text, "P:\"UnitScaleFactor\",\"double\",\"Number\",\"\",100",
            "P:\"UnitScaleFactor\",\"double\",\"Number\",\"\",1");
    const auto centimeters = directory / "centimeters.fbx";
    Write(centimeters, text);
    const auto converted = ModelAsset::LoadFbx(centimeters.string(), error);
    Check(converted != nullptr, error.c_str());
    double maximumX = -1e30;
    for (const auto& submesh : converted->submeshes) {
        const auto* matrix = converted->nodes[submesh.node].worldTransform.elements;
        for (const auto& vertex : submesh.vertices)
            maximumX = std::max(maximumX, double(matrix[0] * vertex.position.x +
                                                 matrix[4] * vertex.position.y +
                                                 matrix[8] * vertex.position.z + matrix[12]));
    }
    Check(Near(static_cast<float>(maximumX), .04f),
          "FBX source centimeters were not normalized to meters through local/world transforms");
}

void Run(const fs::path& directory, const fs::path& primitives) {
    std::string error;
    Check(!ModelAsset::LoadFbx({}, error) && !error.empty(), "Empty FBX path was accepted");
    Check(!ModelAsset::LoadFbx(std::string("bad\0.fbx", 8), error),
          "Embedded NUL FBX path was accepted");
    Check(!ModelAsset::LoadFbx(std::string(4097, 'a'), error), "Overlong FBX path was accepted");
    Check(!ModelAsset::LoadFbx(std::string("\xc0\x80.fbx", 6), error),
          "Invalid UTF-8 FBX path was accepted");
    const auto path = directory / "hierarchy.fbx";
    Write(path, Fixture(false));
    const auto model = ModelAsset::LoadFbx(path.string(), error);
    Check(model != nullptr, error.c_str());
    Check(model->submeshes.size() == 1 && model->materials.size() == 1 &&
              model->animations.size() == 1,
          "FBX structured import lost mesh, material or clip");
    const auto& mesh = model->submeshes.front();
    Check(mesh.vertices.size() == 3 && mesh.indices.size() == 3 && mesh.material == 0 &&
              mesh.hasTexCoords && model->nodes[mesh.node].parent >= 0,
          "FBX hierarchy, material slot or UV was not preserved");
    Check(Near(model->materials[0].baseColor.x, .2f), "FBX diffuse/PBR fallback failed");
    ModelPose pose;
    Check(model->Evaluate(.5, 0, pose, error), error.c_str());
    Check(Near(pose.nodeLocalTransforms[mesh.node].position.x, 1),
          "FBX node animation is not sampled in clip-relative seconds");
    Check(Near(pose.submeshes[0][0].position.x, mesh.vertices[0].position.x),
          "Node animation was incorrectly baked into local vertices");
    Check(model->Evaluate(5, 0, pose, error) &&
              Near(pose.nodeLocalTransforms[mesh.node].position.x, 2),
          "FBX clip sampling does not clamp to the clip duration");
    Check(model->Evaluate(0, -1, pose, error) &&
              Near(pose.nodeLocalTransforms[mesh.node].position.x, 0),
          "FBX rest pose incorrectly evaluates the default animation");
    const auto old = pose.nodeLocalTransforms.size();
    Check(!model->Evaluate(std::numeric_limits<double>::quiet_NaN(), 0, pose, error) &&
              pose.nodeLocalTransforms.size() == old && !error.empty(),
          "Invalid sampling changed the caller's pose");
    Check(!model->Evaluate(0, 10, pose, error), "Invalid FBX clip index was accepted");
    Check(ModelAsset::LoadFbx(path.string(), error) == model,
          "FBX model cache missed unchanged file");
    Write(path, Fixture(false) + "\n; changed file\n");
    Check(ModelAsset::LoadFbx(path.string(), error) != model,
          "FBX model cache returned stale data");

    const auto skinPath = directory / "skin.fbx";
    Write(skinPath, Fixture(true));
    const auto skin = ModelAsset::LoadFbx(skinPath.string(), error);
    Check(skin != nullptr, error.c_str());
    Check(skin->submeshes[0].skinned && skin->submeshes[0].bones.size() == 1 &&
              skin->submeshes[0].vertices[0].weights.size() == 1 &&
              Near(skin->submeshes[0].vertices[0].weights[0].weight, 1),
          "FBX bones, bind matrices or vertex weights were lost");
    ModelPose rest, animated;
    Check(skin->Evaluate(0, 0, rest, error) && skin->Evaluate(.5, 0, animated, error),
          error.c_str());
    Check(Near(animated.submeshes[0][0].position.x - rest.submeshes[0][0].position.x, 1),
          "FBX CPU skinning did not move vertices with the animated bone");
    const auto boneNode = skin->submeshes[0].bones[0].node;
    Check(Near(animated.nodeLocalTransforms[boneNode].position.x, 1),
          "FBX bone animation transform is not exposed");
    if (!primitives.empty()) {
        size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(primitives)) {
            if (entry.path().extension() != ".fbx")
                continue;
            const auto primitive = ModelAsset::LoadFbx(entry.path().string(), error);
            Check(primitive != nullptr, error.c_str());
            Check(primitive->Evaluate(0, -1, pose, error) && !pose.submeshes.empty(),
                  error.c_str());
            ++count;
        }
        Check(count >= 10, "Bundled primitive FBX coverage is incomplete");
    }
    Write(directory / "broken.fbx", "invalid FBX");
    Check(!ModelAsset::LoadFbx((directory / "broken.fbx").string(), error) && !error.empty(),
          "Malformed FBX was accepted");
    SubmeshesAndTextures(directory);
    MorphTargets(directory);
    CoordinateConversion(directory);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
        Write(fs::path(argv[2]), Fixture(true));
        return 0;
    }
    const auto directory =
        fs::temp_directory_path() /
        ("lux-fbx-model-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(directory);
        Run(directory, argc > 1 ? fs::path(argv[1]) : fs::path{});
        fs::remove_all(directory);
        std::cout << "FBX model hierarchy/material/animation/CPU skinning checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        fs::remove_all(directory);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
