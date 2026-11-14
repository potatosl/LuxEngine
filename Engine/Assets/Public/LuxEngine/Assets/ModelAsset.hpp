#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <LuxEngine/Core/Math.hpp>
#include <memory>
#include <string>
#include <vector>

namespace LuxEngine {

struct ModelTransform {
    Vec3 position;
    Vec3 rotation; // Euler XYZ, radians.
    Vec3 scale = {1.0f, 1.0f, 1.0f};
    Mat4 matrix;
};

struct ModelWeight {
    uint32 bone = 0;
    float weight = 0.0f;
};

struct ModelVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoords;
    std::vector<ModelWeight> weights;
};

struct ModelBone {
    std::string name;
    uint32 node = 0;
    // Maps this submesh's node-local vertices into the bone's bind space.
    Mat4 inverseBind;
};

struct ModelNode {
    std::string name;
    int32 parent = -1;
    ModelTransform localTransform;
    Mat4 worldTransform;
    std::vector<uint32> submeshes;
    bool bone = false;
};

struct ModelSubmesh {
    std::string name;
    uint32 node = 0;
    int32 material = -1;
    // Geometry transforms are baked in: vertices are in the owning node's local space.
    std::vector<ModelVertex> vertices;
    std::vector<uint32> indices;
    std::vector<ModelBone> bones;
    Vec3 boundsMin;
    Vec3 boundsMax;
    bool hasTexCoords = false;
    bool skinned = false;
    bool morphTargets = false;
};

struct ModelTexture {
    std::string name;
    std::string path;
    std::string relativePath;
    std::vector<uint8> embedded;
};

struct ModelMaterial {
    std::string name;
    Vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vec3 emissive;
    float metallic = 0.0f;
    float roughness = 1.0f;
    int32 baseColorTexture = -1;
    int32 normalTexture = -1;
    int32 metallicTexture = -1;
    int32 roughnessTexture = -1;
    int32 emissiveTexture = -1;
    int32 occlusionTexture = -1;
    bool doubleSided = false;
};

struct ModelAnimation {
    std::string name;
    double start = 0.0;
    double duration = 0.0;
};

struct ModelPose {
    std::vector<ModelTransform> nodeLocalTransforms;
    std::vector<Mat4> nodeWorldTransforms;
    // Parallel to ModelAsset::submeshes. CPU-deformed vertices remain node-local.
    std::vector<std::vector<ModelVertex>> submeshes;
};

class LUX_API ModelAsset {
public:
    static Ref<ModelAsset> LoadFbx(const std::string& filepath, std::string& error);
    ~ModelAsset();

    // Seconds are clip-relative and clamped. clip == -1 evaluates the default/rest scene.
    // CPU evaluation includes node animation, skinning and morph targets; no GPU is required.
    bool Evaluate(double seconds, int32 clip, ModelPose& output, std::string& error) const;

    std::string sourcePath;
    double sourceUnitMeters = 1.0;
    double framesPerSecond = 30.0;
    std::vector<ModelNode> nodes;
    std::vector<ModelSubmesh> submeshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelTexture> textures;
    std::vector<ModelAnimation> animations;
    std::vector<std::string> warnings;

private:
    struct Impl;
    ModelAsset();
    std::shared_ptr<Impl> impl_;
};

} // namespace LuxEngine
