#include <LuxEditor/Viewport/ModelViewportGeometry.hpp>
#include <LuxEngine/Assets/ModelAsset.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace LuxEditor {
namespace {
struct RetainedModel {
    std::string path;
    LuxEngine::Ref<LuxEngine::ModelAsset> model;
};

LuxEngine::Ref<LuxEngine::ModelAsset> LoadModel(const std::string& path) {
    // The importer owns weak references. Retain a small working set so software viewports do
    // not parse the same FBX separately for bounds, selection and each animation frame.
    static std::vector<RetainedModel> retained;
    auto old = std::find_if(retained.begin(), retained.end(),
                            [&path](const auto& item) { return item.path == path; });
    std::string error;
    auto model = LuxEngine::ModelAsset::LoadFbx(path, error);
    if (!model)
        return old != retained.end() ? old->model : nullptr;
    if (old != retained.end())
        retained.erase(old);
    if (retained.size() >= 4)
        retained.erase(retained.begin());
    retained.push_back({path, model});
    return model;
}

struct Geometry {
    LuxEngine::Ref<LuxEngine::ModelAsset> model;
    const LuxEngine::ModelSubmesh* part = nullptr;
    LuxEngine::ModelPose pose;
    const std::vector<LuxEngine::ModelVertex>* vertices = nullptr;
};

bool Resolve(LuxEngine::Entity entity, Geometry& result) {
    const auto* mesh = entity.TryGetComponent<LuxEngine::MeshRendererComponent>();
    if (!mesh || mesh->modelSubmesh < 0 || mesh->meshPath.empty())
        return false;
    result.model = LoadModel(entity.GetScene()->ResolveAssetPath(mesh->meshPath));
    if (!result.model || static_cast<size_t>(mesh->modelSubmesh) >= result.model->submeshes.size())
        return false;
    result.part = &result.model->submeshes[static_cast<size_t>(mesh->modelSubmesh)];
    result.vertices = &result.part->vertices;
    const auto* animation = entity.TryGetComponent<LuxEngine::ModelAnimationComponent>();
    if (result.part->skinned || result.part->morphTargets || (animation && animation->clip >= 0)) {
        std::string error;
        if (!result.model->Evaluate(animation ? animation->time : 0,
                                    animation ? animation->clip : -1, result.pose, error) ||
            static_cast<size_t>(mesh->modelSubmesh) >= result.pose.submeshes.size())
            return false;
        result.vertices = &result.pose.submeshes[static_cast<size_t>(mesh->modelSubmesh)];
    }
    return !result.vertices->empty();
}

LuxEngine::Vec3 Transform(const LuxEngine::Mat4& matrix, LuxEngine::Vec3 point) {
    const auto* m = matrix.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}
} // namespace

bool ModelViewportBounds(LuxEngine::Entity entity, LuxEngine::Vec3& minimum,
                         LuxEngine::Vec3& maximum) {
    Geometry geometry;
    if (!Resolve(entity, geometry))
        return false;
    const float infinity = std::numeric_limits<float>::infinity();
    minimum = {infinity, infinity, infinity};
    maximum = {-infinity, -infinity, -infinity};
    for (const auto& vertex : *geometry.vertices) {
        const auto point = vertex.position;
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            return false;
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                   std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                   std::max(maximum.z, point.z)};
    }
    return true;
}

std::optional<double> IntersectModelViewport(const ViewportRay3D& ray, LuxEngine::Entity entity) {
    Geometry geometry;
    if (!entity.HasComponent<LuxEngine::TransformComponent>() || !Resolve(entity, geometry))
        return std::nullopt;
    const auto world = entity.GetScene()->GetWorldMatrix(entity.GetId());
    double nearest = std::numeric_limits<double>::infinity();
    const auto& indices = geometry.part->indices;
    const auto& vertices = *geometry.vertices;
    for (size_t index = 0; index + 2 < indices.size(); index += 3) {
        if (indices[index] >= vertices.size() || indices[index + 1] >= vertices.size() ||
            indices[index + 2] >= vertices.size())
            continue;
        const auto a = Transform(world, vertices[indices[index]].position);
        const auto b = Transform(world, vertices[indices[index + 1]].position);
        const auto c = Transform(world, vertices[indices[index + 2]].position);
        const auto first = b - a, second = c - a;
        const auto p = ray.direction.Cross(second);
        const double determinant = first.Dot(p);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
            continue;
        const auto offset = ray.origin - a;
        const double u = offset.Dot(p) / determinant;
        if (u < 0 || u > 1)
            continue;
        const auto q = offset.Cross(first);
        const double v = ray.direction.Dot(q) / determinant;
        if (v < 0 || u + v > 1)
            continue;
        const double distance = second.Dot(q) / determinant;
        if (std::isfinite(distance) && distance >= 0)
            nearest = std::min(nearest, distance);
    }
    return std::isfinite(nearest) ? std::optional<double>(nearest) : std::nullopt;
}
} // namespace LuxEditor
